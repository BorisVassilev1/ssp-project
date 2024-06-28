#pragma once

#include "Task.h"
#include "Executor.h"

#include <atomic>
#include <climits>
#include <condition_variable>
#include <iostream>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <chrono>

inline std::string operator""_lib(const char *str, std::size_t size) {
#if defined(_WIN32) || defined(_WIN64)
	return std::string{str} + ".dll";
#elif defined(__APPLE__)
	return "./lib" + std::string{str} + ".dylib";
#elif defined(__linux__)
	return "./lib" + std::string{str} + ".so";
#endif
}

namespace TaskSystem {

template <class T>
class Pool {
	std::vector<std::unique_ptr<T>> pool;
	std::queue<int>					free;

   public:
	std::size_t add(std::unique_ptr<T> obj) {
		if (free.empty()) {
			pool.push_back(std::move(obj));
			return pool.size() - 1;
		} else {
			auto i	= free.front();
			pool[i] = std::move(obj);
			free.pop();
			return i;
		}
	}

	void remove(std::size_t id) {
		pool[id] = std::move(nullptr);	   // TODO: remove this
		free.push(id);
	}

	T &operator[](std::size_t id) { return *pool[id]; }
};
struct SpinLock {
	std::atomic_flag flag;
	void			 lock() {
		while (flag.test_and_set())
			;
	}

	void unlock() { flag.clear(); }

	bool tryLock() { return !flag.test_and_set(); }
};

/**
 * @brief The task system main class that can accept tasks to be scheduled and execute them on multiple threads
 *
 */
struct TaskSystemExecutor {
   private:
	TaskSystemExecutor(int threadCount) : threadCount(threadCount), executing(threadCount), waiting(taskCmp) {
		running = true;
		for (int i = 0; i < threadCount; ++i) {
			executing[i].store(-1, std::memory_order_relaxed);
			threads.push_back(std::thread(worker, i));
		}

		threads.push_back(std::thread(scheduler));
	};

	~TaskSystemExecutor() {
		printf("joining threads...\n");
		running = false;
		working = false;
		for (auto &th : threads) {
			if (th.joinable()) { th.join(); }
		}
	}

	std::function<void(int)> worker = [this](int threadID) -> void {
		while (running) {
			if (!working) {
				std::unique_lock lock(workMtx);
				workCndVar.wait_for(lock, std::chrono::milliseconds(500), [this] { return working; });
			}

			auto execID = executing[threadID].load(std::memory_order_relaxed);
			if (execID == -1) {
				std::this_thread::yield();
				continue;
			}

			addTaskLock.lock();
			auto &info = tasks[execID];
			addTaskLock.unlock();
			Executor::ExecStatus res = info.exec->ExecuteStep(threadID, threadCount);

			if (res == Executor::ExecStatus::ES_Stop) {
				TaskState expected = TaskState::Executing;
				executing[threadID].compare_exchange_strong(execID, -1, std::memory_order_acquire);
				if (info.state.compare_exchange_strong(expected, TaskState::Finished, std::memory_order_relaxed)) {
					info.callback(execID);
					info.done.notify_all();
					rescheduleCndVar.notify_all();
				}
				std::this_thread::yield();
			}
		}
	};

	std::function<void(void)> scheduler = [this] {
		using namespace std::literals::chrono_literals;
		while (running) {
			std::unique_lock lock(rescheduleMtx);
			rescheduleCndVar.wait_for(lock, 33ms);
			printf("%ld %ld %ld %ld\n", executing[0].load(), executing[1].load(), executing[2].load(),
				   executing[3].load());

			reschedule();
		}
	};

	void reschedule() {
		// magic code that schedules new tasks if currrent are done
		if (scheduled.empty()) {
			int workingCount = 0;
			for (int i = 0; i < threadCount; ++i) {
				workingCount += executing[i].load(std::memory_order_relaxed) != -1;
				if (workingCount > 0) return;
			}

			if (waiting.empty()) {
				working = false;
				return;
			}

			int priority = tasks[waiting.top()].priority;
			while (!waiting.empty() && tasks[waiting.top()].priority == priority) {
				TaskID task = waiting.top();
				scheduled.push(task);
				tasks[task].state.store(TaskState::Scheduled, std::memory_order_relaxed);
				waiting.pop();
			}

			current_priority = priority;
		}

		// find an executing task to change and guarantee even
		// execution across threads and tasks
		{
			TaskID next = scheduled.front();
			while (tasks[next].state.load(std::memory_order_relaxed) == TaskState::Finished) {
				scheduled.pop();
				next = scheduled.front();
			}
			TaskID prev;
			while (executing[reschedule_idx].load(std::memory_order_relaxed) == next) {
				++reschedule_idx;
				reschedule_idx %= threadCount;
			}
			prev = executing[reschedule_idx].exchange(next);
			tasks[next].state.store(TaskState::Executing, std::memory_order_relaxed);
			scheduled.pop();
			if (prev != -1) {
				scheduled.push(prev);
				tasks[prev].state.store(TaskState::Scheduled, std::memory_order_relaxed);
				++reschedule_idx;
				reschedule_idx %= threadCount;
			}
		}
		// find idling threads and assign them work
		for (int i = 0; i < threadCount && !scheduled.empty(); ++i) {
			TaskID next		= scheduled.front();
			TaskID expected = -1;
			if (executing[i].compare_exchange_strong(expected, next, std::memory_order_relaxed)) {
				tasks[next].state.store(TaskState::Executing, std::memory_order_relaxed);
				scheduled.pop();
			}
		}
	}

