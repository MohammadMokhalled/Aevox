// src/log/async_writer.cpp
//
// INTERNAL — AsyncLogWriter implementation: drain loop, push, flush.
//
// Design: Tasks/architecture/AEV-011-arch.md §4.3

#include "async_writer.hpp"

#include <iostream>
#include <thread>

#include "log_backend.hpp"
#include "request_context.hpp"
#include "spdlog_backend.hpp"

namespace aevox {

AsyncLogWriter::AsyncLogWriter(LogConfig config)
    : queue_{std::make_unique<LockFreeQueue>(config.ring_buffer_entries)},
      backend_{make_log_backend(config)}, config_{std::move(config)}
{
    drain_thread_ = std::jthread([this](const std::stop_token& st) {
        // Use stop_flag_ for the main loop condition; stop_token is ignored
        // because we manage shutdown explicitly in the destructor.
        (void)st;
        drain_loop();
    });
}

AsyncLogWriter::~AsyncLogWriter()
{
    stop_flag_.store(true, std::memory_order_relaxed);
    if (drain_thread_.joinable()) {
        drain_thread_.join();
    }
    // Final drain of any entries pushed during join.
    LogEntry entry;
    while (queue_->try_pop(entry)) {
        if (backend_) {
            try {
                backend_->write(entry);
            }
            catch (...) {
                std::clog << "[aevox] backend write error during shutdown — entry dropped\n";
            }
        }
    }
    if (backend_) {
        try {
            backend_->flush();
        }
        catch (...) {
            std::clog << "[aevox] backend flush error during shutdown\n";
        }
    }
}

AsyncLogWriter::AsyncLogWriter(AsyncLogWriter&& other) noexcept
    : queue_{std::move(other.queue_)}, backend_{std::move(other.backend_)},
      drain_thread_{std::move(other.drain_thread_)}, config_{std::move(other.config_)}
{
    stop_flag_.store(other.stop_flag_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    other.stop_flag_.store(false, std::memory_order_relaxed);
}

AsyncLogWriter& AsyncLogWriter::operator=(AsyncLogWriter&& other) noexcept
{
    if (this != &other) {
        queue_        = std::move(other.queue_);
        backend_      = std::move(other.backend_);
        drain_thread_ = std::move(other.drain_thread_);
        config_       = std::move(other.config_);
        stop_flag_.store(other.stop_flag_.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
        other.stop_flag_.store(false, std::memory_order_relaxed);
    }
    return *this;
}

void AsyncLogWriter::push(LogLevel level, std::string_view message,
                          const RequestContext* ctx) noexcept
{
    try {
        if (static_cast<std::uint8_t>(level) < static_cast<std::uint8_t>(config_.level))
            return;

        LogEntry entry;
        entry.level = level;
        entry.set_message(message);
        if (ctx) {
            entry.request_id     = ctx->request_id;
            entry.correlation_id = ctx->correlation_id;
            entry.thread_id      = ctx->thread_id;
        }
        entry.timestamp = std::chrono::system_clock::now();

        (void)queue_->try_push(entry);
    }
    catch (...) {
        std::clog << "[aevox] log push failed — entry dropped\n";
    }
}

void AsyncLogWriter::flush() noexcept
{
    try {
        if (backend_)
            backend_->flush();
    }
    catch (...) {
        std::clog << "[aevox] backend flush failed\n";
    }
}

void AsyncLogWriter::install_as_global() noexcept
{
    aevox::log::global().set_writer(this);
}

Logger AsyncLogWriter::make_logger(RequestContext* ctx) noexcept
{
    return Logger(this, ctx);
}

void AsyncLogWriter::drain_loop()
{
    constexpr std::size_t kBatchSize = 256;

    while (!stop_flag_.load(std::memory_order_relaxed)) {
        bool     had_entry = false;
        LogEntry entry;
        for (std::size_t i = 0; i < kBatchSize; ++i) {
            if (!queue_->try_pop(entry))
                break;
            if (backend_) {
                try {
                    backend_->write(entry);
                }
                catch (...) {
                    std::clog << "[aevox] backend write error — entry dropped\n";
                }
            }
            had_entry = true;
        }
        if (!had_entry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Drain remaining entries on stop.
    LogEntry entry;
    while (queue_->try_pop(entry)) {
        if (backend_) {
            try {
                backend_->write(entry);
            }
            catch (...) {
                std::clog << "[aevox] backend write error — entry dropped\n";
            }
        }
    }
    if (backend_) {
        try {
            backend_->flush();
        }
        catch (...) {
            std::clog << "[aevox] backend flush error\n";
        }
    }
}

} // namespace aevox
