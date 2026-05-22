#pragma once
// include/aevox/log.hpp
//
// Public logging API for Aevox.
//
// The v0.2 logger intentionally exposes a small surface: global log functions,
// request-correlated overloads, one runtime configuration struct, and observable
// counters. Implementation details such as queues, writer threads, formatting,
// and destinations live under src/log/.

#include <aevox/error.hpp>

#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace aevox {

class Request;

/**
 * @brief Default maximum number of queued log entries.
 *
 * Used by `LogConfig::queue_capacity` unless the application overrides it.
 *
 * @note Thread-safety: compile-time constant with no shared mutable state.
 */
inline constexpr std::uint32_t kDefaultLogQueueCapacity{8192};

/**
 * @brief Severity level for a log entry.
 *
 * Levels are ordered from most verbose to most severe. Runtime filtering drops
 * entries whose level is lower than `LogConfig::level`.
 *
 * @note Thread-safety: values are immutable and safe to use concurrently.
 */
enum class LogLevel : std::uint8_t
{
    Trace, ///< Most verbose diagnostic output.
    Debug, ///< Developer-oriented diagnostics.
    Info,  ///< Normal lifecycle or access-log events.
    Warn,  ///< Slow requests, retries, or degraded behavior.
    Error, ///< Handler, parser, or I/O failures.
    Fatal, ///< Unrecoverable process-level failure.
};

/**
 * @brief Output format for log entries.
 *
 * JSON is intended for production ingestion. Pretty is intended for local
 * development and tests.
 *
 * @note Thread-safety: values are immutable and safe to use concurrently.
 */
enum class LogFormat : std::uint8_t
{
    Json,   ///< Newline-delimited JSON object per entry.
    Pretty, ///< Human-readable one-line text.
};

/**
 * @brief Output destination used by the built-in logger.
 *
 * `File` writes newline-delimited entries to `LogConfig::file_path`.
 * `Disabled` drops every entry after level checks and is useful for benchmarks
 * or tests that do not inspect logs.
 *
 * @note Thread-safety: values are immutable and safe to use concurrently.
 */
enum class LogDestination : std::uint8_t
{
    Stdout,   ///< Write to standard output.
    Stderr,   ///< Write to standard error.
    File,     ///< Append to the configured file path.
    Disabled, ///< Drop all entries.
};

/**
 * @brief Error codes produced while starting or flushing the logging subsystem.
 *
 * Logging calls never return errors to request handlers. Startup and flush
 * operations are fallible because file destinations may fail to open or flush.
 *
 * @note Thread-safety: values are immutable and safe to use concurrently.
 */
enum class LogError : std::uint8_t
{
    FilePathRequired, ///< `LogDestination::File` was selected without `file_path`.
    FileOpenFailed,   ///< The configured file could not be opened for append.
    FlushFailed,      ///< The configured destination failed while flushing.
};

/**
 * @brief Converts a LogError to a stable human-readable string.
 *
 * @param error Error code to describe.
 * @return Static string literal with process lifetime.
 * @note Thread-safety: safe to call concurrently.
 */
[[nodiscard]] std::string_view to_string(LogError error) noexcept;

/**
 * @brief Maps a LogError to the broad Aevox error category.
 *
 * @param error Error code to classify.
 * @return Broad error category for generic handling.
 * @note Thread-safety: safe to call concurrently.
 */
[[nodiscard]] ErrorCategory category(LogError error) noexcept;

/**
 * @brief Runtime configuration for the built-in logger.
 *
 * `LogConfig` owns all configuration values copied into the internal writer at
 * `App` startup. Applications may move or destroy the original config after
 * constructing `App`.
 *
 * @note Thread-safety: configuration is read during `App` construction/listen
 *       startup and is not mutated concurrently by Aevox.
 * @note Move semantics: movable and copyable; moved-from strings follow normal
 *       `std::string` rules.
 */
struct LogConfig
{
    bool                       enabled{true};         ///< Master switch for the logging subsystem.
    LogLevel                   level{LogLevel::Info}; ///< Minimum severity accepted by the writer.
    LogFormat                  format{LogFormat::Json}; ///< Output format used by the writer.
    LogDestination             destination{LogDestination::Stdout}; ///< Active output destination.
    std::optional<std::string> file_path{std::nullopt}; ///< Required when destination is `File`.
    std::uint32_t              queue_capacity{
        kDefaultLogQueueCapacity}; ///< Maximum queued entries before drops begin.
};

/**
 * @brief Observable counters maintained by the logger.
 *
 * Counters are monotonic for the lifetime of the installed writer. They are
 * intended for tests, diagnostics, and future metrics integration.
 *
 * @note Thread-safety: returned by value from atomics; safe to read concurrently.
 */
