#include <aevox/log.hpp>
#include <aevox/middleware.hpp>
#include <aevox/middleware/logger.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/task.hpp>

#include <chrono>
#include <format>
#include <functional>
#include <string>
#include <utility>

#include "log/log_writer.hpp"

namespace aevox::middleware {

Middleware logger(LoggerMiddlewareConfig config)
{
    return Middleware{
        [config = std::move(config)](
            aevox::Request&                                                        req,
            std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next) mutable
            -> aevox::Task<aevox::Response> {
            if (config.exclude_paths.contains(std::string{req.path()})) {
                co_return co_await next(req);
            }

            const auto start    = std::chrono::steady_clock::now();
            auto       response = co_await next(req);
            const auto elapsed  = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start);

            auto level = config.level;
            if (config.log_slow_requests && elapsed >= config.slow_request_threshold) {
                level = aevox::LogLevel::Warn;
            }

            const auto message = std::format("{} {} -> {} in {}us", aevox::to_string(req.method()),
                                             req.path(), response.status_code(), elapsed.count());

            aevox::detail::write_request_log(req, level, message, response.status_code(), elapsed);
            co_return response;
        }};
}

} // namespace aevox::middleware
