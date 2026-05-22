#pragma once
// src/log/ring_buffer.hpp
//
// INTERNAL — bounded MPMC lock-free queue for log entries.
//
// Algorithm: LMAX Disruptor / Dmitry Vyukov atomic-sequence bounded queue.
// Each cell has a sequence number. Producers claim with fetch_add, wait until
// seq == claim_idx, write, then set seq = claim_idx + 1. Consumer claims with
// fetch_add, waits until seq == claim_idx + 1, reads, then sets
// seq = claim_idx + capacity.
//
// Design: Tasks/architecture/AEV-011-arch.md §4.2

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include "log_entry.hpp"

namespace aevox {

inline constexpr std::size_t kCacheLineBytes{64};

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4324)
#endif

/**
 * @brief Bounded multi-producer single-consumer lock-free queue.
 *
 * Thread-safe for any number of concurrent producers and **exactly one**
 * consumer. The consumer is the background drain thread in AsyncLogWriter.
 * Calling try_pop from more than one thread simultaneously is undefined behaviour.
 */
class LockFreeQueue
{
public:
    explicit LockFreeQueue(std::size_t capacity);
    ~LockFreeQueue();

    /**
     * @brief Attempts to push an entry into the queue.
     *
     * @return `true` if the entry was enqueued, `false` if the queue is full.
     */
    [[nodiscard]] bool try_push(LogEntry entry) noexcept;

    /**
     * @brief Attempts to pop an entry from the queue.
     *
     * @param out  Output parameter; populated on success.
     * @return `true` if an entry was dequeued, `false` if the queue is empty.
     */
    [[nodiscard]] bool try_pop(LogEntry& out) noexcept;

    /**
     * @brief Returns the number of entries dropped since construction.
     *
     * A drop occurs when try_push fails because the queue is full.
     */
    [[nodiscard]] std::size_t dropped_count() const noexcept;

    // Non-copyable, non-movable.
    LockFreeQueue(const LockFreeQueue&)            = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    LockFreeQueue(LockFreeQueue&&)                 = delete;
    LockFreeQueue& operator=(LockFreeQueue&&)      = delete;

private:
    class Cell
    {
    private:
        alignas(kCacheLineBytes) std::atomic<std::size_t> seq_{0};
        LogEntry entry_;

    public:
        Cell()  = default;
        ~Cell() = default;

        Cell(const Cell&)            = delete;
        Cell& operator=(const Cell&) = delete;

        Cell(Cell&& other) noexcept
            : seq_(other.seq_.load(std::memory_order_relaxed)), entry_(std::move(other.entry_))
        {}

        Cell& operator=(Cell&& other) noexcept
        {
            seq_.store(other.seq_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            entry_ = std::move(other.entry_);
            return *this;
        }

        void store_sequence(std::size_t value, std::memory_order order) noexcept
        {
            seq_.store(value, order);
        }

        [[nodiscard]] std::size_t sequence(std::memory_order order) const noexcept
        {
            return seq_.load(order);
        }

        void set_entry(LogEntry entry) noexcept
        {
            entry_ = std::move(entry);
        }

        [[nodiscard]] LogEntry take_entry() noexcept
        {
            return std::move(entry_);
        }
    };

    alignas(kCacheLineBytes) std::atomic<std::size_t> enqueue_pos_{0};
    std::vector<Cell> buffer_;
    std::size_t       capacity_mask_{0};
    alignas(kCacheLineBytes) std::atomic<std::size_t> dequeue_pos_{0};
    alignas(kCacheLineBytes) std::atomic<std::size_t> dropped_{0};
};

#ifdef _MSC_VER
    #pragma warning(pop)
#endif

} // namespace aevox
