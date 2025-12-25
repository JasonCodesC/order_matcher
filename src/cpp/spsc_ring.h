#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <thread>

struct SpinWait {
    uint32_t count = 0;

    inline void pause() {
        if (count < 64) {
            std::atomic_signal_fence(std::memory_order_relaxed);
        } 
        else if (count < 128) {
            std::this_thread::yield();
        } 
        else {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
        ++count;
    }

    inline void reset() { count = 0; }
};

template <typename T, uint32_t N>
class SpscRing {
    static_assert((N & (N - 1)) == 0, "N must be power of two");

    // cache align
    alignas(64) std::array<T, N> buf_{};
    alignas(64) std::atomic<uint32_t> read_ptr{0};
    alignas(64) std::atomic<uint32_t> write_ptr{0};
    alignas(64) uint32_t cached_write_ptr{0};
    alignas(64) uint32_t cached_read_ptr{0};

    public:

    // acquire slot pointer then commit when populated
    inline bool try_acquire_producer_slot(T*& slot) {
        const uint32_t head = read_ptr.load(std::memory_order_relaxed);
        if (head - cached_write_ptr == N) {
            cached_write_ptr = write_ptr.load(std::memory_order_acquire);
            if (head - cached_write_ptr == N) {return false;}
        }
        slot = &buf_[head & (N - 1)];
        return true;
    }

    inline void commit_producer_slot() {
        const uint32_t head = read_ptr.load(std::memory_order_relaxed);
        read_ptr.store(head + 1, std::memory_order_release);
    }

    inline bool try_acquire_consumer_slot(T*& slot) {
        const uint32_t tail = write_ptr.load(std::memory_order_relaxed);
        if (cached_read_ptr == tail) {
            cached_read_ptr = read_ptr.load(std::memory_order_acquire);
            if (cached_read_ptr == tail) {return false;}
        }
        slot = &buf_[tail & (N - 1)];
        return true;
    }

    inline void release_consumer_slot() {
        const uint32_t tail = write_ptr.load(std::memory_order_relaxed);
        write_ptr.store(tail + 1, std::memory_order_release);
    }
};
