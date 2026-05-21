#pragma once
// include/aevox/log.hpp
//
// Public logging API for Aevox.
//
// Provides level-based logging (trace, debug, info, warn, error, fatal),
// structured output (JSON / pretty), multi-sink configuration, and
// request-correlated logging via aevox::Request::logger().
//
// Thread-safety: All Logger methods are thread-safe. The underlying async
// writer uses a lock-free ring buffer; formatting and sink I/O happen on a
// dedicated background thread.
//
// Invariants:
//   - No backend networking types appear in this header.
//   - No spdlog types appear in this header.
//   - All heap allocation goes through std::make_unique / std::make_shared.
//
// Design: Tasks/architecture/AEV-011-arch.md §3.1

#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace aevox {

inline constexpr std::size_t kDefaultLogRotateMb{100};
inline constexpr std::size_t kDefaultLogKeepFiles{10};
inline constexpr std::size_t kDefaultLogRingBufferEntries{65536};

// =============================================================================
// Severity levels
// =============================================================================

/**
 * @brief Severity levels for log entries.
 *
 * @note TRACE and DEBUG are compiled out entirely in release builds
 *       (defined away to zero instructions). INFO and above are always
 *       available at runtime with level-filtering.
 */
enum class LogLevel : std::uint8_t
{
    Trace, ///< Every coroutine suspension, every byte read. Compiled out in release.
    Debug, ///< Request lifecycle, middleware, DB queries. Compiled out in release.
    Info,  ///< Request completed, server started, config loaded.
    Warn,  ///< Slow requests, retries, deprecated usage.
    Error, ///< Handler failure, parse error, connection lost.
    Fatal, ///< Unrecoverable — triggers graceful shutdown.
};

// =============================================================================
// Output format
// =============================================================================

/**
 * @brief Output format selector for log sinks.
 */
enum class LogFormat : std::uint8_t
{
    JSON,   ///< Structured JSON output for production log aggregation.
    Pretty, ///< Human-readable single-line output for development terminals.
};

// =============================================================================
// Structured log fields
// =============================================================================

/**
 * @brief Fields that may appear in a structured log line.
 *
 * Used by the logger middleware to select which fields to emit.
 */
enum class LogField : std::uint8_t
{
    Timestamp,     ///< Unix nanoseconds since epoch.
    Level,         ///< Log severity (TRACE … FATAL).
    RequestId,     ///< Unique request identifier assigned by the acceptor.
    ThreadId,      ///< Hashed OS thread ID that handled the request.
    Method,        ///< HTTP method (GET, POST, …).
    Path,          ///< Request path without query string.
    Status,        ///< HTTP response status code.
    DurationMs,    ///< Wall-clock time from request start to response sent.
    Ip,            ///< Client remote address.
    UserAgent,     ///< Value of the User-Agent header.
    BodySize,      ///< Response body length in bytes.
    Message,       ///< Free-form log message text.
    CorrelationId, ///< Deprecated alias for TraceId. Use TraceId in new code.
    TraceId,       ///< W3C trace_id from traceparent header (32 hex chars).
    SpanId,        ///< W3C parent_id (span_id) from traceparent header (16 hex chars).
};

// =============================================================================
// Sink configuration
// =============================================================================

/**
 * @brief Configuration for a console (stdout) sink.
 */
struct ConsoleSinkConfig
{
    LogFormat format{LogFormat::Pretty};
    bool      color{true};
};

/**
 * @brief Configuration for a rotating file sink.
 */
struct FileSinkConfig
{
    std::string path;                             ///< Absolute or relative file path.
    std::size_t rotate_mb{kDefaultLogRotateMb};   ///< Maximum file size before rotation.
    std::size_t keep_files{kDefaultLogKeepFiles}; ///< Number of rotated files to retain.
    LogFormat   format{LogFormat::JSON};
};

// =============================================================================
// Runtime logging configuration
// =============================================================================

/**
 * @brief Runtime logging configuration passed to AppConfig.
 *
 * All fields have sensible production defaults.
 */
struct LogConfig
{
    LogLevel                                                     level{LogLevel::Info};
    std::vector<std::variant<ConsoleSinkConfig, FileSinkConfig>> sinks{ConsoleSinkConfig{}};
    std::size_t                                                  ring_buffer_entries{
        kDefaultLogRingBufferEntries}; ///< Per-queue capacity. Power of 2 recommended.
};

// =============================================================================
// Forward declarations for Logger internals
// =============================================================================

class AsyncLogWriter;
struct RequestContext;

// =============================================================================
// Logger
// =============================================================================

/**
 * @brief Primary logging interface — global and per-request.
 *
 * `Logger` is lightweight (two pointers: one to the async writer, one to
 * optional request context). It is safe to copy and move across threads.
 * All methods are `noexcept` — formatting failures are handled internally
 * by writing a fallback message, never throwing.
 *
 * @note The global logger (accessed via `aevox::log::info(...)`) has no
 *       request context. The per-request logger (`req.logger().info(...)`) carries
 *       `request_id`, `thread_id`, and `timestamp` automatically.
 * @note Thread-safe: multiple threads may call `Logger` methods concurrently.
 *       The underlying `AsyncLogWriter` uses a lock-free queue.
 */
