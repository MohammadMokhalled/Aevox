# Runtime Configuration

Aevox reads its server parameters — port, host, body-size limits, thread counts, and timeouts —
from a plain `AppConfig` struct. Every field has a compiled-in default, so a zero-argument
`App` works immediately. When you need to tune those values without recompiling, pass an
optional path to a TOML file. The file is read once at startup, validated, and merged on top
of any programmatic base values. There is no runtime reloading — the resolved configuration
is fixed for the lifetime of the server.

---

## Pure-defaults usage

The simplest possible server ignores configuration entirely:

```cpp
#include <aevox/app.hpp>
#include <aevox/response.hpp>

int main()
{
    aevox::App app;                     // all defaults: port 8080, 0.0.0.0, 10 MiB body limit
    app.get("/", [](aevox::Request&) {
        return aevox::Response::ok("Hello");
    });
    app.listen();   // blocks until SIGINT/SIGTERM
}
```

When you want to override individual fields without a file, use C++23 designated initialisers:

```cpp
aevox::App app(aevox::AppConfig{
    .port     = 9090,
    .host     = "127.0.0.1",
    .executor = { .thread_count = 8 },
});
app.listen();
```

---

## Loading a TOML config file

`App::create()` is the factory to use when a config file may be present. It returns
`std::expected<App, ConfigErrorDetail>` — always check the result before using the App.

```cpp
#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/response.hpp>

#include <format>
#include <iostream>

int main(int argc, char* argv[])
{
    // Optional path from the command line
    std::optional<std::string_view> config_path;
    if (argc >= 2)
        config_path = argv[1];

    // App::create() merges file values over base_config defaults.
    // With no path (or std::nullopt) it behaves identically to App(AppConfig{}).
    auto result = aevox::App::create({}, config_path);

    // Always handle the error branch — never discard the expected
    if (!result) {
        const auto& err = result.error();
        std::cerr << std::format("config error ({}): {}\n",
                                 aevox::to_string(err.code),
                                 err.message);
        return 1;
    }

    auto& app = *result;
    app.get("/", [](aevox::Request&) {
        return aevox::Response::ok("running");
    });
    app.listen();
}
```

`App::create()` is `[[nodiscard]]`. The compiler warns if you discard the result without
checking it.

---

## The AppConfig struct

`AppConfig` holds every tunable server parameter. All fields have named `constexpr` defaults
defined in `<aevox/config.hpp>`:

| Field | Type | Default | Description |
|---|---|---|---|
| `port` | `uint16_t` | `8080` | TCP port to bind |
| `host` | `std::string` | `"0.0.0.0"` | Bind address — all interfaces by default |
| `backlog` | `int` | `1024` | TCP listen backlog depth |
| `reuse_port` | `bool` | `true` | Enable `SO_REUSEPORT` for multi-process setups |
| `max_body_size` | `size_t` | `10485760` (10 MiB) | Maximum request body in bytes; excess triggers HTTP 413 |
| `request_timeout` | `chrono::seconds` | `30s` | Drop idle connections after this interval |
| `max_header_count` | `size_t` | `100` | Maximum headers per request; excess triggers HTTP 431 |
| `max_read_bytes` | `size_t` | `65536` (64 KiB) | Maximum bytes per TCP `read()` call |
| `executor` | `ExecutorConfig` | see below | Thread pool settings |

`ExecutorConfig` is nested inside `AppConfig` via the `executor` field:

| Field | Type | Default | Description |
|---|---|---|---|
| `thread_count` | `size_t` | `0` (auto) | I/O worker threads; `0` uses `hardware_concurrency()` |
| `cpu_pool_threads` | `size_t` | `4` | CPU-bound pool threads for `aevox::pool()` |
| `drain_timeout` | `chrono::seconds` | `30s` | Grace period after `stop()` before force-shutdown |

---

## The aevox.toml format

An example file with every supported key and its valid range:

```toml
# aevox.toml — all fields are optional
# Absent fields retain their compiled-in defaults

port = 9090                # 1..65535
host = "0.0.0.0"           # non-empty string
backlog = 512              # 1..65535

# Maximum request body size (bytes)
max_body_size = 1048576    # 1..2147483648  (1 MiB shown)

# Maximum bytes read per TCP read() call
max_read_bytes = 16384     # 512..16777216  (16 KiB shown)

# Maximum HTTP headers per request
max_header_count = 50      # 1..1000

# Seconds before an idle request is dropped
request_timeout = 10       # 1..3600

[executor]
# 0 = auto-detect via hardware_concurrency()
thread_count = 0           # 0..1024

# Threads in the CPU-bound work pool (aevox::pool())
cpu_pool_threads = 2       # 0..256

# Seconds to wait for in-flight requests after stop()
drain_timeout = 5          # 1..3600
```

