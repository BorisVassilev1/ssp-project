#include "TaskSystem.h"
#include "Window.hpp"
#include "imgui.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <thread>

#if defined(_WIN32) || defined(_WIN64)
	#define USE_WIN
	#define WIN32_LEAN_AND_MEAN
	#include <Windows.h>
	#undef LoadLibrary
#else
	#include <dlfcn.h>
#endif

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
		res			  = tasks.add(std::move(data));
		tasks[res].id = res;
		addTaskLock.unlock();

		if (priority > current_priority) {
			for (int i = 0; i < threadCount; ++i) {
				auto current = executing[i].exchange(res, std::memory_order_relaxed);
				if (current != -1) {
					tasks[current].state.store(TaskState::Waiting, std::memory_order_relaxed);
					waiting.push(current);
				}
			}
			while (!scheduled.empty()) {
				TaskID task = scheduled.front();
				scheduled.pop_front();
				waiting.push(task);
			}
			tasks[res].state.store(TaskState::Executing, std::memory_order_relaxed);
			tasks[res].executingCount.store(threadCount, std::memory_order_relaxed);
			current_priority = priority;
		} else if (priority == current_priority) {
			for (int i = 0; i < threadCount; ++i)
				scheduled.push_back(res);
			tasks[res].state.store(TaskState::Scheduled, std::memory_order_relaxed);
		} else {
			for (int i = 0; i < threadCount; ++i)
				waiting.push(res);
			tasks[res].state.store(TaskState::Waiting, std::memory_order_relaxed);
		}
	}

	rescheduleCndVar.notify_all();
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
		}
		if (workingCount == threadCount) return;
		// proceed only if there are starving threads

		if (waiting.empty()) {
			working = false;
			return;
		}

		int priority = tasks[waiting.top()].priority;
		while (!waiting.empty() && tasks[waiting.top()].priority == priority) {
			TaskID task = waiting.top();
			if(tasks[task].doNotSchedule.test(std::memory_order_relaxed)) {
				threadFinished(tasks[task], task);
			} else {
				scheduled.push_back(task);
				tasks[task].state.store(TaskState::Scheduled, std::memory_order_relaxed);
			}
			waiting.pop();
		}
		current_priority = priority;
	}

	// find an executing task to change and guarantee even
	// execution across threads and tasks
	{
		TaskID next = scheduled.front();
		// filter out already finished threads
		while (!scheduled.empty() && tasks[next].doNotSchedule.test(std::memory_order_relaxed)) {
			threadFinished(tasks[next], next);
			scheduled.pop_front();
			if(!scheduled.empty()) next = scheduled.front();
		}
		if (scheduled.empty()) return;
		TaskID prev;
		while (executing[reschedule_idx].load(std::memory_order_relaxed) == next) {
			++reschedule_idx;
			reschedule_idx %= threadCount;
		}
		prev = executing[reschedule_idx].exchange(next);
		tasks[next].executingCount.fetch_add(1, std::memory_order_relaxed);
		scheduled.pop_front();
		if (prev != -1) {
			scheduled.push_back(prev);
			tasks[prev].executingCount.fetch_add(-1, std::memory_order_relaxed);
			++reschedule_idx;
			reschedule_idx %= threadCount;
		}
	}

	// find idling threads and assign them work
	for (int i = 0; i < threadCount && !scheduled.empty(); ++i) {
		TaskID next = scheduled.front();
		while (!scheduled.empty() &&
			   tasks[next].doNotSchedule.test(std::memory_order_relaxed)) {
			threadFinished(tasks[next], next);
			scheduled.pop_front();
			if(!scheduled.empty()) next = scheduled.front();
		}
		if (scheduled.empty()) break;
		TaskID expected = -1;
		if (executing[i].compare_exchange_strong(expected, next, std::memory_order_relaxed)) {
			tasks[next].executingCount.fetch_add(1, std::memory_order_relaxed);
			scheduled.pop_front();
		}
	}

	for (TaskID id : scheduled) {
		if (tasks[id].executingCount.load(std::memory_order_relaxed) > 0) {
			tasks[id].state.store(TaskState::Executing);
		} else {
			if (tasks[id].finished.load(std::memory_order_relaxed) == threadCount) {
				//tasks[id].state.store(TaskState::Finished);
			}
			else tasks[id].state.store(TaskState::Scheduled);
		}
	}
}

