#include "TaskSystem.h"

#include <cassert>
#include <chrono>
#include <thread>

using namespace TaskSystem;

struct PrinterParams : Task {
    int max;
    int sleep;

    PrinterParams(int max, int sleep): max(max), sleep(sleep) {}
    virtual std::optional<int> GetIntParam(const std::string &name) const {
        if (name == "max") {
            return max;
        } else if (name == "sleep") {
            return sleep;
        }
        return std::nullopt;
    }
    virtual std::string GetExecutorName() const { return "printer"; }
};


struct RaytracerParams : Task {
    std::string sceneName;

    RaytracerParams(const std::string &sceneName): sceneName(sceneName) {}
    virtual std::optional<std::string> GetStringParam(const std::string &name) const {
        if (name == "sceneName") {
            return sceneName;
        }
        return std::nullopt;
    }
    virtual std::string GetExecutorName() const { return "raytracer"; }
};

void testRenderer() {
    TaskSystemExecutor &ts = TaskSystemExecutor::GetInstance();

    const bool libLoaded = ts.LoadLibrary("RaytracerExecutor"_lib);
    assert(libLoaded);
    std::unique_ptr<Task> task1 = std::make_unique<RaytracerParams>("HeavyMesh");
    std::unique_ptr<Task> task2 = std::make_unique<RaytracerParams>("HeavyMesh");
    std::unique_ptr<Task> task3 = std::make_unique<RaytracerParams>("HeavyMesh");

    TaskSystemExecutor::TaskID id1 = ts.ScheduleTask(std::move(task1), 100);
    TaskSystemExecutor::TaskID id2 = ts.ScheduleTask(std::move(task2), 100);
    TaskSystemExecutor::TaskID id3 = ts.ScheduleTask(std::move(task3), 100);

    ts.OnTaskCompleted(id1, [](TaskSystemExecutor::TaskID id) {
        printf("Render 1 finished\n");
    });
    ts.OnTaskCompleted(id2, [](TaskSystemExecutor::TaskID id) {
        printf("Render 2 finished\n");
    });
    ts.WaitForTask(id1);
    ts.WaitForTask(id2);
    ts.WaitForTask(id3);
}

void testPrinter() {
    TaskSystemExecutor &ts = TaskSystemExecutor::GetInstance();
    const bool libLoaded = ts.LoadLibrary("PrinterExecutor"_lib);
    assert(libLoaded);

    // two instances of the same task
    std::unique_ptr<Task> p1 = std::make_unique<PrinterParams>(100, 25);
    std::unique_ptr<Task> p2 = std::make_unique<PrinterParams>(50, 25);

    // give some time for the first task to execute
    TaskSystemExecutor::TaskID id1 = ts.ScheduleTask(std::move(p1), 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // insert bigger priority task, TaskSystem should switch to it
    TaskSystemExecutor::TaskID id2 = ts.ScheduleTask(std::move(p2), 20);

    ts.OnTaskCompleted(id1, [](TaskSystemExecutor::TaskID id) {
        printf("Task 1 finished\n");
    });
    ts.OnTaskCompleted(id2, [](TaskSystemExecutor::TaskID id) {
        printf("Task 2 finished\n");
    });
    ts.WaitForTask(id2);
    ts.WaitForTask(id1);
}

int main(int argc, char *argv[]) {
    TaskSystemExecutor::Init(4);

	testRenderer();
    //testPrinter();


	TaskSystemExecutor::Shutdown();
    return 0;
}