struct LogStats
{
    std::uint64_t accepted{0}; ///< Entries accepted into the writer queue.
    std::uint64_t dropped{0};  ///< Entries dropped by filtering, contention, or capacity.
    std::uint64_t written{0};  ///< Entries written to the configured destination.
};

namespace log {

/**
 * @brief Logs a preformatted global message.
 *
 * The call never performs file or console I/O on the caller thread. If the
 * logger is disabled, the entry is below the configured level, the queue is
 * full, or the queue lock is contended, the entry is dropped. Dropped entries
 * are counted in `stats()`.
 *
 * @param level Severity level.
 * @param message Message text. The writer copies the text before this function returns.
 * @note Thread-safety: safe to call concurrently from multiple threads.
 * @note noexcept: allocation failure or writer absence degrades to a dropped entry.
 */
void write(LogLevel level, std::string_view message) noexcept;

/**
 * @brief Logs a preformatted request-correlated message.
 *
 * The emitted entry includes the request id, method, path, and trace fields
 * available on `request`. The call never performs file or console I/O on the
 * caller thread.
 *
 * @param request Request whose context should be attached.
 * @param level Severity level.
 * @param message Message text. The writer copies the text before this function returns.
 * @note Thread-safety: same as `Request`; call on the request's owning coroutine/strand.
 * @note noexcept: allocation failure or writer absence degrades to a dropped entry.
 */
void write(const Request& request, LogLevel level, std::string_view message) noexcept;

/**
 * @brief Flushes the installed logger.
 *
 * Drains queued entries and flushes the active destination. This may block and
 * must not be called from the request hot path.
 *
 * @return Empty expected on success, or `LogError` when the destination flush fails.
 * @note Thread-safety: safe to call concurrently, but calls serialize internally.
 */
[[nodiscard]] std::expected<void, LogError> flush() noexcept;

/**
 * @brief Returns current logger counters.
 *
 * @return Accepted, dropped, and written entry counts for the installed writer.
 * @note Thread-safety: safe to call concurrently.
 */
[[nodiscard]] LogStats stats() noexcept;

/**
 * @brief Logs a TRACE-level global message.
 *
 * @tparam Args Format argument types.
 * @param fmt Compile-time checked format string.
 * @param args Values referenced by the format string.
 * @note Thread-safety: safe to call concurrently from multiple threads.
 * @note noexcept: formatting failures emit `"[format error]"`.
 */
template <typename... Args> void trace(std::format_string<Args...> fmt, Args&&... args) noexcept
{
    try {
        write(LogLevel::Trace, std::format(fmt, std::forward<Args>(args)...));
    }
    catch (...) {
        write(LogLevel::Trace, "[format error]");
    }
}

/**
 * @brief Logs a DEBUG-level global message.
 *
 * @tparam Args Format argument types.
 * @param fmt Compile-time checked format string.
 * @param args Values referenced by the format string.
 * @note Thread-safety: safe to call concurrently from multiple threads.
 * @note noexcept: formatting failures emit `"[format error]"`.
 */
template <typename... Args> void debug(std::format_string<Args...> fmt, Args&&... args) noexcept
{
    try {
        write(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
    }
    catch (...) {
        write(LogLevel::Debug, "[format error]");
    }
}

/**
 * @brief Logs an INFO-level global message.
 *
 * @tparam Args Format argument types.
 * @param fmt Compile-time checked format string.
 * @param args Values referenced by the format string.
 * @note Thread-safety: safe to call concurrently from multiple threads.
 * @note noexcept: formatting failures emit `"[format error]"`.
 */
template <typename... Args> void info(std::format_string<Args...> fmt, Args&&... args) noexcept
{
    try {
        write(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
    }
    catch (...) {
        write(LogLevel::Info, "[format error]");
    }
}

/**
 * @brief Logs a WARN-level global message.
 *
 * @tparam Args Format argument types.
 * @param fmt Compile-time checked format string.
 * @param args Values referenced by the format string.
 * @note Thread-safety: safe to call concurrently from multiple threads.
 * @note noexcept: formatting failures emit `"[format error]"`.
 */
template <typename... Args> void warn(std::format_string<Args...> fmt, Args&&... args) noexcept
{
    try {
        write(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
    }
    catch (...) {
        write(LogLevel::Warn, "[format error]");
    }
}

/**
 * @brief Logs an ERROR-level global message.
 *
 * @tparam Args Format argument types.
 * @param fmt Compile-time checked format string.
 * @param args Values referenced by the format string.
 * @note Thread-safety: safe to call concurrently from multiple threads.
 * @note noexcept: formatting failures emit `"[format error]"`.
 */
template <typename... Args> void error(std::format_string<Args...> fmt, Args&&... args) noexcept
{
    try {
        write(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
    }
    catch (...) {
        write(LogLevel::Error, "[format error]");
    }
}

/**
 * @brief Logs a FATAL-level global message.
 *
 * @tparam Args Format argument types.
 * @param fmt Compile-time checked format string.
 * @param args Values referenced by the format string.
 * @note Thread-safety: safe to call concurrently from multiple threads.
 * @note noexcept: formatting failures emit `"[format error]"`.
 */
template <typename... Args> void fatal(std::format_string<Args...> fmt, Args&&... args) noexcept
{
    try {
        write(LogLevel::Fatal, std::format(fmt, std::forward<Args>(args)...));
    }
    catch (...) {
        write(LogLevel::Fatal, "[format error]");
    }
}

/**
 * @brief Logs a TRACE-level request-correlated message.
 *
 * @tparam Args Format argument types.
 * @param request Request whose correlation context is attached.
 * @param fmt Compile-time checked format string.
 * @param args Values referenced by the format string.
 * @note Thread-safety: same as `Request`; call on the owning coroutine/strand.
 * @note noexcept: formatting failures emit `"[format error]"`.
 */
template <typename... Args>
void trace(const Request& request, std::format_string<Args...> fmt, Args&&... args) noexcept
{
    try {
        write(request, LogLevel::Trace, std::format(fmt, std::forward<Args>(args)...));
    }
    catch (...) {
        write(request, LogLevel::Trace, "[format error]");
    }
}

/**
 * @brief Logs a DEBUG-level request-correlated message.
 *
 * @tparam Args Format argument types.
 * @param request Request whose correlation context is attached.
 * @param fmt Compile-time checked format string.
 * @param args Values referenced by the format string.
 * @note Thread-safety: same as `Request`; call on the owning coroutine/strand.
 * @note noexcept: formatting failures emit `"[format error]"`.
 */
template <typename... Args>
void debug(const Request& request, std::format_string<Args...> fmt, Args&&... args) noexcept
{
    try {
        write(request, LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
    }
    catch (...) {
        write(request, LogLevel::Debug, "[format error]");
    }
}

/**
 * @brief Logs an INFO-level request-correlated message.
 *
 * @tparam Args Format argument types.
 * @param request Request whose correlation context is attached.
 * @param fmt Compile-time checked format string.
 * @param args Values referenced by the format string.
 * @note Thread-safety: same as `Request`; call on the owning coroutine/strand.
 * @note noexcept: formatting failures emit `"[format error]"`.
 */
template <typename... Args>
void info(const Request& request, std::format_string<Args...> fmt, Args&&... args) noexcept
{
    try {
        write(request, LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
    }
    catch (...) {
        write(request, LogLevel::Info, "[format error]");
    }
}

/**
 * @brief Logs a WARN-level request-correlated message.
 *
 * @tparam Args Format argument types.
 * @param request Request whose correlation context is attached.
 * @param fmt Compile-time checked format string.
 * @param args Values referenced by the format string.
 * @note Thread-safety: same as `Request`; call on the owning coroutine/strand.
 * @note noexcept: formatting failures emit `"[format error]"`.
 */
template <typename... Args>
void warn(const Request& request, std::format_string<Args...> fmt, Args&&... args) noexcept
{
    try {
        write(request, LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
    }
    catch (...) {
        write(request, LogLevel::Warn, "[format error]");
    }
}

/**
 * @brief Logs an ERROR-level request-correlated message.
 *
 * @tparam Args Format argument types.
 * @param request Request whose correlation context is attached.
 * @param fmt Compile-time checked format string.
 * @param args Values referenced by the format string.
 * @note Thread-safety: same as `Request`; call on the owning coroutine/strand.
 * @note noexcept: formatting failures emit `"[format error]"`.
 */
template <typename... Args>
void error(const Request& request, std::format_string<Args...> fmt, Args&&... args) noexcept
{
    try {
        write(request, LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
    }
    catch (...) {
        write(request, LogLevel::Error, "[format error]");
    }
}

/**
 * @brief Logs a FATAL-level request-correlated message.
 *
 * @tparam Args Format argument types.
 * @param request Request whose correlation context is attached.
 * @param fmt Compile-time checked format string.
 * @param args Values referenced by the format string.
 * @note Thread-safety: same as `Request`; call on the owning coroutine/strand.
 * @note noexcept: formatting failures emit `"[format error]"`.
 */
template <typename... Args>
void fatal(const Request& request, std::format_string<Args...> fmt, Args&&... args) noexcept
{
    try {
        write(request, LogLevel::Fatal, std::format(fmt, std::forward<Args>(args)...));
    }
    catch (...) {
        write(request, LogLevel::Fatal, "[format error]");
    }
}

} // namespace log

} // namespace aevox
