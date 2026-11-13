#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace adaptive {

/**
 * Fixed-capacity, lock-free, single-producer single-consumer queue.
 *
 * One thread may push and one other thread may pop, concurrently, without
 * locks or allocation. T must be trivially copyable. Capacity must be a power
 * of two; one slot is never used, so the queue holds Capacity - 1 items.
 */
template<typename T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    /// Producer side. Returns false (and drops the item) if the queue is full.
    bool push(const T& item) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & (Capacity - 1);
        if (next == tail_.load(std::memory_order_acquire)) return false;
        items_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    /// Consumer side. Returns false if the queue is empty.
    bool pop(T& out) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = items_[tail];
        tail_.store((tail + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

private:
    T items_[Capacity] = {};
    alignas(64) std::atomic<std::size_t> head_{0};  // written by the producer
    alignas(64) std::atomic<std::size_t> tail_{0};  // written by the consumer
};

} // namespace adaptive
