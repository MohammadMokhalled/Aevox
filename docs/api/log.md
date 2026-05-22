# Logging

> Structured asynchronous logging with global and request-correlated entry points.

**Header:** `#include <aevox/log.hpp>`

---

## Overview

Aevox logging uses a bounded queue and a background writer so request handlers never perform file or
console I/O directly. The public surface is intentionally small: one configuration struct, global
log functions, request-correlated overloads, `flush()`, and `stats()`.

Request-correlated entries include `request_id`, method, path, and valid trace fields. Automatic
HTTP access logging is provided by `#include <aevox/middleware/logger.hpp>`.

## Quick Start

```cpp
aevox::AppConfig config;
config.logging.destination = aevox::LogDestination::File;
config.logging.file_path = "/var/log/aevox/app.log";

aevox::App app{config};
app.use(aevox::middleware::logger());

app.get("/orders/{id}", [](aevox::Request& req) -> aevox::Task<aevox::Response> {
    aevox::log::info(req, "Processing order {}", req.param<int>("id").value());
    co_return aevox::Response::ok("ok");
});
```

## API Reference

#### `aevox::LogLevel`

| Value | Meaning | Typical response |
|---|---|---|
| `Trace` | Very verbose diagnostics | Enable only during local diagnosis |
| `Debug` | Developer diagnostics | Enable in development |
| `Info` | Normal lifecycle and access events | Default production level |
| `Warn` | Slow requests or degraded behavior | Investigate if sustained |
| `Error` | Handler, parser, or I/O failure | Alert or inspect logs |
| `Fatal` | Unrecoverable process-level failure | Shut down or restart |

#### `aevox::LogFormat`

| Value | Meaning | Typical response |
|---|---|---|
| `Json` | Newline-delimited JSON object per entry | Use for production ingestion |
| `Pretty` | Human-readable one-line text | Use for local development |

#### `aevox::LogDestination`

| Value | Meaning | Typical response |
|---|---|---|
| `Stdout` | Write to standard output | Container-friendly default |
| `Stderr` | Write to standard error | Useful for process supervisors |
| `File` | Append to `LogConfig::file_path` | Use for local file collection |
| `Disabled` | Drop all entries | Use for benchmarks |

#### `aevox::LogConfig`

```cpp
struct LogConfig
{
    bool enabled{true};
    LogLevel level{LogLevel::Info};
    LogFormat format{LogFormat::Json};
    LogDestination destination{LogDestination::Stdout};
    std::optional<std::string> file_path{std::nullopt};
    std::uint32_t queue_capacity{kDefaultLogQueueCapacity};
};
```

TOML equivalent:

```toml
[logging]
enabled = true
level = "info"
format = "json"
destination = "file"
file_path = "/var/log/aevox/app.log"
queue_capacity = 16384
```

#### `aevox::LogStats`

```cpp
struct LogStats
{
    std::uint64_t accepted;
    std::uint64_t dropped;
    std::uint64_t written;
};
```

`accepted` counts records accepted into the queue. `dropped` counts records filtered out or rejected
because the queue could not accept them immediately. `written` counts records written by the
background writer.

#### `aevox::log::write()`

```cpp
void write(LogLevel level, std::string_view message) noexcept;
void write(const Request& request, LogLevel level, std::string_view message) noexcept;
```

Use `write()` when the message is already formatted.

#### `aevox::log::{trace,debug,info,warn,error,fatal}()`

```cpp
aevox::log::info("Server starting on port {}", port);
aevox::log::warn(req, "Slow lookup for {}", user_id);
```

The request overloads attach request correlation fields. Formatting failures emit
`"[format error]"` instead of throwing.

#### `aevox::log::flush()`

```cpp
auto result = aevox::log::flush();
if (!result) {
    // result.error() is aevox::LogError
}
```

`flush()` drains queued entries and flushes the destination. It may block and should not be called
from request handlers.

#### `aevox::log::stats()`

```cpp
const aevox::LogStats stats = aevox::log::stats();
```

Returns counters by value. The values are intended for diagnostics, tests, and future metrics.

#### `aevox::Request::id()`

```cpp
std::string_view request_id = req.id();
```

Returns the request id that also appears in request-correlated logs.

#### `aevox::middleware::logger()`

```cpp
app.use(aevox::middleware::logger({
    .exclude_paths = {"/health"},
}));
```

The middleware emits one access log entry after the response is produced. Slow requests emit the
access entry at `Warn`; they do not emit a second duplicate line.

## Error Reference

| Value | Meaning | Typical response |
|---|---|---|
| `FilePathRequired` | `File` destination was selected without `file_path` | Fix configuration |
| `FileOpenFailed` | The configured file could not be opened | Check path and permissions |
| `FlushFailed` | Destination failed while flushing | Inspect disk or stream state |

Use `aevox::to_string(LogError)` and `aevox::category(LogError)` for diagnostics.

When an `App` cannot start the configured logger, Aevox writes a diagnostic to standard error and
continues with logging disabled. Request handling is not aborted because logging is observability, not
the service's availability boundary.

## Thread Safety

Global log functions are safe to call concurrently from multiple threads. Request-correlated
overloads follow the `Request` contract: call them on the request's owning coroutine/strand.

`flush()` is thread-safe but serializes internally and may block. `stats()` reads atomic counters and
is safe to call concurrently.

## Implementation Notes

The implementation lives in `src/log/`. Public headers expose no spdlog, fmtlib, Asio, llhttp,
glaze, or toml++ types. The background writer owns the queue and destination; callers only enqueue
records or observe counters.

!!! note
    The queue is bounded. Under contention or overload, Aevox drops log entries instead of blocking
    request handlers.

## See Also

- [Logging Guide](../guide/logging.md)
- [Middleware API](middleware.md)
- [Request and Response API](request-response.md)
