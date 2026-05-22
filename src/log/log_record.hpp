#pragma once
// src/log/log_record.hpp
//
// INTERNAL — stable in-memory payload passed from request/global logging calls
// to the background writer.

#include <aevox/log.hpp>

#include <chrono>
#include <optional>
#include <string>

namespace aevox::detail {

struct LogRequestFields
{
    std::string request_id;
    std::string method;
    std::string path;
    std::string trace_id;
    std::string span_id;
};

struct LogRecord
{
    std::chrono::system_clock::time_point    timestamp;
    LogLevel                                 level{LogLevel::Info};
    std::string                              message;
    std::optional<LogRequestFields>          request;
    std::optional<int>                       status_code;
    std::optional<std::chrono::microseconds> duration;
};

} // namespace aevox::detail