Every key is optional. Unrecognised keys are silently ignored with a warning written to
`std::clog`. The file is read and parsed exactly once at `App::create()` time.

---

## Merging base config with file

`App::create()` accepts a `base_config` argument that acts as the starting point. The file
overrides only the keys that are present in it. This is useful when you need to enforce a
programmatic minimum while still allowing file-level tuning:

```cpp
#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/response.hpp>

#include <format>
#include <iostream>

int main()
{
    // Enforce a loopback-only bind regardless of what the file says.
    // The file can still override port, body limits, thread counts, etc.
    aevox::AppConfig base;
    base.host = "127.0.0.1";

    auto result = aevox::App::create(base, "aevox.toml");
    if (!result) {
        const auto& err = result.error();
        std::cerr << std::format("config error ({}): {}\n",
                                 aevox::to_string(err.code),
                                 err.message);
        return 1;
    }

    auto& app = *result;
    app.get("/", [](aevox::Request&) {
        return aevox::Response::ok("loopback only");
    });
    app.listen();
}
```

If `aevox.toml` also contains `host = "0.0.0.0"`, that value wins because file values
replace their corresponding base fields. To enforce a field unconditionally, apply it
after `create()` returns — but note that `AppConfig` is immutable after construction.
Use a programmatic base when a field must not be overridden.

---

## Inspecting resolved config

After `App::create()` (or the `App(AppConfig)` constructor), call `app.config()` to read
back the fully-resolved parameters:

```cpp
#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/response.hpp>

#include <format>
#include <iostream>

int main()
{
    auto result = aevox::App::create({}, "aevox.toml");
    if (!result) {
        const auto& err = result.error();
        std::cerr << std::format("config error ({}): {}\n",
                                 aevox::to_string(err.code),
                                 err.message);
        return 1;
    }

    auto& app = *result;
    const aevox::AppConfig& cfg = app.config();

    std::cout << std::format("listening on {}:{}\n", cfg.host, cfg.port);
    std::cout << std::format("  max_body_size={}  io_threads={}\n",
                             cfg.max_body_size, cfg.executor.thread_count);

    app.get("/", [](aevox::Request&) {
        return aevox::Response::ok("ok");
    });
    app.listen();
}
```

`app.config()` returns a `const AppConfig&` valid for the lifetime of the `App`. The
config is immutable after construction — no fields can be changed while the server is running.

---

## Error types

`App::create()` returns one of three error codes when it fails:

| Code | When |
|---|---|
| `ConfigError::file_not_found` | The path supplied does not exist on the filesystem |
| `ConfigError::parse_error` | The file exists but is not valid TOML |
| `ConfigError::invalid_value` | A TOML key is present but its value fails a range or type check |

`aevox::to_string(ConfigError)` returns a short static description. For `invalid_value`,
`ConfigErrorDetail::key` names the offending TOML key:

```cpp
#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/response.hpp>

#include <format>
#include <iostream>

int main()
{
    auto result = aevox::App::create({}, "aevox.toml");
    if (!result) {
        const aevox::ConfigErrorDetail& err = result.error();

        switch (err.code) {
        case aevox::ConfigError::file_not_found:
            std::cerr << "config file not found — using compiled defaults\n";
            // Fall through to construct without a file
            break;

        case aevox::ConfigError::parse_error:
            std::cerr << std::format("TOML parse error: {}\n", err.message);
            return 1;

        case aevox::ConfigError::invalid_value:
            std::cerr << std::format("invalid value for key '{}': {}\n",
                                     err.key, err.message);
            return 1;
        }

        // Retry without the file when the file is simply absent
        auto fallback = aevox::App::create({}, std::nullopt);
        if (!fallback) return 1;    // should never fail without a file

        auto& app = *fallback;
        app.get("/", [](aevox::Request&) {
            return aevox::Response::ok("ok");
        });
        app.listen();
        return 0;
    }

    auto& app = *result;
    app.get("/", [](aevox::Request&) {
        return aevox::Response::ok("ok");
    });
    app.listen();
}
```

---

## See Also

- [Router and App API Reference](../api/router.md) — full `App` and `Router` symbol reference
- [Configuration API Reference](../api/config.md) — every constant, enum, and struct in `<aevox/config.hpp>`
- [Error Handling](error-handling.md) — the `std::expected` error model used throughout Aevox
- [First HTTP Server](first-http-server.md) — end-to-end walkthrough without configuration complexity
