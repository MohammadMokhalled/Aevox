#include <aevox/error.hpp>
#include <aevox/log.hpp>
#include <aevox/request.hpp>

#include <atomic>
#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "http/request_impl.hpp"
#include "log/log_record.hpp"
#include "log/log_writer.hpp"

namespace aevox {

std::string_view to_string(LogError error) noexcept
{
    switch (error) {
        case LogError::FilePathRequired:
            return "file path required for file log destination";
        case LogError::FileOpenFailed:
            return "failed to open log file";
        case LogError::FlushFailed:
            return "failed to flush log destination";
    }
    return "unknown log error";
}

ErrorCategory category(LogError error) noexcept
{
    switch (error) {
        case LogError::FilePathRequired:
            return ErrorCategory::Validation;
        case LogError::FileOpenFailed:
        case LogError::FlushFailed:
            return ErrorCategory::Io;
    }
    return ErrorCategory::Unknown;
}

namespace detail {

namespace {

[[nodiscard]] std::atomic<std::shared_ptr<LogWriter>>& writer_slot() noexcept
{
    static std::atomic<std::shared_ptr<LogWriter>> writer;
    return writer;
}

[[nodiscard]] std::optional<LogRequestFields> request_fields(const Request& request)
{
    auto impl = get_request_impl(request);
    if (!impl) {
        return std::nullopt;
    }

    const auto& ctx = impl->get();
    return LogRequestFields{
        .request_id = std::string{ctx.request_id()},
        .method     = std::string{to_string(request.method())},
        .path       = std::string{request.path()},
        .trace_id   = std::string{ctx.trace_id()},
        .span_id    = std::string{ctx.span_id()},
    };
}

} // namespace

void install_log_writer(std::shared_ptr<LogWriter> writer) noexcept
{
    writer_slot().store(std::move(writer));
}

void reset_log_writer() noexcept
{
    writer_slot().store(std::shared_ptr<LogWriter>{});
}

std::shared_ptr<LogWriter> current_log_writer() noexcept
{
    return writer_slot().load();
}

void write_request_log(const Request& request, LogLevel level, std::string_view message,
                       std::optional<int>                       status_code,
                       std::optional<std::chrono::microseconds> duration) noexcept
{
    auto writer = current_log_writer();
    if (!writer || !writer->accepts(level)) {
        if (writer) {
            writer->record_drop();
        }
        return;
    }

    try {
        writer->enqueue(LogRecord{
            .timestamp   = std::chrono::system_clock::now(),
            .level       = level,
            .message     = std::string{message},
            .request     = request_fields(request),
            .status_code = status_code,
            .duration    = duration,
        });
    }
    catch (...) {
        writer->record_drop();
    }
}

} // namespace detail

namespace log {

void write(LogLevel level, std::string_view message) noexcept
{
    auto writer = detail::current_log_writer();
    if (!writer || !writer->accepts(level)) {
        if (writer) {
            writer->record_drop();
        }
        return;
    }

    try {
        writer->enqueue(detail::LogRecord{.timestamp   = std::chrono::system_clock::now(),
                                          .level       = level,
                                          .message     = std::string{message},
                                          .request     = std::nullopt,
                                          .status_code = std::nullopt,
                                          .duration    = std::nullopt});
    }
    catch (...) {
        writer->record_drop();
    }
}

void write(const Request& request, LogLevel level, std::string_view message) noexcept
{
    detail::write_request_log(request, level, message);
}

std::expected<void, LogError> flush() noexcept
{
    auto writer = detail::current_log_writer();
    if (!writer) {
        return {};
    }
    return writer->flush();
}

LogStats stats() noexcept
{
    auto writer = detail::current_log_writer();
    if (!writer) {
        return {};
    }
    return writer->stats();
}

} // namespace log

} // namespace aevox
