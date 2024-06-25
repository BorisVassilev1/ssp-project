#pragma once

#include "Task.h"
#include "Executor.h"

#include <map>
#include <functional>


inline std::string operator""_lib(const char* str, std::size_t size) {
#if defined(_WIN32) || defined(_WIN64)
	return std::string{str} + ".dll";
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
    TaskSystemExecutor(int threadCount) {};
public:
    enum class TaskState {
        Waiting,
        Executing,
        Scheduled,
        Finished,
        Unknown,
    };


    TaskSystemExecutor(const TaskSystemExecutor &) = delete;
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

    struct TaskID {};

    /**
     * @brief Schedule a task with specific priority to be executed
     *
     * @param task the parameters describing the task, Executor will be instantiated based on the expected name
     * @param priority the task priority, bigger means executer sooner
     * @return TaskID unique identifier used in later calls to wait or schedule callbacks for tasks
     */
    TaskID ScheduleTask(std::unique_ptr<Task> task, int priority) {
        std::unique_ptr<Executor> exec(executors[task->GetExecutorName()](std::move(task)));

        while (exec->ExecuteStep(0, 1) != Executor::ExecStatus::ES_Stop)
            ;

        return TaskID{};
    }

    /**
     * @brief Blocking wait for a given task. Does not block if the task has already finished
     *
     * @param task the task to wait for
     */
    void WaitForTask(TaskID task) {
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
        callback(task);
    }

    /**
     * @brief Get the state of a given task
     *
     * @param task
     * @return TaskState
     */
    TaskState GetTaskState(TaskID task) {
        return TaskState::Waiting;
    }

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
        executors[executorName] = constructor;
    }

private:
    static TaskSystemExecutor *self;
    std::map<std::string, ExecutorConstructor> executors;
};

};