   public:
	using TaskID = std::size_t;
	enum class TaskState {
		Waiting,
		Executing,
		Scheduled,
		Finished,
		Unknown,
	};

	TaskSystemExecutor(const TaskSystemExecutor &)			  = delete;
	TaskSystemExecutor &operator=(const TaskSystemExecutor &) = delete;

	static TaskSystemExecutor &GetInstance();

	/**
	 * @brief Initialisation called once by the main application to allocate needed resources and start threads
	 *
	 * @param threadCount the desired number of threads to utilize
	 */
	static void Init(int threadCount) {
		delete self;
		self = new TaskSystemExecutor(threadCount);
	}
	
	/**
	 * @brief Cleanup, must be called once by the main application to deallocate needed resources and stop all threads
	 *
	 */
	static void Shutdown() { delete self; }

	/**
	 * @brief Schedule a task with specific priority to be executed
	 *
	 * @param task the parameters describing the task, Executor will be instantiated based on the expected name
	 * @param priority the task priority, bigger means executer sooner
	 * @return TaskID unique identifier used in later calls to wait or schedule callbacks for tasks
	 */
	TaskID ScheduleTask(std::unique_ptr<Task> task, int priority) {
		std::unique_ptr<Executor> exec(executorConstructors[task->GetExecutorName()](std::move(task)));
		std::unique_ptr<TaskData> data = std::make_unique<TaskData>(std::move(exec), TaskState::Waiting, priority);
		TaskID					  res;
		{
			std::lock_guard lock(rescheduleMtx);
			addTaskLock.lock();
			res = tasks.add(std::move(data));
			addTaskLock.unlock();

			if (priority > current_priority) {
				for (int i = 0; i < threadCount; ++i) {
					auto current = executing[i].load(std::memory_order_relaxed);
					if (current != -1) {
						tasks[current].state.store(TaskState::Waiting, std::memory_order_relaxed);
						waiting.push(current);
					}
					executing[i].store(res, std::memory_order_relaxed);
				}
				tasks[res].state.store(TaskState::Executing, std::memory_order_relaxed);
				current_priority = priority;
			} else if (priority == current_priority) {
				for (int i = 0; i < threadCount; ++i)
					scheduled.push(res);
				tasks[res].state.store(TaskState::Scheduled, std::memory_order_relaxed);
			} else {
				for (int i = 0; i < threadCount; ++i)
					waiting.push(res);
				tasks[res].state.store(TaskState::Waiting, std::memory_order_relaxed);
			}
		}
		if (!working) {
			working = true;
			workCndVar.notify_all();
		}
		return res;
	}

	/**
	 * @brief Blocking wait for a given task. Does not block if the task has already finished
	 *
	 * @param task the task to wait for
	 */
	void WaitForTask(TaskID task) {
		tasks[task].waitDone();
		return;
	}

	/**
	 * @brief Register a callback to be executed when a task has finished executing. Executes the callbacl
	 *        immediately if the task has already finished
	 *
	 * @param task the task that was previously scheduled
	 * @param callback the callback to be executed
	 */
	void OnTaskCompleted(TaskID task, std::function<void(TaskID)> &&callback) {
		tasks[task].callback = std::move(callback);
	}

	/**
	 * @brief Get the state of a given task
	 *
	 * @param task
	 * @return TaskState
	 */
	TaskState GetTaskState(TaskID task) { return tasks[task].state.load(); }

	/**
	 * @brief Load a dynamic library from a path and attempt to call OnLibraryInit
	 *
	 * @param path the path to the dynamic library
	 * @return true when OnLibraryInit is found, false otherwise
	 */
	bool LoadLibrary(const std::string &path);

	/**
	 * @brief Register an executor with a name and constructor function. Should be called from
	 *        inside the dynamic libraries defining executors.
	 *
	 * @param executorName the name associated with the executor
	 * @param constructor constructor returning new instance of the executor
	 */
	void Register(const std::string &executorName, ExecutorConstructor constructor) {
		executorConstructors[executorName] = constructor;
	}

   private:
	struct TaskData {
		std::unique_ptr<Executor>	exec;
		std::atomic<TaskState>		state = TaskState::Unknown;
		std::condition_variable		done;
		std::mutex					mtx;
		std::function<void(TaskID)> callback;
		int							priority;

		TaskData(std::unique_ptr<Executor> exec, TaskState state, int priority)
			: exec(std::move(exec)), state(state), callback([](TaskID) {}), priority(priority) {}
		void waitDone() {
			std::unique_lock lock(mtx);
			done.wait(lock, [this] { return state.load() == TaskState::Finished; });
		}
	};

	static TaskSystemExecutor				  *self;
	std::map<std::string, ExecutorConstructor> executorConstructors;

	std::condition_variable workCndVar;
	std::mutex				workMtx;

	std::condition_variable rescheduleCndVar;
	std::mutex				rescheduleMtx;

	SpinLock addTaskLock;

	Pool<TaskData>					 tasks;
	std::vector<std::thread>		 threads;
	std::vector<std::atomic<TaskID>> executing;

	std::function<bool(TaskID, TaskID)> taskCmp = [this](TaskID t1, TaskID t2) -> bool {
		return tasks[t1].priority < tasks[t2].priority;
	};
	std::queue<TaskID>																	  scheduled;
	std::priority_queue<TaskID, std::vector<TaskID>, std::function<bool(TaskID, TaskID)>> waiting;
	int																					  current_priority = INT_MIN;

	volatile bool working = false;
	volatile bool running = false;
	int			  threadCount;

	int reschedule_idx = 0;
};

};	   // namespace TaskSystem
