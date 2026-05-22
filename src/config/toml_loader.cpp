// src/config/toml_loader.cpp
//
// Implements load_toml_config() using toml++.
//
// toml++ (tomlplusplus) is included ONLY in this translation unit. No toml++ type
// ever escapes to a public header. The public interface uses only aevox types.
//
// Unrecognised keys emit a warning to std::clog (the framework's internal log
// layer has no implementation yet; std::clog is the appropriate fallback).
//
// Validation order matches AEV-025-arch.md §4.6: first-failure returns immediately.

#include "toml_loader.hpp"

#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/log.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <toml++/impl/parse_error.hpp>
#include <toml++/impl/parser.hpp>
#include <toml++/impl/table.hpp>
#include <toml++/toml.hpp> // IWYU pragma: keep

namespace aevox::config {

namespace {

// Known top-level TOML key names. Used to detect unrecognised keys.
constexpr std::array<std::string_view, 9> kKnownTopLevelKeys{
    "port",           "host",     "backlog", "max_body_size", "request_timeout", "max_header_count",
    "max_read_bytes", "executor", "logging",
};

constexpr int64_t kMinPositiveValue{1};
constexpr int64_t kMaxPortValue{std::numeric_limits<std::uint16_t>::max()};
constexpr int64_t kMaxBodyBytes{2LL * 1024LL * 1024LL * 1024LL};
constexpr int64_t kMaxRequestTimeoutSeconds{3600};
constexpr int64_t kMaxHeaderCount{1000};
constexpr int64_t kMinReadBytes{512};
constexpr int64_t kMaxReadBytes{16LL * 1024LL * 1024LL};
constexpr int64_t kMaxExecutorThreadCount{1024};
constexpr int64_t kMaxExecutorCpuPoolThreads{256};
constexpr int64_t kMaxDrainTimeoutSeconds{3600};
constexpr int64_t kMinRingBufferEntries{64};
constexpr int64_t kMaxRingBufferEntries{1024LL * 1024LL};
constexpr int64_t kMaxLogRotateMb{4096};
constexpr int64_t kMaxLogKeepFiles{100};

// Known executor-section key names.
constexpr std::array<std::string_view, 3> kKnownExecutorKeys{
    "thread_count",
    "cpu_pool_threads",
    "drain_timeout",
};

// Known logging-section key names.
constexpr std::array<std::string_view, 3> kKnownLoggingKeys{
    "level",
    "ring_buffer_entries",
    "sinks",
};

bool is_known_key(std::string_view key, std::span<const std::string_view> known) noexcept
{
    for (const auto& k : known) {
        if (k == key)
            return true;
    }
    return false;
}

ConfigErrorDetail make_invalid(std::string_view key, std::string_view reason)
{
    return ConfigErrorDetail{ConfigError::InvalidValue,
                             std::format("invalid value for '{}': {}", key, reason),
                             std::string{key}};
}

} // namespace

[[nodiscard]] std::expected<AppConfig, ConfigErrorDetail> load_toml_config(std::string_view path,
                                                                           AppConfig base) noexcept
{
    // Check existence before parsing — toml++ throws parse_error for missing files,
    // which would be indistinguishable from a TOML syntax error without this check.
    if (!std::filesystem::exists(std::filesystem::path{path})) {
        return std::unexpected(ConfigErrorDetail{ConfigError::FileNotFound,
                                                 std::format("config file not found: '{}'", path)});
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    }
    catch (const toml::parse_error& e) {
        return std::unexpected(
            ConfigErrorDetail{ConfigError::ParseError,
                              std::format("TOML parse error in '{}': {}", path, e.description())});
    }
    catch (...) {
        return std::unexpected(
            ConfigErrorDetail{ConfigError::ParseError,
                              std::format("could not read config file '{}'", path)});
    }

    // Warn about unrecognised top-level keys.
    for (const auto& [key, val] : tbl) {
        if (!is_known_key(std::string_view{key}, kKnownTopLevelKeys)) {
            std::clog << std::format("[aevox] config warning: unrecognised key '{}' — ignored\n",
                                     std::string_view{key});
        }
        (void)val;
    }

    // ── port ──────────────────────────────────────────────────────────────────
    if (const auto* v = tbl.get("port")) {
        const auto raw = v->value<int64_t>();
        if (!raw || *raw < kMinPositiveValue || *raw > kMaxPortValue)
            return std::unexpected(make_invalid("port", "must be an integer in 1..65535"));
        base.port = static_cast<std::uint16_t>(*raw);
    }

    // ── host ──────────────────────────────────────────────────────────────────
    if (const auto* v = tbl.get("host")) {
        const auto raw = v->value<std::string>();
        if (!raw || raw->empty())
            return std::unexpected(make_invalid("host", "must be a non-empty string"));
        base.host = *raw;
    }

    // ── backlog ───────────────────────────────────────────────────────────────
    if (const auto* v = tbl.get("backlog")) {
        const auto raw = v->value<int64_t>();
        if (!raw || *raw < kMinPositiveValue || *raw > kMaxPortValue)
            return std::unexpected(make_invalid("backlog", "must be an integer in 1..65535"));
        base.backlog = static_cast<int>(*raw);
    }

    // ── max_body_size ─────────────────────────────────────────────────────────
    if (const auto* v = tbl.get("max_body_size")) {
        const auto raw = v->value<int64_t>();
        if (!raw || *raw < kMinPositiveValue || *raw > kMaxBodyBytes)
            return std::unexpected(
                make_invalid("max_body_size", "must be an integer in 1..2147483648"));
        base.max_body_size = static_cast<std::size_t>(*raw);
    }

    // ── request_timeout ───────────────────────────────────────────────────────
    if (const auto* v = tbl.get("request_timeout")) {
        const auto raw = v->value<int64_t>();
        if (!raw || *raw < kMinPositiveValue || *raw > kMaxRequestTimeoutSeconds)
            return std::unexpected(
                make_invalid("request_timeout", "must be an integer in 1..3600 (seconds)"));
        base.request_timeout = std::chrono::seconds{*raw};
    }

    // ── max_header_count ──────────────────────────────────────────────────────
    if (const auto* v = tbl.get("max_header_count")) {
        const auto raw = v->value<int64_t>();
        if (!raw || *raw < kMinPositiveValue || *raw > kMaxHeaderCount)
            return std::unexpected(
                make_invalid("max_header_count", "must be an integer in 1..1000"));
        base.max_header_count = static_cast<std::size_t>(*raw);
    }

    // ── max_read_bytes ────────────────────────────────────────────────────────
    if (const auto* v = tbl.get("max_read_bytes")) {
        const auto raw = v->value<int64_t>();
        if (!raw || *raw < kMinReadBytes || *raw > kMaxReadBytes)
            return std::unexpected(
                make_invalid("max_read_bytes", "must be an integer in 512..16777216"));
        base.max_read_bytes = static_cast<std::size_t>(*raw);
    }

    // ── [executor] section ────────────────────────────────────────────────────
    if (const auto* ex_node = tbl.get("executor")) {
        const auto* ex = ex_node->as_table();
        if (!ex)
            return std::unexpected(ConfigErrorDetail{ConfigError::InvalidValue,
                                                     "'executor' must be a TOML table section",
                                                     "executor"});

        // Warn about unrecognised executor keys.
        for (const auto& [key, val] : *ex) {
            if (!is_known_key(std::string_view{key}, kKnownExecutorKeys)) {
                std::clog << std::format(
                    "[aevox] config warning: unrecognised key 'executor.{}' — ignored\n",
                    std::string_view{key});
            }
            (void)val;
        }

        // executor.thread_count
        if (const auto* v = ex->get("thread_count")) {
            const auto raw = v->value<int64_t>();
            if (!raw || *raw < 0 || *raw > kMaxExecutorThreadCount)
                return std::unexpected(
                    make_invalid("executor.thread_count", "must be an integer in 0..1024"));
            base.executor.thread_count = static_cast<std::size_t>(*raw);
        }

        // executor.cpu_pool_threads
        if (const auto* v = ex->get("cpu_pool_threads")) {
            const auto raw = v->value<int64_t>();
            if (!raw || *raw < 0 || *raw > kMaxExecutorCpuPoolThreads)
                return std::unexpected(
                    make_invalid("executor.cpu_pool_threads", "must be an integer in 0..256"));
            base.executor.cpu_pool_threads = static_cast<std::size_t>(*raw);
        }

        // executor.drain_timeout
        if (const auto* v = ex->get("drain_timeout")) {
            const auto raw = v->value<int64_t>();
            if (!raw || *raw < kMinPositiveValue || *raw > kMaxDrainTimeoutSeconds)
                return std::unexpected(make_invalid("executor.drain_timeout",
                                                    "must be an integer in 1..3600 (seconds)"));
            base.executor.drain_timeout = std::chrono::seconds{*raw};
        }
    }

    // ── [logging] section ─────────────────────────────────────────────────────
    if (const auto* log_node = tbl.get("logging")) {
        const auto* log_tbl = log_node->as_table();
        if (!log_tbl)
            return std::unexpected(ConfigErrorDetail{ConfigError::InvalidValue,
                                                     "'logging' must be a TOML table section",
                                                     "logging"});

        // Warn about unrecognised logging keys.
        for (const auto& [key, val] : *log_tbl) {
            if (!is_known_key(std::string_view{key}, kKnownLoggingKeys)) {
                std::clog << std::format(
                    "[aevox] config warning: unrecognised key 'logging.{}' — ignored\n",
                    std::string_view{key});
            }
            (void)val;
        }

        // logging.level
        if (const auto* v = log_tbl->get("level")) {
            const auto raw = v->value<std::string>();
            if (!raw)
                return std::unexpected(make_invalid("logging.level", "must be a string"));
            const std::string& s = *raw;
            if (s == "trace")
                base.logging.level = LogLevel::Trace;
            else if (s == "debug")
                base.logging.level = LogLevel::Debug;
            else if (s == "info")
                base.logging.level = LogLevel::Info;
            else if (s == "warn")
                base.logging.level = LogLevel::Warn;
            else if (s == "error")
                base.logging.level = LogLevel::Error;
            else if (s == "fatal")
                base.logging.level = LogLevel::Fatal;
            else
                return std::unexpected(
                    make_invalid("logging.level",
                                 "must be one of: trace, debug, info, warn, error, fatal"));
        }

        // logging.ring_buffer_entries
        if (const auto* v = log_tbl->get("ring_buffer_entries")) {
            const auto raw = v->value<int64_t>();
            if (!raw || *raw < kMinRingBufferEntries || *raw > kMaxRingBufferEntries)
                return std::unexpected(make_invalid("logging.ring_buffer_entries",
                                                    "must be an integer in 64..1048576"));
            base.logging.ring_buffer_entries = static_cast<std::size_t>(*raw);
        }

        // logging.sinks
        if (const auto* sinks_node = log_tbl->get("sinks")) {
            const auto* arr = sinks_node->as_array();
            if (!arr)
                return std::unexpected(make_invalid("logging.sinks", "must be an array of tables"));

            base.logging.sinks.clear();
            for (const auto& elem : *arr) {
                const auto* sink_tbl = elem.as_table();
                if (!sink_tbl)
                    continue; // skip non-table entries silently

                const auto type_raw = sink_tbl->get("type");
                if (!type_raw)
                    continue;
                const auto type_str = type_raw->value<std::string>();
                if (!type_str)
                    continue;

                if (*type_str == "console") {
                    ConsoleSinkConfig cfg;
                    if (const auto* fmt = sink_tbl->get("format")) {
                        const auto f = fmt->value<std::string>();
                        if (f && *f == "json")
                            cfg.format = LogFormat::JSON;
                    }
                    if (const auto* c = sink_tbl->get("color")) {
                        const auto col = c->value<bool>();
                        if (col)
                            cfg.color = *col;
                    }
                    base.logging.sinks.emplace_back(cfg);
                }
                else if (*type_str == "file") {
                    FileSinkConfig cfg;
                    if (const auto* p = sink_tbl->get("path")) {
                        const auto sink_path = p->value<std::string>();
                        if (sink_path)
                            cfg.path = *sink_path;
                    }
                    if (const auto* r = sink_tbl->get("rotate_mb")) {
                        const auto raw = r->value<int64_t>();
                        if (raw && *raw >= kMinPositiveValue && *raw <= kMaxLogRotateMb)
                            cfg.rotate_mb = static_cast<std::size_t>(*raw);
                    }
                    if (const auto* k = sink_tbl->get("keep_files")) {
                        const auto raw = k->value<int64_t>();
                        if (raw && *raw >= kMinPositiveValue && *raw <= kMaxLogKeepFiles)
                            cfg.keep_files = static_cast<std::size_t>(*raw);
                    }
                    if (const auto* fmt = sink_tbl->get("format")) {
                        const auto f = fmt->value<std::string>();
                        if (f && *f == "json")
                            cfg.format = LogFormat::JSON;
                        else if (f && *f == "pretty")
                            cfg.format = LogFormat::Pretty;
                    }
                    base.logging.sinks.emplace_back(cfg);
                }
            }
        }
    }

    return base;
}

} // namespace aevox::config
