#include "TaskSystem.h"

#include <cassert>

#if defined(_WIN32) || defined(_WIN64)
#define USE_WIN
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef LoadLibrary
#else
#include <dlfcn.h>
#endif

namespace TaskSystem {

TaskSystemExecutor* TaskSystemExecutor::self = nullptr;

TaskSystemExecutor &TaskSystemExecutor::GetInstance() {
    return *self;
}

bool TaskSystemExecutor::LoadLibrary(const std::string &path) {
#ifdef USE_WIN
    HMODULE handle = LoadLibraryA(path.c_str());
#else
    void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
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

};
