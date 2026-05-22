#include "log/format.hpp"

#include <aevox/log.hpp>

#include <chrono>
#include <ctime>
#include <format>
#include <string>
#include <string_view>

#include "log/log_record.hpp"

namespace aevox::detail {

namespace {

constexpr int          kTmYearOffset{1900};
constexpr unsigned int kJsonControlCharacterLimit{0x20};
constexpr std::size_t  kJsonReserveOverhead{192UZ};
constexpr std::size_t  kPrettyReserveOverhead{160UZ};

[[nodiscard]] std::string format_timestamp(std::chrono::system_clock::time_point timestamp)
{
    const auto time_since_epoch = timestamp.time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_since_epoch);
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(time_since_epoch - seconds).count();

    const std::time_t raw_time = std::chrono::system_clock::to_time_t(timestamp);
    std::tm           utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &raw_time);
#else
    gmtime_r(&raw_time, &utc);
#endif

    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:06}Z", utc.tm_year + kTmYearOffset,
                       utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec, micros);
}

void append_json_field(std::string& out, std::string_view key, std::string_view value)
{
    out += ",\"";
    out += key;
    out += "\":\"";
    out += escape_json(value);
    out += '"';
}

} // namespace

std::string to_string(LogLevel level)
{
    switch (level) {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
    }
    return "UNKNOWN";
}

std::string escape_json(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (ch < kJsonControlCharacterLimit) {
                    out += std::format("\\u{:04x}", static_cast<unsigned int>(ch));
                }
                else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

std::string format_json(const LogRecord& record)
{
    std::string out;
    out.reserve(record.message.size() + kJsonReserveOverhead);
    out += R"({"timestamp":")";
    out += format_timestamp(record.timestamp);
    out += R"(","level":")";
    out += to_string(record.level);
    out += '"';

    if (record.request) {
        append_json_field(out, "request_id", record.request->request_id);
        append_json_field(out, "method", record.request->method);
        append_json_field(out, "path", record.request->path);
        if (record.status_code) {
            out += std::format(",\"status\":{}", *record.status_code);
        }
        if (record.duration) {
            out += std::format(",\"duration_us\":{}", record.duration->count());
        }
        if (!record.request->trace_id.empty()) {
            append_json_field(out, "trace_id", record.request->trace_id);
        }
        if (!record.request->span_id.empty()) {
            append_json_field(out, "span_id", record.request->span_id);
        }
    }

    append_json_field(out, "message", record.message);
    out += '}';
    return out;
}

std::string format_pretty(const LogRecord& record)
{
    std::string out;
    out.reserve(record.message.size() + kPrettyReserveOverhead);
    out += format_timestamp(record.timestamp);
    out += ' ';
    out += to_string(record.level);

    if (record.request) {
        out += " req=";
        out += record.request->request_id;
        out += ' ';
        out += record.request->method;
        out += ' ';
        out += record.request->path;
        if (record.status_code) {
            out += std::format(" status={}", *record.status_code);
        }
        if (record.duration) {
            out += std::format(" dur_us={}", record.duration->count());
        }
    }

    out += ' ';
    out += record.message;
    return out;
}

} // namespace aevox::detail
