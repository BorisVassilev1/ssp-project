#pragma once

#include <memory>
#include <queue>
#include <vector>
#include <cassert>
#include <atomic>
#include <cstdint>

/**
 * @brief A simple entity-pool-like container.
 *
 * Iterators must be checked for nullptr. Also Not thread safe
 */
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

	auto begin() const { return pool.begin(); }
	auto end() const { return pool.end(); }
};

/**
 * @brief SpinLock, copy pasta from lectures
 */
struct SpinLock {
	std::atomic_flag flag;
	void			 lock() {
		while (flag.test_and_set())
			;
	}

	void unlock() { flag.clear(); }

	bool tryLock() { return !flag.test_and_set(); }
};

struct RWSpinLock {
	std::atomic<std::intptr_t> state;
	enum {
		ONE_WRITER	= 1,
		ONE_READER	= 1 << 1,
		WRITER_MASK = ONE_WRITER,
		READER_MASK = ~WRITER_MASK,
	};

	bool upgradeRead();

	void lockWrite() {
		while (true) {
			std::intptr_t current = state.load(std::memory_order_relaxed);
			if (current == 0) {
				if (state.compare_exchange_strong(current, ONE_WRITER)) {
					return;		// acquired
				}
			}
		}
	}

	void unlockWrite() { state.fetch_and(READER_MASK); }

	void lockRead() {
		while (true) {
			const std::intptr_t current = state.load(std::memory_order_relaxed);
			if ((current & ONE_WRITER) == 0) {
				const std::intptr_t old = state.fetch_add(ONE_READER);
				if ((old & ONE_WRITER) == 0) {
					return;		// acquired
				}
				state.fetch_sub(ONE_READER);
			}
		}
	}
	void unlockRead() { state.fetch_sub(ONE_READER); }

	bool upgradeLock() {
		std::intptr_t current = state.load(std::memory_order_relaxed);
		while (current == ONE_READER) {
			if (state.compare_exchange_strong(current, current | ONE_WRITER)) {
				state.fetch_sub(ONE_READER);
				return true;	 // acquired
			}
		}
		unlockRead();
		lockWrite();
		return false;
	}
};
