#include "log/log_writer.hpp"

#include <aevox/log.hpp>

#include <atomic>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>

#include "log/format.hpp"
#include "log/log_record.hpp"

namespace aevox::detail {

namespace {

[[nodiscard]] std::uint32_t normalized_capacity(std::uint32_t capacity) noexcept
{
    return capacity == 0U ? 1U : capacity;
}

} // namespace

std::expected<std::shared_ptr<LogWriter>, LogError> LogWriter::create(LogConfig config) noexcept
{
    try {
        auto writer = std::make_shared<LogWriter>(std::move(config));
        if (auto error = writer->open_destination(); error) {
            return std::unexpected(*error);
        }
        writer->thread_ =
            std::jthread{[writer](std::stop_token token) { writer->run(std::move(token)); }};
        return writer;
    }
    catch (...) {
        return std::unexpected(LogError::FileOpenFailed);
    }
}

LogWriter::LogWriter(LogConfig config) : config_{std::move(config)}
{
    config_.queue_capacity = normalized_capacity(config_.queue_capacity);
}

LogWriter::~LogWriter()
{
    if (thread_.joinable()) {
        thread_.request_stop();
        cv_.notify_all();
    }
}

void LogWriter::enqueue(LogRecord&& record) noexcept
{
    if (!accepts(record.level)) {
        increment_dropped();
        return;
    }

    std::unique_lock lock{mutex_, std::try_to_lock};
    if (!lock.owns_lock()) {
        increment_dropped();
        return;
    }

    if (queue_.size() >= config_.queue_capacity) {
        increment_dropped();
        return;
    }

    queue_.push_back(std::move(record));
    accepted_.fetch_add(1U, std::memory_order_relaxed);
    lock.unlock();
    cv_.notify_one();
}

void LogWriter::record_drop() noexcept
{
    increment_dropped();
}

std::expected<void, LogError> LogWriter::flush() noexcept
{
    try {
        std::unique_lock lock{mutex_};
        cv_.wait(lock, [this] { return queue_.empty() && !writing_; });
        if (output_ != nullptr) {
            output_->flush();
            if (!*output_) {
                return std::unexpected(LogError::FlushFailed);
            }
        }
        return {};
    }
    catch (...) {
        return std::unexpected(LogError::FlushFailed);
    }
}

LogStats LogWriter::stats() const noexcept
{
    return LogStats{
        .accepted = accepted_.load(std::memory_order_relaxed),
        .dropped  = dropped_.load(std::memory_order_relaxed),
        .written  = written_.load(std::memory_order_relaxed),
    };
}

bool LogWriter::accepts(LogLevel level) const noexcept
{
    return config_.enabled && config_.destination != LogDestination::Disabled &&
           static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(config_.level);
}

std::optional<LogError> LogWriter::open_destination() noexcept
{
    switch (config_.destination) {
        case LogDestination::Stdout:
            output_ = &std::cout;
            return std::nullopt;
        case LogDestination::Stderr:
            output_ = &std::cerr;
            return std::nullopt;
        case LogDestination::Disabled:
            output_ = nullptr;
            return std::nullopt;
        case LogDestination::File:
            if (!config_.file_path || config_.file_path->empty()) {
                return LogError::FilePathRequired;
            }
            file_.open(*config_.file_path, std::ios::out | std::ios::app);
            if (!file_.is_open()) {
                return LogError::FileOpenFailed;
            }
            output_ = &file_;
            return std::nullopt;
    }
    return LogError::FileOpenFailed;
}

void LogWriter::run(std::stop_token token) noexcept
{
    for (;;) {
        LogRecord record;

        {
            std::unique_lock lock{mutex_};
            cv_.wait(lock, [this, &token] { return token.stop_requested() || !queue_.empty(); });

            if (queue_.empty() && token.stop_requested()) {
                cv_.notify_all();
                return;
            }

            record = std::move(queue_.front());
            queue_.pop_front();
            writing_ = true;
        }

        write_record(record);

        {
            const std::scoped_lock lock{mutex_};
            writing_ = false;
        }
        cv_.notify_all();
    }
}

void LogWriter::write_record(const LogRecord& record) noexcept
{
    if (output_ == nullptr) {
        return;
    }

    try {
        if (config_.format == LogFormat::Json) {
            *output_ << format_json(record) << '\n';
        }
        else {
            *output_ << format_pretty(record) << '\n';
        }
        if (*output_) {
            written_.fetch_add(1U, std::memory_order_relaxed);
        }
    }
    catch (...) {
        increment_dropped();
    }
}

void LogWriter::increment_dropped() noexcept
{
    if (dropped_.load(std::memory_order_relaxed) != std::numeric_limits<std::uint64_t>::max()) {
        dropped_.fetch_add(1U, std::memory_order_relaxed);
    }
}

} // namespace aevox::detail
