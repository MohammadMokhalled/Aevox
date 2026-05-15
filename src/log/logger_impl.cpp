// src/log/logger_impl.cpp
//
// INTERNAL — Out-of-line Logger method implementations and global() factory.
//
// Design: Tasks/architecture/AEV-011-arch.md §4.1, §4.6

#include <aevox/log.hpp>

#include "async_writer.hpp"

namespace aevox {

// =============================================================================
// Logger — private constructor
// =============================================================================

Logger::Logger(AsyncLogWriter* writer, RequestContext* ctx) noexcept
    : writer_{writer}, context_{ctx}
{}

// =============================================================================
// Logger — non-template log entry point
// =============================================================================

void Logger::log(LogLevel level, std::string_view message) noexcept
{
    if (writer_ != nullptr) {
        writer_->push(level, message, context_);
    }
}

void Logger::set_writer(AsyncLogWriter* writer) noexcept
{
    writer_ = writer;
}

// =============================================================================
// Global logger
// =============================================================================

namespace log {

Logger& global() noexcept
{
    static Logger instance;
    return instance;
}

} // namespace log

} // namespace aevox
