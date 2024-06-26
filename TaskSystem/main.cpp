#include "TaskSystem.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <optional>
#include <thread>

using namespace TaskSystem;

struct PrinterParams : Task {
	int max;
	int sleep;

	PrinterParams(int max, int sleep) : max(max), sleep(sleep) {}
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

	RaytracerParams(const std::string &sceneName) : sceneName(sceneName) {}
	virtual std::optional<std::string> GetStringParam(const std::string &name) const {
		if (name == "sceneName") { return sceneName; }
		return std::nullopt;
	}
	virtual std::string GetExecutorName() const { return "raytracer"; }
};

struct BRDFParams : Task {
	std::string name;
	int			size;
	int			sample_count;

	BRDFParams(const std::string &name, int size, int sample_count)
		: name(name), size(size), sample_count(sample_count) {}

	std::optional<std::string> GetStringParam(const std::string &name) const override {
		if (name == "name") return this->name;
		return std::nullopt;
	}
	std::optional<int> GetIntParam(const std::string &name) const override {
		if (name == "size") return this->size;
		if (name == "sample_count") return this->sample_count;
		return std::nullopt;
	}
	std::string GetExecutorName() const override { return "BRDFBuilder"; }
};

void testRenderer() {
	TaskSystemExecutor &ts = TaskSystemExecutor::GetInstance();

	const bool libLoaded = ts.LoadLibrary("RaytracerExecutor"_lib);
	assert(libLoaded);
	std::unique_ptr<Task> task1 = std::make_unique<RaytracerParams>("HeavyMesh");
	std::unique_ptr<Task> task2 = std::make_unique<RaytracerParams>("ManySimpleMeshes");
	// std::unique_ptr<Task> task3 = std::make_unique<RaytracerParams>("ManyHeavyMeshes");

	TaskSystemExecutor::TaskID id1 = ts.ScheduleTask(std::move(task1), 100);
	TaskSystemExecutor::TaskID id2 = ts.ScheduleTask(std::move(task2), 100);
	// TaskSystemExecutor::TaskID id3 = ts.ScheduleTask(std::move(task3), 100);

	ts.OnTaskCompleted(id1, [](TaskSystemExecutor::TaskID id) { printf("Render 1 finished\n"); });
	ts.OnTaskCompleted(id2, [](TaskSystemExecutor::TaskID id) { printf("Render 2 finished\n"); });
	// ts.OnTaskCompleted(id3, [](TaskSystemExecutor::TaskID id) { printf("Render 3 finished\n"); });

	ts.WaitForTask(id1);
	ts.WaitForTask(id2);

	ts.DestroyTask(id1);
	ts.DestroyTask(id2);

	// ts.WaitForTask(id3);
}

void testPrinter() {
	TaskSystemExecutor &ts		  = TaskSystemExecutor::GetInstance();
	const bool			libLoaded = ts.LoadLibrary("PrinterExecutor"_lib);
	assert(libLoaded);

	// two instances of the same task
	std::unique_ptr<Task> p1 = std::make_unique<PrinterParams>(110, 25);
	std::unique_ptr<Task> p2 = std::make_unique<PrinterParams>(120, 25);
	std::unique_ptr<Task> p3 = std::make_unique<PrinterParams>(130, 25);
	std::unique_ptr<Task> p4 = std::make_unique<PrinterParams>(140, 25);

	// give some time for the first task to execute
	TaskSystemExecutor::TaskID id1 = ts.ScheduleTask(std::move(p1), 10);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	// insert bigger priority task, TaskSystem should switch to it
	TaskSystemExecutor::TaskID id2 = ts.ScheduleTask(std::move(p2), 20);
	TaskSystemExecutor::TaskID id3 = ts.ScheduleTask(std::move(p3), 20);
	TaskSystemExecutor::TaskID id4 = ts.ScheduleTask(std::move(p4), 20);

	ts.OnTaskCompleted(id1, [](TaskSystemExecutor::TaskID id) { printf("Task 1 finished\n"); });
	ts.OnTaskCompleted(id2, [](TaskSystemExecutor::TaskID id) { printf("Task 2 finished\n"); });
	ts.WaitForTask(id1);
	ts.WaitForTask(id2);
	ts.WaitForTask(id3);
	ts.WaitForTask(id4);

	ts.DestroyTask(id1);
	ts.DestroyTask(id2);
	ts.DestroyTask(id3);
	ts.DestroyTask(id4);
}

TaskSystemExecutor::TaskID testBRDF() {
	TaskSystemExecutor &ts		  = TaskSystemExecutor::GetInstance();
	const bool			libLoaded = ts.LoadLibrary("BRDFExecutor"_lib);
	assert(libLoaded);

	std::unique_ptr<Task>	   p  = std::make_unique<BRDFParams>("brdf_1k", 512, 100);
	TaskSystemExecutor::TaskID id = ts.ScheduleTask(std::move(p), 10);

	ts.OnTaskCompleted(id, [](TaskSystemExecutor::TaskID id) { printf("BRDF created!!!\n"); });
	return id;
}

int main(int argc, char *argv[]) {
	int threads;

	if (argc == 1) {
		threads = 0;
	} else {
		threads = std::stoi(argv[1]);
	}

	TaskSystemExecutor::Init(threads, argc > 2);

	for (int i = 0; i < 10; ++i) {
		auto id = testBRDF();
		testRenderer();

		testPrinter();
		TaskSystemExecutor::GetInstance().WaitForTask(id);
		TaskSystemExecutor::GetInstance().DestroyTask(id);
	}

	TaskSystemExecutor::Shutdown();
	return 0;
}
