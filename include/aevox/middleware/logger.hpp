#pragma once
// include/aevox/middleware/logger.hpp
//
// Public API for the automatic request/response logger middleware.
//
// Provides a factory function that returns a Middleware which logs every
// request/response with configurable fields, format, and exclusion rules.
//
// Design: Tasks/architecture/AEV-011-arch.md §3.2

#include <aevox/log.hpp>
#include <aevox/middleware.hpp>

#include <chrono>
#include <string>
#include <unordered_set>
#include <vector>

namespace aevox::middleware {

/**
 * @brief Configuration for the automatic request/response logger middleware.
 */
struct LoggerMiddlewareConfig
{
    LogFormat             format{LogFormat::JSON};
    LogLevel              level{LogLevel::Info};
    std::vector<LogField> include{
        LogField::Timestamp, LogField::Level,  LogField::RequestId,  LogField::Method,
        LogField::Path,      LogField::Status, LogField::DurationMs,
    };
    std::unordered_set<std::string> exclude_paths{};
    std::chrono::milliseconds       slow_request_threshold{500};
};

/**
 * @brief Factory that returns a Middleware which logs every request/response.
 *
 * The middleware measures duration (from entry to response return), extracts
 * the configured fields, and emits one structured log line per request via
 * the global async logger.
 *
 * @param config  Field selection, format, and exclusion rules.
 * @return A Middleware satisfying the `MiddlewareFn` concept.
 *
 * @note The middleware short-circuits (emits nothing) for paths in
 *       `exclude_paths`. This is a fast `unordered_set` lookup before any
 *       timer or field extraction.
 */
[[nodiscard]] Middleware logger(LoggerMiddlewareConfig config = {});

} // namespace aevox::middleware
