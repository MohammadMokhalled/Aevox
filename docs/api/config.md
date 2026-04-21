# API Reference: Configuration

Headers: `<aevox/config.hpp>` (constants and error types), `<aevox/app.hpp>` (AppConfig, App::create, App::config)

The configuration layer provides named `constexpr` defaults, the `AppConfig` and
`ExecutorConfig` structs, and the error types returned by `App::create()` when a TOML
file is loaded.

---

## Constants

All constants live in `namespace aevox`. They serve as both documentation of the default
values and as named arguments when you want to restore a field to its default explicitly.

### AppConfig defaults

| Constant | Type | Value | Description |
|---|---|---|---|
| `kDefaultPort` | `uint16_t` | `8080` | TCP port |
| `kDefaultHost` | `string_view` | `"0.0.0.0"` | Bind address (all interfaces) |
| `kDefaultBacklog` | `int` | `1024` | TCP listen backlog depth |
| `kDefaultMaxBodySize` | `size_t` | `10485760` | Maximum request body in bytes (10 MiB) |
| `kDefaultRequestTimeout` | `chrono::seconds` | `30s` | Per-request idle timeout |
| `kDefaultMaxHeaderCount` | `size_t` | `100` | Maximum headers per request |
| `kDefaultMaxReadBytes` | `size_t` | `65536` | Maximum bytes per TCP `read()` call (64 KiB) |

### ExecutorConfig defaults

| Constant | Type | Value | Description |
|---|---|---|---|
| `kDefaultIoThreadCount` | `size_t` | `0` | I/O worker threads; `0` = `hardware_concurrency()` |
| `kDefaultCpuPoolThreads` | `size_t` | `4` | CPU-bound pool threads for `aevox::pool()` |
| `kDefaultDrainTimeout` | `chrono::seconds` | `30s` | Grace period after `stop()` |

---

## AppConfig

```cpp
// include/aevox/app.hpp
struct AppConfig {
    std::uint16_t          port{kDefaultPort};
    std::string            host{std::string{kDefaultHost}};
    int                    backlog{kDefaultBacklog};
    bool                   reuse_port{true};
    ExecutorConfig         executor{};
    std::size_t            max_body_size{kDefaultMaxBodySize};
    std::chrono::seconds   request_timeout{kDefaultRequestTimeout};
    std::size_t            max_header_count{kDefaultMaxHeaderCount};
    std::size_t            max_read_bytes{kDefaultMaxReadBytes};
};
```

All fields have sensible production defaults. Use C++23 designated initialisers to override
only the fields you care about:

```cpp
#include <aevox/app.hpp>

aevox::App app(aevox::AppConfig{
    .port             = 9090,
    .host             = "127.0.0.1",
    .max_body_size    = 1024 * 1024,   // 1 MiB
    .request_timeout  = std::chrono::seconds{10},
    .executor         = { .thread_count = 4 },
});
```

### Field details

**`port`** — TCP port to bind. Valid TOML range: `1..65535`.

**`host`** — Bind address string. IPv6 is not supported in v0.1. Pass `"127.0.0.1"` to
restrict to loopback. Valid TOML constraint: non-empty string.

**`backlog`** — Depth of the OS-level TCP accept queue. Higher values absorb connection
bursts at the cost of more kernel memory. Valid TOML range: `1..65535`.

**`reuse_port`** — When `true`, enables `SO_REUSEPORT`. Multiple processes on the same
port are load-balanced by the kernel. Requires Linux 3.9+. Not configurable from TOML —
code-only.

**`executor`** — Nested `ExecutorConfig`. Configures the I/O thread pool and the CPU
work pool. See the `ExecutorConfig` section below.

**`max_body_size`** — Maximum request body in bytes. Requests that exceed this size are
rejected with HTTP 413 before the body is buffered in full. Valid TOML range:
`1..2147483648` (2 GiB).

**`request_timeout`** — If a complete HTTP request is not received within this window,
the connection is closed. The timer resets at the start of each request on a keep-alive
connection. Valid TOML range: `1..3600` seconds.

**`max_header_count`** — Requests with more HTTP headers than this value are rejected with
HTTP 431. Matches the Node.js default of 100. Valid TOML range: `1..1000`.

**`max_read_bytes`** — Maximum bytes passed to a single `TcpStream::read()` call.
Increasing reduces syscall frequency for large uploads; decreasing reduces per-connection
memory pressure. Valid TOML range: `512..16777216` (16 MiB).

---

## ExecutorConfig

```cpp
// include/aevox/executor.hpp
struct ExecutorConfig {
    std::size_t           thread_count{kDefaultIoThreadCount};
    std::size_t           cpu_pool_threads{kDefaultCpuPoolThreads};
    std::chrono::seconds  drain_timeout{kDefaultDrainTimeout};
};
```

**`thread_count`** — Number of I/O worker threads. `0` resolves to
`std::max(1u, std::thread::hardware_concurrency())` at executor construction time. Set a
positive value to pin the thread count. TOML range: `0..1024`.

**`cpu_pool_threads`** — Threads in the dedicated CPU work pool. Work submitted via
`aevox::pool(fn)` runs here, keeping CPU-bound work off the I/O threads. Set to `0` to
run `pool()` work on I/O threads instead. TOML range: `0..256`.

**`drain_timeout`** — After `stop()` is called, the executor stops accepting connections
and waits up to this duration for in-flight coroutines to finish. If the timeout expires,
the I/O context is force-stopped. TOML range: `1..3600` seconds.

