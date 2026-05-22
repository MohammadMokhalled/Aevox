#pragma once
// src/log/format.hpp
//
// INTERNAL — dependency-free log formatters.

#include <string>
#include <string_view>

#include "log/log_record.hpp"

namespace aevox::detail {

[[nodiscard]] std::string escape_json(std::string_view value);

[[nodiscard]] std::string format_json(const LogRecord& record);

[[nodiscard]] std::string format_pretty(const LogRecord& record);

[[nodiscard]] std::string to_string(LogLevel level);

} // namespace aevox::detail
