#pragma once


#include "Task.h"
#include "Executor.h"

#include <atomic>
#include <climits>
#include <condition_variable>
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
	TaskSystemExecutor(int threadCount);

	~TaskSystemExecutor();

	std::function<void(int)> worker;
	std::function<void(void)> scheduler;

	void reschedule();

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
	TaskID ScheduleTask(std::unique_ptr<Task> task, int priority);

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
