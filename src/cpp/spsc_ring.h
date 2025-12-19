#pragma once

#include <array>
#include <atomic>
#include <cstdint>

template <typename T, uint32_t N>
class SpscRing {

    // cache align
    alignas(64) std::array<T, N> buf_{};
    alignas(64) std::atomic<uint32_t> head_{0};
    alignas(64) std::atomic<uint32_t> tail_{0};

    public:

    inline bool try_push(const T& v) {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail == N) {return false;}
        buf_[head & (N - 1)] = v;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    inline bool try_pop(T& out) {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        const uint32_t head = head_.load(std::memory_order_acquire);
        if (tail == head) {return false;}
        out = buf_[tail & (N - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
};