class Logger
{
public:
    /**
     * @brief Default constructor — creates a no-op logger.
     *
     * A default-constructed Logger discards all log entries silently.
     */
    Logger() noexcept = default;

    /**
     * @brief Logs a TRACE-level message.
     */
    template <typename... Args> void trace(std::format_string<Args...> fmt, Args&&... args) noexcept
    {
        try {
            log(LogLevel::Trace, std::format(fmt, std::forward<Args>(args)...));
        }
        catch (...) {
            log(LogLevel::Trace, "[format error]");
        }
    }

    /**
     * @brief Logs a DEBUG-level message.
     */
    template <typename... Args> void debug(std::format_string<Args...> fmt, Args&&... args) noexcept
    {
        try {
            log(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
        }
        catch (...) {
            log(LogLevel::Debug, "[format error]");
        }
    }

    /**
     * @brief Logs an INFO-level message.
     */
    template <typename... Args> void info(std::format_string<Args...> fmt, Args&&... args) noexcept
    {
        try {
            log(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
        }
        catch (...) {
            log(LogLevel::Info, "[format error]");
        }
    }

    /**
     * @brief Logs a WARN-level message.
     */
    template <typename... Args> void warn(std::format_string<Args...> fmt, Args&&... args) noexcept
    {
        try {
            log(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
        }
        catch (...) {
            log(LogLevel::Warn, "[format error]");
        }
    }

    /**
     * @brief Logs an ERROR-level message.
     */
    template <typename... Args> void error(std::format_string<Args...> fmt, Args&&... args) noexcept
    {
        try {
            log(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
        }
        catch (...) {
            log(LogLevel::Error, "[format error]");
        }
    }

    /**
     * @brief Logs a FATAL-level message.
     */
    template <typename... Args> void fatal(std::format_string<Args...> fmt, Args&&... args) noexcept
    {
        try {
            log(LogLevel::Fatal, std::format(fmt, std::forward<Args>(args)...));
        }
        catch (...) {
            log(LogLevel::Fatal, "[format error]");
        }
    }

    /**
     * @brief Logs a pre-formatted message at the given level.
     *
     * Used by the middleware and by the templated level methods above.
     * This is the single non-template entry point — all formatting funnels
     * here before being pushed to the async writer.
     *
     * @param level    Severity level.
     * @param message  Pre-formatted message text.
     */
    void log(LogLevel level, std::string_view message) noexcept;

private:
    friend class AsyncLogWriter;

    std::optional<std::reference_wrapper<AsyncLogWriter>> writer_;
    std::optional<std::reference_wrapper<RequestContext>> context_;

    explicit Logger(
        AsyncLogWriter&                                       writer,
        std::optional<std::reference_wrapper<RequestContext>> ctx = std::nullopt) noexcept;

    void set_writer(AsyncLogWriter& writer) noexcept;
};

// =============================================================================
// Global logger free functions
// =============================================================================

namespace log {

/**
 * @brief Returns the global logger instance.
 *
 * The global logger is initialised lazily on first use with a no-op
 * configuration. `App::listen()` replaces it with the real configured
 * instance before accepting connections.
 */
[[nodiscard]] Logger& global() noexcept;

/**
 * @brief Logs a TRACE-level message via the global logger.
 */
template <typename... Args>
inline void trace(std::format_string<Args...> fmt, Args&&... args) noexcept
{
    global().trace(fmt, std::forward<Args>(args)...);
}

/**
 * @brief Logs a DEBUG-level message via the global logger.
 */
template <typename... Args>
inline void debug(std::format_string<Args...> fmt, Args&&... args) noexcept
{
    global().debug(fmt, std::forward<Args>(args)...);
}

/**
 * @brief Logs an INFO-level message via the global logger.
 */
template <typename... Args>
inline void info(std::format_string<Args...> fmt, Args&&... args) noexcept
{
    global().info(fmt, std::forward<Args>(args)...);
}

/**
 * @brief Logs a WARN-level message via the global logger.
 */
template <typename... Args>
inline void warn(std::format_string<Args...> fmt, Args&&... args) noexcept
{
    global().warn(fmt, std::forward<Args>(args)...);
}

/**
 * @brief Logs an ERROR-level message via the global logger.
 */
template <typename... Args>
inline void error(std::format_string<Args...> fmt, Args&&... args) noexcept
{
    global().error(fmt, std::forward<Args>(args)...);
}

/**
 * @brief Logs a FATAL-level message via the global logger.
 */
template <typename... Args>
inline void fatal(std::format_string<Args...> fmt, Args&&... args) noexcept
{
    global().fatal(fmt, std::forward<Args>(args)...);
}

} // namespace log

} // namespace aevox
