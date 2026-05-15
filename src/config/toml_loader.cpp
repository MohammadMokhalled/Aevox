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

#include <array>
#include <filesystem>
#include <format>
#include <iostream>
#include <toml++/toml.hpp>

namespace aevox::config {

namespace {

// Known top-level TOML key names. Used to detect unrecognised keys.
constexpr std::array<std::string_view, 9> kKnownTopLevelKeys{
    "port",           "host",     "backlog", "max_body_size", "request_timeout", "max_header_count",
    "max_read_bytes", "executor", "logging",
};

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
    return ConfigErrorDetail{
        .code    = ConfigError::InvalidValue,
        .message = std::format("invalid value for '{}': {}", key, reason),
        .key     = std::string{key},
    };
}

} // namespace

[[nodiscard]] std::expected<AppConfig, ConfigErrorDetail> load_toml_config(std::string_view path,
                                                                           AppConfig base) noexcept
{
    // Check existence before parsing — toml++ throws parse_error for missing files,
    // which would be indistinguishable from a TOML syntax error without this check.
    if (!std::filesystem::exists(std::filesystem::path{path})) {
        return std::unexpected(ConfigErrorDetail{
            .code    = ConfigError::FileNotFound,
            .message = std::format("config file not found: '{}'", path),
            .key     = {},
        });
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    }
    catch (const toml::parse_error& e) {
        return std::unexpected(ConfigErrorDetail{
            .code    = ConfigError::ParseError,
            .message = std::format("TOML parse error in '{}': {}", path, e.description()),
            .key     = {},
        });
    }
    catch (...) {
        return std::unexpected(ConfigErrorDetail{
            .code    = ConfigError::ParseError,
            .message = std::format("could not read config file '{}'", path),
            .key     = {},
        });
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
        if (!raw || *raw < 1 || *raw > 65535)
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
        if (!raw || *raw < 1 || *raw > 65535)
            return std::unexpected(make_invalid("backlog", "must be an integer in 1..65535"));
        base.backlog = static_cast<int>(*raw);
    }

    // ── max_body_size ─────────────────────────────────────────────────────────
    if (const auto* v = tbl.get("max_body_size")) {
        const auto        raw         = v->value<int64_t>();
        constexpr int64_t kMaxAllowed = 2LL * 1024LL * 1024LL * 1024LL; // 2 GiB
        if (!raw || *raw < 1 || *raw > kMaxAllowed)
            return std::unexpected(
                make_invalid("max_body_size", "must be an integer in 1..2147483648"));
        base.max_body_size = static_cast<std::size_t>(*raw);
    }

    // ── request_timeout ───────────────────────────────────────────────────────
    if (const auto* v = tbl.get("request_timeout")) {
        const auto raw = v->value<int64_t>();
        if (!raw || *raw < 1 || *raw > 3600)
            return std::unexpected(
                make_invalid("request_timeout", "must be an integer in 1..3600 (seconds)"));
        base.request_timeout = std::chrono::seconds{*raw};
    }

    // ── max_header_count ──────────────────────────────────────────────────────
    if (const auto* v = tbl.get("max_header_count")) {
        const auto raw = v->value<int64_t>();
        if (!raw || *raw < 1 || *raw > 1000)
            return std::unexpected(
                make_invalid("max_header_count", "must be an integer in 1..1000"));
        base.max_header_count = static_cast<std::size_t>(*raw);
    }

    // ── max_read_bytes ────────────────────────────────────────────────────────
    if (const auto* v = tbl.get("max_read_bytes")) {
        const auto        raw      = v->value<int64_t>();
        constexpr int64_t kMaxRead = 16LL * 1024LL * 1024LL; // 16 MiB
        if (!raw || *raw < 512 || *raw > kMaxRead)
            return std::unexpected(
                make_invalid("max_read_bytes", "must be an integer in 512..16777216"));
        base.max_read_bytes = static_cast<std::size_t>(*raw);
    }

    // ── [executor] section ────────────────────────────────────────────────────
    if (const auto* ex_node = tbl.get("executor")) {
        const auto* ex = ex_node->as_table();
        if (!ex)
            return std::unexpected(ConfigErrorDetail{
                .code    = ConfigError::InvalidValue,
                .message = "'executor' must be a TOML table section",
                .key     = "executor",
            });

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
            if (!raw || *raw < 0 || *raw > 1024)
                return std::unexpected(
                    make_invalid("executor.thread_count", "must be an integer in 0..1024"));
            base.executor.thread_count = static_cast<std::size_t>(*raw);
        }

        // executor.cpu_pool_threads
        if (const auto* v = ex->get("cpu_pool_threads")) {
            const auto raw = v->value<int64_t>();
            if (!raw || *raw < 0 || *raw > 256)
                return std::unexpected(
                    make_invalid("executor.cpu_pool_threads", "must be an integer in 0..256"));
            base.executor.cpu_pool_threads = static_cast<std::size_t>(*raw);
        }

        // executor.drain_timeout
        if (const auto* v = ex->get("drain_timeout")) {
            const auto raw = v->value<int64_t>();
            if (!raw || *raw < 1 || *raw > 3600)
                return std::unexpected(make_invalid("executor.drain_timeout",
                                                    "must be an integer in 1..3600 (seconds)"));
            base.executor.drain_timeout = std::chrono::seconds{*raw};
        }
    }

    // ── [logging] section ─────────────────────────────────────────────────────
    if (const auto* log_node = tbl.get("logging")) {
        const auto* log_tbl = log_node->as_table();
        if (!log_tbl)
            return std::unexpected(ConfigErrorDetail{
                .code    = ConfigError::InvalidValue,
                .message = "'logging' must be a TOML table section",
                .key     = "logging",
            });

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
            const auto        raw      = v->value<int64_t>();
            constexpr int64_t kMaxRing = 1024LL * 1024LL; // 1M entries
            if (!raw || *raw < 64 || *raw > kMaxRing)
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
                        if (raw && *raw >= 1 && *raw <= 4096)
                            cfg.rotate_mb = static_cast<std::size_t>(*raw);
                    }
                    if (const auto* k = sink_tbl->get("keep_files")) {
                        const auto raw = k->value<int64_t>();
                        if (raw && *raw >= 1 && *raw <= 100)
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
