#pragma once

#include "Task.h"
#include "Executor.h"
#include "Utils.hpp"

#include <atomic>
#include <cassert>
#include <climits>
#include <condition_variable>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

inline std::string operator""_lib(const char *str, std::size_t size) {
#if defined(_WIN32) || defined(_WIN64)
	return "./lib" + std::string{str} + ".dll";
#elif defined(__APPLE__)
	return "./lib" + std::string{str} + ".dylib";
#elif defined(__linux__)
	return "./lib" + std::string{str} + ".so";
#endif
}

namespace TaskSystem {

/**
 * @brief The task system main class that can accept tasks to be scheduled and execute them on multiple threads
 *
 */
struct TaskSystemExecutor {
   private:
	class TaskData;

   public:
	using TaskID	   = std::size_t;
	using TaskCallback = void (*)(TaskID);
	enum class TaskState {
		Waiting,
		Executing,
		Scheduled,
		Finished,
		Unknown,
	};

   private:
	TaskSystemExecutor(int threadCount, bool gui = false);

	~TaskSystemExecutor();

	std::function<void(int)>  worker;
	std::function<void(void)> scheduler;

	void reschedule();
	void threadFinished(TaskData &info, TaskID taskID);

	void drawGui();

   public:
	TaskSystemExecutor(const TaskSystemExecutor &)			  = delete;
	TaskSystemExecutor &operator=(const TaskSystemExecutor &) = delete;

	static TaskSystemExecutor &GetInstance();

	/**
	 * @brief Initialisation called once by the main application to allocate needed resources and start threads
	 *
	 * @param threadCount the desired number of threads to utilize. If zero, uses defaults to
	 * the core count of the machine.
	 */
	static void Init(int threadCount = 0, bool gui = false) {
		delete self;
		if (threadCount == 0) threadCount = std::thread::hardware_concurrency();
		self = new TaskSystemExecutor(threadCount, gui);
	}

	/**
	 * @brief converts TaskState to string.
	 */
	static const char *TaskStateString(TaskState s) {
		static const char *names[] = {"Waiting", "Executing", "Scheduled", "Finished", "Unknown"};
		return names[int(s)];
	}

	/**
	 * @brief Cleanup, must be called once by the main application to deallocate needed resources and stop all threads.
	 * Calling this before waiting for all tasks to complete is undefined behaviour
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
	void OnTaskCompleted(TaskID task, TaskCallback callback);

	/**
	 * @brief Get the state of a given task
	 *
	 * @param task
	 * @return TaskState
	 */
	TaskState GetTaskState(TaskID task) { return tasks[task].state.load(std::memory_order_relaxed); }

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

	/**
	 * @brief Destroys a task and forgets about it's existence
	 *
	 */
	void DestroyTask(TaskID task) {
		assert(tasks[task].state.load(std::memory_order_relaxed) == TaskState::Finished);
		assert(tasks[task].finished.load(std::memory_order_relaxed) == threadCount);
		addTaskLock.lockWrite();
		tasks.remove(task);
		addTaskLock.unlockWrite();
	}

   private:
	struct TaskData {
		std::unique_ptr<Executor> exec;
		std::atomic<TaskState>	  state			 = TaskState::Unknown;
		std::atomic_flag		  doNotSchedule	 = false;
		std::atomic<int>		  finished		 = 0;
		std::atomic<int>		  executingCount = 0;
		std::condition_variable	  done;
		std::mutex				  mtx;
		std::atomic<TaskCallback> callback;
		int						  priority;
		TaskID					  id;

		TaskData(std::unique_ptr<Executor> exec, TaskState state, int priority)
			: exec(std::move(exec)), state(state), callback([](TaskID) {}), priority(priority) {}

		void waitDone();
	};

	static TaskSystemExecutor				  *self;
	std::map<std::string, ExecutorConstructor> executorConstructors;

	std::condition_variable workCndVar;
	std::mutex				workMtx;

	std::condition_variable rescheduleCndVar;
	std::mutex				rescheduleMtx;

	RWSpinLock addTaskLock;

	Pool<TaskData>					 tasks;
	std::vector<std::thread>		 threads;
	std::vector<std::atomic<TaskID>> executing;

	std::function<bool(TaskID, TaskID)> taskCmp = [this](TaskID t1, TaskID t2) -> bool {
		return tasks[t1].priority < tasks[t2].priority;
	};
	std::deque<TaskID>																	  scheduled;
	std::priority_queue<TaskID, std::vector<TaskID>, std::function<bool(TaskID, TaskID)>> waiting;
	int																					  current_priority = INT_MIN;

	volatile bool working = false;
	volatile bool running = false;
	const int	  threadCount;
	const bool	  gui;

	int reschedule_idx = 0;
};

};	   // namespace TaskSystem