void TaskSystemExecutor::threadFinished(TaskData &info, TaskID taskID) {
	int res;
	if ((res = info.finished.fetch_add(1, std::memory_order_relaxed)) >= this->threadCount - 1) {
		assert(res <= this->threadCount - 1);
		TaskCallback callback = info.callback.load(std::memory_order_relaxed);
		if (callback) callback(taskID);
		TaskState prev = info.state.exchange(TaskState::Finished, std::memory_order_relaxed);
		assert(prev != TaskState::Finished);
		info.done.notify_all();
	}
}

void TaskSystemExecutor::drawGui() {
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
	ImGui::Begin("a", NULL,
				 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration);
	{
		ImColor defaultText = ImGui::GetStyle().Colors[ImGuiCol_Text];
		for (int i = 0; i < this->threadCount; ++i) {
			int			k							  = executing[i].load(std::memory_order_relaxed);
			std::string s							  = "task " + std::to_string(k);
			ImColor		col							  = ImColor::HSV((k % 5) / 10., 1, k != -1);
			ImGui::GetStyle().Colors[ImGuiCol_Button] = col;
			ImGui::GetStyle().Colors[ImGuiCol_Text] = ImColor(1.f - col.Value.x, 1.f - col.Value.y, 1.f - col.Value.z);
			if (i) ImGui::SameLine();
			ImGui::Button(s.c_str(), ImVec2(60, 60));
		}
		ImGui::GetStyle().Colors[ImGuiCol_Text] = defaultText;
		ImGui::Text("tasks:");

		for (auto &ptr : tasks) {
			if (ptr == nullptr) continue;
			const char *state = TaskSystemExecutor::TaskStateString(ptr->state.load(std::memory_order_relaxed));
			ImGui::Text("task %2zu, priority %4d, status %s", ptr->id, ptr->priority, state);
		}
		ImGui::Text("\nscheduled: %zu\nwaiting: %zu", scheduled.size(), waiting.size());
	}
	ImGui::End();
}

TaskSystemExecutor::TaskSystemExecutor(int threadCount, bool gui)
	: threadCount(threadCount), executing(threadCount), waiting(taskCmp), gui(gui) {
	worker = [this](int threadID) -> void {
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
			TaskData &info = tasks[execID];
			assert(info.state.load() != TaskState::Finished);
			addTaskLock.unlock();
			Executor::ExecStatus res = info.exec->ExecuteStep(threadID, this->threadCount);

			if (res == Executor::ExecStatus::ES_Stop) {
				//std::lock_guard lock(rescheduleMtx); // TODO: if something breaks, uncomment
				//printf("TASK %zu FINISH %d\n", execID, threadID);
				info.doNotSchedule.test_and_set(std::memory_order_relaxed);
				if (executing[threadID].compare_exchange_strong(execID, -1, std::memory_order_acq_rel)) {
					info.executingCount.fetch_add(-1, std::memory_order_relaxed);
					threadFinished(info, execID);
				}

				rescheduleCndVar.notify_all();
				std::this_thread::yield();
			}
		}
	};

	scheduler = [this] {
		Window *window = nullptr;
		if (this->gui) window = new Window(70 * this->threadCount + 10, 250, "Scheduler GUI");
		int waitTime = std::ceil(200. / this->threadCount);

		while (running || (this->gui && !window->shouldClose())) {
			if (this->gui) window->BeginFrame();

			{
				std::unique_lock lock(rescheduleMtx);
				rescheduleCndVar.wait_for(lock, std::chrono::milliseconds(waitTime));
				reschedule();
			}

			if (this->gui) {
				drawGui();
				window->SwapBuffers();
			}
		}
		delete window;
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
	if (tasks[task].state.load(std::memory_order_relaxed) == TaskState::Finished) {
		callback(task);
		return;
	}
	tasks[task].callback.store(callback, std::memory_order_relaxed);
}

void TaskSystemExecutor::TaskData::waitDone() {
	std::unique_lock lock(mtx);
	done.wait(lock, [this] { return state.load(std::memory_order_relaxed) == TaskState::Finished;});
}

};	   // namespace TaskSystem
