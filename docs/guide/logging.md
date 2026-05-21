# Logging

Structured, asynchronous logging is a first-class subsystem in Aevox. This guide covers how to use it in handlers, how to configure sinks and levels, and how to read the resulting logs.

## Philosophy

Aevox logging follows three design principles:

1. **Never block the I/O thread.** `req.logger().info(...)` pushes to a lock-free ring buffer and returns instantly. A background thread handles formatting and disk writes.
2. **Always know which request produced a log line.** Every `Request` carries a `Logger` that automatically includes `request_id` and `thread_id` in every entry.
3. **Structured by default.** Sinks output JSON so logs can be ingested by Loki, Elasticsearch, or CloudWatch without parsing.

## Using the Request Logger

Every `Request` has a `log` member. Use it inside handlers:

```cpp
app.get("/users/{id}", [](aevox::Request& req) -> aevox::Task<aevox::Response> {
    req.logger().info("Fetching user {}", req.param<int>("id").value());

    auto user = co_await db.find_user(req.param<int>("id").value());
    if (!user) {
        req.logger().warn("User not found");
        co_return aevox::Response::not_found("Unknown user");
    }

    req.logger().info("User found: {}", user->name);
    co_return aevox::Response::ok(*user);
});
```

The resulting log entries contain the request ID automatically:

```json
{"timestamp":1234567890123,"level":"INFO","message":"Fetching user 42","request_id":"req-7f3a9b2c","thread_id":7}
```

## Global Logger

For logging outside of request handlers — during startup, shutdown, or in background tasks — use the global logger:

```cpp
aevox::log::global().info("Server starting on port {}", port);
```

The global logger is automatically created and installed when `App::listen()` is called. Before that, it is a no-op.

## Runtime Level Filtering

Set `LogConfig::level` to drop entries below the configured severity:

```cpp
aevox::AppConfig cfg;
cfg.logging.level = aevox::LogLevel::Info;
```

With this configuration, `Trace` and `Debug` entries are ignored before they reach the sinks.

## Automatic Request/Response Logging

Add the logger middleware to record every HTTP transaction:

```cpp
app.use(aevox::middleware::logger());
```

This produces one JSON line per request:

```json
{"timestamp":1234567890123,"level":"INFO","request_id":"req-7f3a9b2c","method":"GET","path":"/users/42","status":200,"duration_ms":12}
```

### Excluding Paths

Skip logging for noisy endpoints:

```cpp
app.use(aevox::middleware::logger({
    .exclude_paths = {"/health", "/metrics"},
}));
```

### Slow Request Warnings

Flag requests that exceed a latency threshold:

```cpp
app.use(aevox::middleware::logger({
    .slow_request_threshold = std::chrono::milliseconds{100},
}));
```

Slow requests emit an additional `WARN` line with the same `request_id`.

## Configuration

### Programmatic

```cpp
aevox::AppConfig config;
config.logging.level = aevox::log::LogLevel::Warn;
config.logging.sinks = {
    aevox::log::FileSinkConfig{
        .path = "/var/log/aevox/app.log",
        .rotate_mb = 100,
        .keep_files = 5,
        .format = aevox::log::LogFormat::JSON,
    },
};
```

### TOML

```toml
[logging]
level = "warn"
ring_buffer_entries = 65536

[[logging.sinks]]
type = "file"
path = "/var/log/aevox/app.log"
format = "json"
rotate_mb = 100
keep_files = 5
```

## Ring Buffer Tuning

The ring buffer size controls how many entries can be queued between the I/O threads and the drain thread. If the buffer fills up, new entries are silently dropped.

| Load profile | Recommended `ring_buffer_entries` |
|--------------|-----------------------------------|
| Development  | 4096                              |
| Low traffic  | 16384                             |
| High traffic | 65536 (default)                   |
| Burst traffic| 262144                            |

Monitor `dropped_count()` on the `LockFreeQueue` in benchmarks to verify the size is adequate.

## Reading Logs

Because sinks output one JSON object per line, standard Unix tools work well:

```bash
# Filter by request ID
jq 'select(.request_id == "req-7f3a9b2c")' /var/log/aevox/app.log

# Find slow requests
jq 'select(.duration_ms > 100)' /var/log/aevox/app.log

# Aggregate error rates
jq -s 'map(select(.level == "ERROR")) | length' /var/log/aevox/app.log
```

## Performance

On a typical Linux workstation:

- `req.logger().info(...)` median latency: **~80 ns** (ring buffer push only)
- Sustained throughput: **>1M entries/sec** across 8 threads
- Drop rate under load: **<0.1%** with default 64K buffer

See `tests/bench/log/` for reproducible benchmarks.

## Distributed Tracing

Aevox automatically extracts the W3C Trace Context `traceparent` header from every inbound request. When a valid header is present, all log lines emitted through `req.logger()` carry `trace_id` and `span_id` fields — no manual instrumentation required.

### Automatic Log Enrichment

When a request arrives with the header:

```
traceparent: 00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01
```

Every `req.logger().*()` call automatically includes the trace fields:

```json
{"timestamp":1234567890123,"level":"INFO","message":"Processing order 42","request_id":"a1b2c3d4e5f6a7b8","trace_id":"4bf92f3577b34da6a3ce929d0e0e4736","span_id":"00f067aa0ba902b7","thread_id":7}
```

When no `traceparent` header is present (or the header is invalid), `trace_id` and `span_id` are omitted from log output entirely.

### Propagating Context to Downstream Services

Use `req.trace_context()` to forward the trace context to outbound HTTP calls:

```cpp
app.get("/orders/{id}", [](aevox::Request& req) -> aevox::Task<aevox::Response> {
    req.logger().info("Processing order {}", req.param<int>("id").value());

    // Forward the traceparent header to downstream services
    if (auto ctx = req.trace_context(); ctx) {
        outbound.set_header("traceparent", *ctx);
    }

    co_return aevox::Response::ok(result);
});
```

The returned `std::string_view` is valid for the lifetime of the `Request` and contains the exact incoming `traceparent` value — Aevox does not modify it.

### request_id Format

Each request receives a unique 16-character lowercase hex identifier (e.g. `"a1b2c3d4e5f6a7b8"`), generated from a per-thread PRNG. This replaces the earlier monotonic counter format (`"req-0"`, `"req-1"`, …).
