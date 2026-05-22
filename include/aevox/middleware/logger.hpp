#pragma once
// include/aevox/middleware/logger.hpp
//
// Public API for Aevox's automatic request/response access-log middleware.

#include <aevox/log.hpp>
#include <aevox/middleware.hpp>

#include <chrono>
#include <string>
#include <unordered_set>

namespace aevox::middleware {

/**
 * @brief Default threshold used to classify an HTTP request as slow.
 *
 * The logger middleware emits slow requests at `LogLevel::Warn` when
 * `LoggerMiddlewareConfig::log_slow_requests` is enabled.
 *
 * @note Thread-safety: compile-time constant with no shared mutable state.
 */
inline constexpr std::chrono::milliseconds kDefaultSlowRequestThreshold{
    std::chrono::milliseconds{500}};

/**
 * @brief Configuration for automatic request/response access logging.
 *
 * The middleware emits one log entry after the downstream handler returns. It
 * uses the global logging configuration for destination and format; this config
 * only controls middleware behavior.
 *
 * @note Thread-safety: copied into the middleware object at registration time
 *       and then read concurrently. Do not mutate a registered middleware object
 *       through captured references.
 * @note Move semantics: movable and copyable; moved-from containers follow
 *       standard library rules.
 */
struct LoggerMiddlewareConfig
{
    LogLevel level{LogLevel::Info};                  ///< Severity used for normal access logs.
    std::unordered_set<std::string> exclude_paths{}; ///< Exact paths that produce no log entry.
    std::chrono::milliseconds       slow_request_threshold{kDefaultSlowRequestThreshold};
    bool log_slow_requests{true}; ///< Emits slow requests at Warn when the threshold is exceeded.
};

/**
 * @brief Creates middleware that emits one correlated access log line per request.
 *
 * The middleware records start time, awaits the next middleware/handler, then
 * logs method, path, status, duration, request id, and trace fields through
 * `aevox::log::write(request, ...)`.
 *
 * @param config Middleware behavior configuration.
 * @return Move-only middleware object suitable for `App::use()`.
 * @note Thread-safety: safe for concurrent request invocation after registration.
 */
[[nodiscard]] Middleware logger(LoggerMiddlewareConfig config = {});

} // namespace aevox::middleware
