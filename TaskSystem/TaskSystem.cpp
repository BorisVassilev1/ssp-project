#include "TaskSystem.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>

#if defined(_WIN32) || defined(_WIN64)
	#define USE_WIN
	#define WIN32_LEAN_AND_MEAN
	#include <Windows.h>
	#undef LoadLibrary
#else
	#include <dlfcn.h>
#endif

static void glfw_error_callback(int error, const char *description) {
	fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

namespace TaskSystem {

TaskSystemExecutor *TaskSystemExecutor::self = nullptr;

TaskSystemExecutor &TaskSystemExecutor::GetInstance() { return *self; }

bool TaskSystemExecutor::LoadLibrary(const std::string &path) {
#ifdef USE_WIN
	HMODULE handle = LoadLibraryA(path.c_str());
#else
	void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!handle) { printf("dlopen failed: %s\n", dlerror()); }
#endif
	assert(handle);
	if (handle) {
		OnLibraryInitPtr initLib =
#ifdef USE_WIN
			(OnLibraryInitPtr)GetProcAddress(handle, "OnLibraryInit");
#else
			(OnLibraryInitPtr)dlsym(handle, "OnLibraryInit");
#endif
		assert(initLib);
		if (initLib) {
			initLib(*this);
			printf("Initialized [%s] executor\n", path.c_str());
			return true;
		}
	}
	return false;
}

TaskSystemExecutor::TaskID TaskSystemExecutor::ScheduleTask(std::unique_ptr<Task> task, int priority) {
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

void TaskSystemExecutor::reschedule() {
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
		while (!scheduled.empty() && tasks[next].finished.test(std::memory_order_relaxed)) {
			scheduled.pop();
			next = scheduled.front();
		}
		if (!scheduled.empty()) {
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

TaskSystemExecutor::TaskSystemExecutor(int threadCount)
	: threadCount(threadCount), executing(threadCount), waiting(taskCmp) {
	worker = [this](int threadID) -> void {
		while (running) {
			if (!working) {
				std::unique_lock lock(workMtx);
				workCndVar.wait_for(lock, std::chrono::milliseconds(500), [this] { return working; });
			}

			auto execID = executing[threadID].load(std::memory_order_relaxed);
			if (execID == -1) {
				//std::this_thread::yield();
				continue;
			}

			addTaskLock.lock();
			auto &info = tasks[execID];
			addTaskLock.unlock();
			Executor::ExecStatus res = info.exec->ExecuteStep(threadID, this->threadCount);

			if (res == Executor::ExecStatus::ES_Stop) {
				if (!info.finished.test_and_set(std::memory_order_relaxed)) {
					info.state.store(TaskState::Finished, std::memory_order_relaxed);
					TaskCallback callback = info.callback.load(std::memory_order_relaxed);
					if (callback) callback(execID);
					info.done.notify_all();
					rescheduleCndVar.notify_all();
				}
				executing[threadID].compare_exchange_strong(execID, -1, std::memory_order_release);
				//std::this_thread::yield();
			}
		}
	};

	scheduler = [this] {
		glfwSetErrorCallback(glfw_error_callback);
		if (!glfwInit()) return;
		GLFWwindow *window = glfwCreateWindow(100, 60 * this->threadCount + 50, "Scheduler GUI", nullptr, nullptr);
		if (window == nullptr) return;
		glfwMakeContextCurrent(window);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO &io = ImGui::GetIO();
		(void)io;
		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 130");

		ImFontConfig cfg;
		cfg.SizePixels = 100.f;
		ImFont *f	   = io.Fonts->AddFontDefault();

		while (running) {
			glfwPollEvents();

			// Start the Dear ImGui frame
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
			{
				std::unique_lock lock(rescheduleMtx);
				rescheduleCndVar.wait_for(lock, std::chrono::milliseconds(33));
				// printf("%ld %ld %ld %ld\n", executing[0].load(), executing[1].load(), executing[2].load(),
				//	   executing[3].load());

				reschedule();
			}
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(io.DisplaySize);
			ImGui::Begin("Scheduler");
			ImGui::PushFont(f);
			{
				for (int i = 0; i < this->threadCount; ++i) {
					// ImGui::Text("thread %d:  %ld", i, executing[i].load(std::memory_order_relaxed));
					int			k							  = executing[i].load(std::memory_order_relaxed);
					std::string s							  = "task " + std::to_string(k);
					ImColor		col							  = ImColor::HSV((k % 5) / 10., 1, k != -1);
					ImGui::GetStyle().Colors[ImGuiCol_Button] = col;
					ImGui::GetStyle().Colors[ImGuiCol_Text] =
						ImColor(1.f - col.Value.x, 1.f - col.Value.y, 1.f - col.Value.z);
					ImGui::Button(s.c_str(), ImVec2(60, 60));
				}
			}
			ImGui::PopFont();
			ImGui::End();

			ImGui::Render();
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			glfwSwapBuffers(window);
		}
	};

	running = true;
	for (int i = 0; i < threadCount; ++i) {
		executing[i].store(-1, std::memory_order_relaxed);
		threads.push_back(std::thread(worker, i));
	}

	threads.push_back(std::thread(scheduler));
};

TaskSystemExecutor::~TaskSystemExecutor() {
	running = false;
	working = false;
	for (auto &th : threads) {
		if (th.joinable()) { th.join(); }
	}
}

void TaskSystemExecutor::OnTaskCompleted(TaskID task, TaskCallback callback) {
	if (tasks[task].finished.test(std::memory_order_relaxed)) {
		callback(task);
		return;
	}
	tasks[task].callback.store(callback, std::memory_order_relaxed);
}

void TaskSystemExecutor::TaskData::waitDone() {
	std::unique_lock lock(mtx);
	done.wait(lock, [this] { return finished.test(std::memory_order_relaxed); });
}

};	   // namespace TaskSystem
