# Examples

Runnable applications that demonstrate the Aevox public API. Each example is a
self-contained binary under `examples/` in the repository.

## Available examples

| Example | What it shows |
|---|---|
| [Hello World](hello-world.md) | Static routes, named path parameters, clean shutdown — the complete v0.1 API in one file |
| [Configured Server](configured-server.md) | `App::create()` with an optional TOML config file — partial overrides and graceful error handling |
| [Middleware Plugin](middleware-plugin.md) | Global and scoped middleware composition — logging, auth guards, and short-circuiting |
| [Static Files](static-files.md) | Serving bundled application assets from an explicit filesystem root and URL prefix |
| [WebSocket Chat](websocket-chat.md) | Room-based WebSocket pub/sub broadcast with lifecycle callbacks |

## Building examples

Examples are opt-in and are built by the validation presets:

=== "Linux"
    ```bash
    export VCPKG_ROOT=$HOME/vcpkg
    cmake --preset default-tests
    cmake --build --preset default-tests
    ```

=== "Windows"
    ```bash
    cmake --preset windows-msvc-tests
    cmake --build --preset windows-msvc-tests-debug
    ```

---

## See Also

- [User Guide — First HTTP Server](../guide/first-http-server.md) — a guided walkthrough of the Hello World example
- [Getting Started](../getting-started.md) — fast-path setup and a TCP echo server example
