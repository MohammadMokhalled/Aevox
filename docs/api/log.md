# Logging

> Structured, asynchronous logging with per-request correlation and zero-allocation hot path.

## Overview

The logging subsystem in `<aevox/log.hpp>` provides severity-based structured logging that is fully asynchronous. Log entries are pushed to a lock-free ring buffer by the calling thread; a background drain thread formats and writes them to the configured sinks. This design guarantees that the hot path (`logger.info(...)`) never blocks on I/O.

Key features:

- **No blocking on I/O** — `Logger::info()` returns immediately; a background thread handles disk or console output.
- **Per-request correlation** — every `Request` carries a `Logger` that automatically tags entries with `request_id` and `thread_id`.
- **Structured JSON or pretty output** — sink-level control over formatting.
- **Thread-safe** — multiple request handlers can log concurrently without contention.

## Quick Start

```cpp
#include <aevox/log.hpp>

// Inside a handler — req.logger() is already set up by the framework
app.get("/orders/{id}", [](aevox::Request& req) -> aevox::Task<aevox::Response> {
    req.logger().info("Processing order {}", req.param<int>("id").value());
    // ...
    co_return aevox::Response::ok(result);
});
```

## API Reference

### `aevox::log::LogLevel`

```cpp
enum class LogLevel : std::uint8_t
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};
```

Severity levels in ascending order. Runtime filtering is controlled by `LogConfig::level`.

### `aevox::log::LogFormat`

```cpp
enum class LogFormat : std::uint8_t
{
    JSON,
    Pretty,
};
```

Output format for sinks. `JSON` is recommended for production log aggregation.

### `aevox::log::LogField`

```cpp
enum class LogField : std::uint8_t
{
    Timestamp,     // Unix nanoseconds since epoch.
    Level,         // Log severity (TRACE ... FATAL).
    RequestId,     // Unique request identifier assigned by the acceptor.
    ThreadId,      // Hashed OS thread ID that handled the request.
    Method,        // HTTP method (GET, POST, ...).
    Path,          // Request path without query string.
    Status,        // HTTP response status code.
    DurationMs,    // Wall-clock time from request start to response sent.
    Ip,            // Client remote address.
    UserAgent,     // Value of the User-Agent header.
    BodySize,      // Response body length in bytes.
    Message,       // Free-form log message text.
    CorrelationId, // Deprecated alias for TraceId. Use TraceId in new code.
    TraceId,       // W3C trace_id from traceparent header (32 hex chars).
    SpanId,        // W3C parent_id (span_id) from traceparent header (16 hex chars).
};
```

Fields available for automatic request/response logging via `aevox::middleware::logger()`.

`CorrelationId` is a deprecated alias for `TraceId`. New code should use `TraceId` and `SpanId` directly.

### `aevox::log::ConsoleSinkConfig`

```cpp
struct ConsoleSinkConfig
{
    LogFormat format{LogFormat::Pretty};
    bool      color{true};
};
```

Configuration for a `stdout` sink.

### `aevox::log::FileSinkConfig`

```cpp
struct FileSinkConfig
{
    std::string path;
    std::size_t rotate_mb{100};
    std::size_t keep_files{10};
    LogFormat   format{LogFormat::JSON};
};
```

Configuration for a file sink with rotation.

### `aevox::log::LogConfig`

```cpp
struct LogConfig
{
    LogLevel level{LogLevel::Info};
    std::vector<std::variant<ConsoleSinkConfig, FileSinkConfig>> sinks{
        ConsoleSinkConfig{}
    };
    std::size_t ring_buffer_entries{65536};
};
```

Runtime logging configuration. Passed via `AppConfig::logging`.

### `aevox::log::Logger`

```cpp
class Logger
{
public:
    template <typename... Args>
    void trace(std::format_string<Args...> fmt, Args&&... args) noexcept;

    template <typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) noexcept;

    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) noexcept;

    template <typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) noexcept;

    template <typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) noexcept;

    template <typename... Args>
    void fatal(std::format_string<Args...> fmt, Args&&... args) noexcept;
};
```

Lightweight handle to the async logging system. Each `Request` exposes `logger()`, which returns a `Logger` pre-configured with the request's correlation context.

All methods are `noexcept` and return immediately. If the ring buffer is full, the entry is silently dropped.

### `aevox::log::global()`

```cpp
[[nodiscard]] Logger& global() noexcept;
```

Returns the global application logger. Use this for framework-level logging outside of request handlers. Automatically initialised when `App::listen()` is called.

### Global level functions

```cpp
aevox::log::trace("state = {}", state);
aevox::log::debug("value = {}", value);
```

These functions log through the global application logger. Runtime level filtering is configured with `LogConfig::level`.

---

## Logger Middleware

### `aevox::middleware::logger()`

```cpp
[[nodiscard]] Middleware logger(LoggerMiddlewareConfig config = {});
```

Factory function that creates a middleware which automatically logs every incoming request and outgoing response. The produced log line includes timestamp, method, path, status code, and duration.

### `aevox::log::LoggerMiddlewareConfig`

```cpp
struct LoggerMiddlewareConfig
{
    LogFormat format{LogFormat::JSON};
    LogLevel  level{LogLevel::Info};
    std::vector<LogField> include{...};
    std::unordered_set<std::string> exclude_paths{};
    std::chrono::milliseconds slow_request_threshold{500};
};
```

Configuration for the logger middleware. Use `exclude_paths` to suppress logging for health checks or metrics endpoints. Requests exceeding `slow_request_threshold` emit an additional `WARN` entry.

---

## Configuration

Logging is configured via `AppConfig::logging`:

```cpp
aevox::AppConfig config;
config.logging.level = aevox::log::LogLevel::Info;
config.logging.sinks = {
    aevox::log::ConsoleSinkConfig{.format = aevox::log::LogFormat::Pretty, .color = true},
    aevox::log::FileSinkConfig{.path = "/var/log/aevox/app.log", .format = aevox::log::LogFormat::JSON},
};
config.logging.ring_buffer_entries = 65536;
```

Or via TOML:

```toml
[logging]
level = "info"
ring_buffer_entries = 65536

[[logging.sinks]]
type = "console"
format = "pretty"
color = true

[[logging.sinks]]
type = "file"
path = "/var/log/aevox/app.log"
format = "json"
rotate_mb = 100
keep_files = 5
```
