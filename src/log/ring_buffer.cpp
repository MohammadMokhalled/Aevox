// src/log/ring_buffer.cpp
//
// INTERNAL — LockFreeQueue implementation.
//
// Design: Tasks/architecture/AEV-011-arch.md §4.2

#include "ring_buffer.hpp"

#include <bit>

namespace aevox {

namespace {

[[nodiscard]] std::size_t next_power_of_two(std::size_t n) noexcept
{
    if (n == 0)
        return 1;
    if (std::has_single_bit(n))
        return n;
    return std::bit_ceil(n);
}

} // namespace

LockFreeQueue::LockFreeQueue(std::size_t capacity) : capacity_mask_{next_power_of_two(capacity) - 1}
{
    const std::size_t real_capacity = capacity_mask_ + 1;
    buffer_.resize(real_capacity);
    for (std::size_t i = 0; i < real_capacity; ++i) {
        buffer_[i].seq.store(i, std::memory_order_relaxed);
    }
}

LockFreeQueue::~LockFreeQueue() = default;

bool LockFreeQueue::try_push(LogEntry entry) noexcept
{
    std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
    while (true) {
        const std::size_t tail = dequeue_pos_.load(std::memory_order_acquire);
        if (pos - tail > capacity_mask_) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed,
                                               std::memory_order_relaxed))
        {
            break;
        }
    }

    Cell* cell = &buffer_[pos & capacity_mask_];
    while (cell->seq.load(std::memory_order_relaxed) != pos) {
        // spin — consumer has not finished reading this slot yet
    }

    cell->entry = std::move(entry);
    cell->seq.store(pos + 1, std::memory_order_release);
    return true;
}

bool LockFreeQueue::try_pop(LogEntry& entry) noexcept
{
    const auto head = dequeue_pos_.load(std::memory_order_relaxed);
    const auto tail = enqueue_pos_.load(std::memory_order_acquire);
    if (head >= tail) {
        return false;
    }

    Cell* cell = &buffer_[head & capacity_mask_];
    while (cell->seq.load(std::memory_order_relaxed) != head + 1) {
        // spin — producer has not finished writing this slot yet
    }

    entry = std::move(cell->entry);
    cell->seq.store(head + capacity_mask_ + 1, std::memory_order_release);
    dequeue_pos_.store(head + 1, std::memory_order_release);
    return true;
}

std::size_t LockFreeQueue::dropped_count() const noexcept
{
    return dropped_.load(std::memory_order_relaxed);
}

} // namespace aevox
