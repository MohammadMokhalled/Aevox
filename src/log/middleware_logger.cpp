// src/log/middleware_logger.cpp
//
// INTERNAL — aevox::middleware::logger() factory implementation.
//
// Design: Tasks/architecture/AEV-011-arch.md §3.2

#include <aevox/log.hpp>
#include <aevox/middleware/logger.hpp>

#include <chrono>
#include <format>
#include <string>

#include "json_escape.hpp"

namespace aevox::middleware {

namespace {

[[nodiscard]] std::string format_middleware_message(const Request& req, const Response& resp,
                                                    std::chrono::milliseconds duration_ms,
                                                    LogFormat                 format)
{
    if (format == LogFormat::JSON) {
        return std::format(R"({{"method":"{}","path":"{}","status":{},"duration_ms":{}}}")",
                           json_escape(to_string(req.method())), json_escape(req.path()),
                           resp.status_code(), duration_ms.count());
    }

    // Pretty format
    return std::format("{} {} {} {}ms", std::string{to_string(req.method())},
                       std::string{req.path()}, resp.status_code(), duration_ms.count());
}

} // namespace

Middleware logger(LoggerMiddlewareConfig config)
{
    return Middleware{[config = std::move(config)](Request& req, auto next) -> Task<Response> {
        // Fast-path: excluded paths produce no log lines.
        if (!config.exclude_paths.empty() && config.exclude_paths.contains(std::string{req.path()}))
        {
            co_return co_await next(req);
        }

        const auto start       = std::chrono::steady_clock::now();
        auto       response    = co_await next(req);
        const auto duration    = std::chrono::steady_clock::now() - start;
        const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);

        const std::string msg =
            format_middleware_message(req, response, duration_ms, config.format);
        req.log.log(config.level, msg);

        if (duration_ms > config.slow_request_threshold) {
            req.log.warn("Slow request: {} {} took {}ms", std::string{to_string(req.method())},
                         std::string{req.path()}, duration_ms.count());
        }

        co_return response;
    }};
}

} // namespace aevox::middleware
