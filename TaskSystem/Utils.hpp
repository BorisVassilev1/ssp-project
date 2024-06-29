#pragma once

#include <memory>
#include <queue>
#include <vector>
#include <cassert>

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

	T &operator[](std::size_t id) { 
		assert(id >= 0 && id < pool.size() && pool[id] != nullptr);
		return *pool[id]; 
	}

	auto begin() const {return pool.begin();}
	auto end() const {return pool.end();}
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
