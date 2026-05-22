# Configured Server

Demonstrates `App::create()` with an optional TOML configuration file. Every field in the file overrides the compiled-in default; omitted fields retain their defaults automatically.

**Source:** [`examples/configured-server/main.cpp`](https://github.com/MohammadMokhalled/Aevox/blob/main/examples/configured-server/main.cpp)

---

## Build and run

```bash
export VCPKG_ROOT=$HOME/vcpkg
cmake --preset default
cmake --build --preset default --target configured-server
```

Run with defaults (no file):

```bash
./build/debug/examples/configured-server/configured-server
```

Run with a TOML file:

```bash
./build/debug/examples/configured-server/configured-server examples/configured-server/aevox.toml
```

---

## What this covers

- **`aevox::App::create(defaults, config_path)`** — returns `std::expected<App, ConfigErrorDetail>`
- **Pure defaults** — when no file is provided, every field uses the compiled-in default
- **Partial overrides** — any field present in the file replaces the default; everything else stays unchanged
- **Graceful failure** — a missing or malformed file is reported via `ConfigErrorDetail`, not an exception

---

## Sample TOML file

```toml
port = 9090
host = "0.0.0.0"
backlog = 512
max_body_size = 1048576   # 1 MiB
max_read_bytes = 16384    # 16 KiB
max_header_count = 50
request_timeout = 10

[executor]
thread_count = 0          # 0 = auto-detect
cpu_pool_threads = 2
drain_timeout = 5
```

---

## Full source

```cpp
#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/response.hpp>

#include <format>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[])
{
    std::optional<std::string_view> config_path;
    if (argc >= 2)
        config_path = argv[1];

    auto result = aevox::App::create({}, config_path);
    if (!result) {
        const auto& err = result.error();
        std::cerr << std::format("[configured-server] config error ({}): {}\n",
                                 aevox::to_string(err.error_code()), err.error_message());
        return 1;
    }

    auto&       app = *result;
    const auto& cfg = app.config();

    std::cout << std::format("[configured-server] listening on {}:{}\n", cfg.host, cfg.port);

    app.get("/",
            [](aevox::Request&) { return aevox::Response::ok("configured-server is running"); });

    app.get("/config", [&cfg](aevox::Request&) {
        auto body = std::format("port={} host={} max_body_size={} max_read_bytes={} "
                                "max_header_count={} request_timeout={}s "
                                "io_threads={} cpu_pool_threads={} drain_timeout={}s",
                                cfg.port, cfg.host, cfg.max_body_size, cfg.max_read_bytes,
                                cfg.max_header_count, cfg.request_timeout.count(),
                                cfg.executor.thread_count, cfg.executor.cpu_pool_threads,
                                cfg.executor.drain_timeout.count());
        return aevox::Response::ok(body);
    });

    app.listen();
}
```

---

## API reference

| Type | Header | Docs |
|---|---|---|
| `aevox::App::create()` | `<aevox/app.hpp>` | [Router and App](../api/router.md) |
| `aevox::AppConfig` | `<aevox/config.hpp>` | [Configuration](../api/config.md) |
| `aevox::ConfigErrorDetail` | `<aevox/config.hpp>` | [Configuration](../api/config.md) |

---

## See Also

- [User Guide — Configuration](../guide/configuration.md) — full guide to runtime configuration
- [Hello World example](hello-world.md) — the simplest possible Aevox server
