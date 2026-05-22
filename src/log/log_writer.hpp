#pragma once
// src/log/log_writer.hpp
//
// INTERNAL — bounded asynchronous writer and global writer installation helpers.

#include <aevox/log.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <expected>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string_view>
#include <thread>

#include "log/log_record.hpp"

namespace aevox {
class Request;
} // namespace aevox

namespace aevox::detail {

class LogWriter
{
public:
    [[nodiscard]] static std::expected<std::shared_ptr<LogWriter>, LogError> create(
        LogConfig config) noexcept;

    ~LogWriter();

    LogWriter(const LogWriter&)            = delete;
    LogWriter& operator=(const LogWriter&) = delete;
    LogWriter(LogWriter&&)                 = delete;
    LogWriter& operator=(LogWriter&&)      = delete;

    void enqueue(LogRecord&& record) noexcept;

    void record_drop() noexcept;

    [[nodiscard]] std::expected<void, LogError> flush() noexcept;

    [[nodiscard]] LogStats stats() const noexcept;

    [[nodiscard]] bool accepts(LogLevel level) const noexcept;

    explicit LogWriter(LogConfig config);

private:
    [[nodiscard]] std::optional<LogError> open_destination() noexcept;
    void                                  run(std::stop_token token) noexcept;
    void                                  write_record(const LogRecord& record) noexcept;
    void                                  increment_dropped() noexcept;

    LogConfig               config_;
    std::deque<LogRecord>   queue_;
    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    std::ofstream           file_;
    std::ostream*           output_{nullptr};
    bool                    writing_{false};
    std::atomic_uint64_t    accepted_{0};
    std::atomic_uint64_t    dropped_{0};
    std::atomic_uint64_t    written_{0};
    std::jthread            thread_;
};

void install_log_writer(std::shared_ptr<LogWriter> writer) noexcept;

void reset_log_writer() noexcept;

[[nodiscard]] std::shared_ptr<LogWriter> current_log_writer() noexcept;

void write_request_log(const Request& request, LogLevel level, std::string_view message,
                       std::optional<int>                       status_code = std::nullopt,
                       std::optional<std::chrono::microseconds> duration = std::nullopt) noexcept;

} // namespace aevox::detail
