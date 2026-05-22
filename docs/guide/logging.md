# Logging

Aevox logging is built around a small rule: write useful logs without making request handlers wait
on file or console I/O. Calls enqueue bounded records, and a background writer formats and writes
them.

## Basic Setup

The default logger writes JSON to stdout at `Info` level:

```cpp
aevox::App app;
app.use(aevox::middleware::logger());
```

For file output:

```cpp
aevox::AppConfig config;
config.logging.destination = aevox::LogDestination::File;
config.logging.file_path = "/var/log/aevox/app.log";
config.logging.format = aevox::LogFormat::Json;

aevox::App app{config};
app.use(aevox::middleware::logger());
```

## Handler Logs

Use global functions for startup and background work:

```cpp
aevox::log::info("Server starting on port {}", port);
```

Pass a request when the log line should be correlated:

```cpp
app.get("/users/{id}", [](aevox::Request& req) -> aevox::Task<aevox::Response> {
    aevox::log::info(req, "Fetching user {}", req.param<int>("id").value());
    co_return aevox::Response::ok("ok");
});
```

Request-correlated entries include `request_id`, method, path, and valid trace fields. The same id is
available to handlers:

```cpp
std::string_view request_id = req.id();
```

## Access Logs

`aevox::middleware::logger()` emits one line after the response is produced:

```json
{"timestamp":"2026-05-22T12:00:00.123456Z","level":"INFO","request_id":"0000000000000001","method":"GET","path":"/users/42","status":200,"duration_us":85,"message":"GET /users/42 -> 200 in 85us"}
```

Exclude noisy paths:

```cpp
app.use(aevox::middleware::logger({
    .exclude_paths = {"/health", "/metrics"},
}));
```

Warn on slow requests:

```cpp
app.use(aevox::middleware::logger({
    .slow_request_threshold = std::chrono::milliseconds{100},
}));
```

Slow requests are logged once at `Warn`.

## TOML

```toml
[logging]
enabled = true
level = "info"
format = "json"
destination = "file"
file_path = "/var/log/aevox/app.log"
queue_capacity = 16384
```

Supported destinations are `stdout`, `stderr`, `file`, and `disabled`.

If file logging cannot start, Aevox reports the logging error to standard error and continues with
logging disabled. Fix the path or permissions and restart the process to re-enable file output.

## Flush In Tests

The logger is asynchronous. Tests that inspect a log file should flush before reading:

```cpp
REQUIRE(aevox::log::flush().has_value());
```

`aevox::log::stats()` exposes accepted, dropped, and written counters for diagnostics.

## Trace Context

Aevox extracts valid W3C `traceparent` headers before middleware runs. Request-correlated log lines
include `trace_id` and `span_id` when those fields are present. Use `req.trace_context()` to forward
the original header to downstream services.

## See Also

- [Logging API](../api/log.md)
- [Middleware Guide](middleware.md)
- [Configuration API](../api/config.md)