---

## ConfigError

```cpp
// include/aevox/config.hpp
enum class ConfigError : std::uint8_t {
    file_not_found,   // path does not exist on the filesystem
    parse_error,      // file exists but contains invalid TOML
    invalid_value,    // a field value fails a range or type constraint
};
```

Returned as the error half of `std::expected<App, ConfigErrorDetail>` from `App::create()`.
All three enumerators are non-overlapping. A `switch` with all three cases needs no default.

---

## ConfigErrorDetail

```cpp
// include/aevox/config.hpp
struct ConfigErrorDetail {
    ConfigError  code{ConfigError::file_not_found};
    std::string  message;   // human-readable description
    std::string  key;       // offending TOML key (populated for invalid_value only)
};
```

`code` is the machine-readable discriminant. `message` is always populated with a
human-readable description. `key` is only populated when `code == ConfigError::invalid_value`
and names the TOML key whose value failed validation.

`ConfigErrorDetail` is a value type — safe to copy or move across threads. A moved-from
instance has empty `message` and `key`.

---

## to_string()

```cpp
// include/aevox/config.hpp
[[nodiscard]] std::string_view to_string(ConfigError e) noexcept;
```

Returns a short static description of the error code. The returned `string_view` points
to a string literal — it never dangles and requires no heap allocation.

| Input | Returns |
|---|---|
| `ConfigError::file_not_found` | `"file_not_found"` |
| `ConfigError::parse_error` | `"parse_error"` |
| `ConfigError::invalid_value` | `"invalid_value"` |

```cpp
#include <aevox/config.hpp>
#include <format>
#include <iostream>

void report(const aevox::ConfigErrorDetail& err)
{
    std::cerr << std::format("config error [{}]: {}\n",
                             aevox::to_string(err.code),
                             err.message);
    if (err.code == aevox::ConfigError::invalid_value)
        std::cerr << std::format("  offending key: {}\n", err.key);
}
```

---

## App::create()

```cpp
// include/aevox/app.hpp
[[nodiscard]] static std::expected<App, ConfigErrorDetail>
App::create(AppConfig                       base_config = {},
            std::optional<std::string_view> config_path = std::nullopt) noexcept;
```

Factory function. When `config_path` is `std::nullopt` or empty, behaves identically to
`App(base_config)`. When a path is supplied, reads the file, validates all keys, and
merges present keys over `base_config` fields. Fields absent from the file retain their
`base_config` values.

**Return value**

- On success: `std::expected` containing the constructed `App`.
- On failure: `std::expected` containing a `ConfigErrorDetail` with one of the three
  `ConfigError` codes.

**Preconditions**

- Must be called from the main thread before `listen()`.
- `config_path`, if non-empty, must be a valid UTF-8 filesystem path.

**Error conditions**

| Error code | Condition |
|---|---|
| `file_not_found` | Path does not exist |
| `parse_error` | File exists but is not valid TOML |
| `invalid_value` | A TOML key is present with an out-of-range or wrong-type value |

Unrecognised TOML keys are silently ignored with a warning written to `std::clog` — they
do not produce errors.

```cpp
#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/response.hpp>

#include <format>
#include <iostream>

int main()
{
    auto result = aevox::App::create(
        aevox::AppConfig{ .port = 9090 },   // base: port 9090
        "aevox.toml"                         // file may override everything
    );

    if (!result) {
        const auto& err = result.error();
        std::cerr << std::format("[{}] {}\n",
                                 aevox::to_string(err.code),
                                 err.message);
        return 1;
    }

    auto& app = *result;
    app.get("/", [](aevox::Request&) {
        return aevox::Response::ok("ok");
    });
    app.listen();
}
```

---

## App::config()

```cpp
// include/aevox/app.hpp
[[nodiscard]] const AppConfig& App::config() const noexcept;
```

Returns a const reference to the fully-resolved `AppConfig`. The reference is valid for
the lifetime of the `App`. The config is immutable after construction — no fields change
while the server is running.

Thread-safety: safe to call concurrently from multiple threads after `App::create()` or
`App(AppConfig)` completes.

```cpp
const aevox::AppConfig& cfg = app.config();
std::cout << std::format("port={} io_threads={}\n",
                         cfg.port,
                         cfg.executor.thread_count);
```

---

## TOML key reference

| TOML key | AppConfig field | Valid range |
|---|---|---|
| `port` | `port` | `1..65535` |
| `host` | `host` | non-empty string |
| `backlog` | `backlog` | `1..65535` |
| `max_body_size` | `max_body_size` | `1..2147483648` bytes |
| `request_timeout` | `request_timeout` | `1..3600` seconds |
| `max_header_count` | `max_header_count` | `1..1000` |
| `max_read_bytes` | `max_read_bytes` | `512..16777216` bytes |
| `executor.thread_count` | `executor.thread_count` | `0..1024` |
| `executor.cpu_pool_threads` | `executor.cpu_pool_threads` | `0..256` |
| `executor.drain_timeout` | `executor.drain_timeout` | `1..3600` seconds |

---

## See Also

- [Configuration User Guide](../guide/configuration.md) — practical walkthrough with annotated examples
- [Router and App](router.md) — full `App` class reference including route registration and lifecycle
- [Executor](executor.md) — `ExecutorConfig` and the `Executor` interface
- [Error Handling Guide](../guide/error-handling.md) — the `std::expected` pattern used by `App::create()`
