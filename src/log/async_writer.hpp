#pragma once
// src/log/async_writer.hpp
//
// INTERNAL — AsyncLogWriter owns the ring buffer and drain thread.
//
// Design: Tasks/architecture/AEV-011-arch.md §4.3

#include <aevox/log.hpp>

#include <atomic>
#include <memory>
#include <thread>

#include "log_entry.hpp"
#include "request_context.hpp"
#include "ring_buffer.hpp"

namespace aevox {

/**
 * @brief Async log writer — owns the lock-free queue and background drain thread.
 *
 * The hot path (`push()`) formats the message and enqueues a LogEntry.
 * The drain thread dequeues batches and dispatches to the LogBackend.
 */
class AsyncLogWriter
{
public:
    explicit AsyncLogWriter(LogConfig config);
    ~AsyncLogWriter();

    /**
     * @brief Hot path — called from request handlers on any thread.
     */
    void push(LogLevel level, std::string_view message,
              const RequestContext* ctx = nullptr) noexcept;

    /**
     * @brief Flush all pending entries and block until written.
     */
    void flush() noexcept;

    /**
     * @brief Replace the no-op global logger with this writer.
     */
    void install_as_global() noexcept;

    /**
     * @brief Construct a Logger bound to this writer and optional context.
     *
     * Used by the connection handler and by tests / benchmarks.
     */
    [[nodiscard]] Logger make_logger(RequestContext* ctx = nullptr) noexcept;

    // Non-copyable, move-only.
    AsyncLogWriter(AsyncLogWriter&&) noexcept;
    AsyncLogWriter& operator=(AsyncLogWriter&&) noexcept;

    AsyncLogWriter(const AsyncLogWriter&)            = delete;
    AsyncLogWriter& operator=(const AsyncLogWriter&) = delete;

private:
    std::unique_ptr<LockFreeQueue>       queue_;
    std::unique_ptr<class SpdlogBackend> backend_;
    std::jthread                         drain_thread_;
    std::atomic<bool>                    stop_flag_{false};
    LogConfig                            config_;

    void drain_loop();
};

} // namespace aevox
