// src/log/spdlog_backend.cpp
//
// INTERNAL — SpdlogBackend implementation.
//
// This is the ONLY file in the entire codebase permitted to include
// <spdlog/spdlog.h> and its subsidiary headers.
//
// Design: Tasks/architecture/AEV-011-arch.md §4.5

#include "spdlog_backend.hpp"

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <ctime>
#include <format>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "json_escape.hpp"
#include "log_entry.hpp"

namespace aevox {

namespace {

// No custom formatter classes needed. We use spdlog's built-in pattern
// "%v" (raw message payload) so that each sink outputs the pre-formatted
// payload produced by SpdlogBackend::write().

// ---------------------------------------------------------------------------
// Sink construction
// ---------------------------------------------------------------------------

struct SinkBuildResult
{
    std::vector<spdlog::sink_ptr> sinks;
    std::vector<LogFormat>        formats;
};

[[nodiscard]] SinkBuildResult build_sinks(const LogConfig& config)
{
    SinkBuildResult result;
    for (const auto& sink_variant : config.sinks) {
        std::visit(
            [&result](const auto& cfg) {
                using T = std::decay_t<decltype(cfg)>;
                if constexpr (std::same_as<T, ConsoleSinkConfig>) {
                    std::shared_ptr<spdlog::sinks::sink> sink;
                    if (cfg.color) {
                        sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                    }
                    else {
                        sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
                    }
                    sink->set_pattern("%v");
                    result.sinks.push_back(std::move(sink));
                    result.formats.push_back(cfg.format);
                }
                else if constexpr (std::same_as<T, FileSinkConfig>) {
                    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                        cfg.path, static_cast<std::size_t>(cfg.rotate_mb) * 1024 * 1024,
                        cfg.keep_files, false);
                    sink->set_pattern("%v");
                    result.sinks.push_back(std::move(sink));
                    result.formats.push_back(cfg.format);
                }
            },
            sink_variant);
    }
    return result;
}

// ---------------------------------------------------------------------------
// ISO 8601 timestamp formatting
// ---------------------------------------------------------------------------

[[nodiscard]] std::tm utc_time(std::time_t value) noexcept
{
    std::tm result{};
#ifdef _WIN32
    gmtime_s(&result, &value);
#else
    gmtime_r(&value, &result);
#endif
    return result;
}

[[nodiscard]] std::string format_iso8601(std::chrono::system_clock::time_point tp)
{
    const auto time_t = std::chrono::system_clock::to_time_t(tp);
    const auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count() %
        1'000'000'000;

    const std::tm utc = utc_time(time_t);

    return std::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:09d}Z", utc.tm_year + 1900,
                       utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec, ns);
}

// ---------------------------------------------------------------------------
// LogEntry -> string helpers
// ---------------------------------------------------------------------------

[[nodiscard]] std::string log_level_to_string(LogLevel level) noexcept
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

[[nodiscard]] std::string format_json(const LogEntry& entry)
{
    std::string result = "{";
    result += std::format(R"("timestamp":"{}")", format_iso8601(entry.timestamp));
    result += std::format(R"(,"level":"{}")", log_level_to_string(entry.level));
    result += std::format(R"(,"message":"{}")", json_escape(entry.message()));
    if (!entry.request_id.empty()) {
        result += std::format(R"(,"request_id":"{}")", json_escape(entry.request_id));
    }
    if (entry.thread_id != 0) {
        result += std::format(R"(,"thread_id":{})", entry.thread_id);
    }
    if (!entry.trace_id.empty()) {
        result += std::format(R"(,"trace_id":"{}")", json_escape(entry.trace_id));
    }
    if (!entry.span_id.empty()) {
        result += std::format(R"(,"span_id":"{}")", json_escape(entry.span_id));
    }
    result += "}";
    return result;
}

[[nodiscard]] std::string format_pretty(const LogEntry& entry)
{
    std::string result = std::format("[{}] {} {}", format_iso8601(entry.timestamp),
                                     log_level_to_string(entry.level), entry.message());
    if (!entry.request_id.empty()) {
        result += std::format(" req={}", entry.request_id);
    }
    if (entry.thread_id != 0) {
        result += std::format(" tid={}", entry.thread_id);
    }
    if (!entry.trace_id.empty()) {
        result += std::format(" trace={}", entry.trace_id);
    }
    if (!entry.span_id.empty()) {
        result += std::format(" span={}", entry.span_id);
    }
    return result;
}

} // namespace

// =============================================================================
// SpdlogBackend::Impl
// =============================================================================

struct SpdlogBackend::Impl
{
    std::shared_ptr<spdlog::logger> logger;
    std::vector<LogFormat>          sink_formats;
};

// =============================================================================
// SpdlogBackend
// =============================================================================

SpdlogBackend::SpdlogBackend(const LogConfig& config) : impl_{std::make_unique<Impl>()}
{
    auto [sinks, formats] = build_sinks(config);
    if (sinks.empty()) {
        // Fallback to stdout if no sinks configured.
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sinks.push_back(std::move(sink));
        formats.push_back(LogFormat::Pretty);
    }

    impl_->logger       = std::make_shared<spdlog::logger>("aevox", sinks.begin(), sinks.end());
    impl_->sink_formats = std::move(formats);
    impl_->logger->set_level(spdlog::level::trace);
    impl_->logger->flush_on(spdlog::level::off);
}

SpdlogBackend::~SpdlogBackend() = default;

void SpdlogBackend::write(const LogEntry& entry)
{
    // spdlog level mapping
    spdlog::level::level_enum spd_level = spdlog::level::info;
    switch (entry.level) {
        case LogLevel::Trace:
            spd_level = spdlog::level::trace;
            break;
        case LogLevel::Debug:
            spd_level = spdlog::level::debug;
            break;
        case LogLevel::Info:
            spd_level = spdlog::level::info;
            break;
        case LogLevel::Warn:
            spd_level = spdlog::level::warn;
            break;
        case LogLevel::Error:
            spd_level = spdlog::level::err;
            break;
        case LogLevel::Fatal:
            spd_level = spdlog::level::critical;
            break;
    }

    // Pre-format both payloads once.
    const std::string json_payload   = format_json(entry);
    const std::string pretty_payload = format_pretty(entry);

    const auto& sinks = impl_->logger->sinks();
    for (std::size_t i = 0; i < sinks.size(); ++i) {
        if (i < impl_->sink_formats.size() && impl_->sink_formats[i] == LogFormat::Pretty) {
            sinks[i]->log(spdlog::details::log_msg{"aevox", spd_level, pretty_payload});
        }
        else {
            sinks[i]->log(spdlog::details::log_msg{"aevox", spd_level, json_payload});
        }
    }
}

void SpdlogBackend::flush()
{
    if (impl_->logger)
        impl_->logger->flush();
}

} // namespace aevox
