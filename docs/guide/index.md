# User Guide

This guide walks you through every feature of Aevox step by step, assuming you know C++23 but have never used Aevox before. Each page introduces one concept with a plain explanation followed immediately by a runnable code example.

## Pages in This Guide

| Page | What it covers |
|---|---|
| [Installation](installation.md) | Prerequisites, cloning the repo, configuring with CMake and vcpkg, building, and running the test suite |
| [First HTTP Server](first-http-server.md) | A full walkthrough from an empty file to a running HTTP server with three routes |
| [Routing](routing.md) | Route patterns, HTTP methods, route groups, match priority, and error responses |
| [Request and Response](request-response.md) | Reading method, path, headers, body, and path parameters; building responses with factory methods |
| [Async Patterns](async-patterns.md) | Writing coroutine handlers, offloading CPU work, non-blocking timers, concurrent fan-out, and graceful shutdown |
| [Error Handling](error-handling.md) | The `std::expected` error model, Aevox error types, propagating errors in coroutines, and diagnostics |
| [Configuration](configuration.md) | Runtime configuration via `AppConfig` and an optional TOML file — ports, limits, thread counts, and error handling |

## How to Use This Guide

Read the pages in order if you are new to Aevox — each page builds on the previous one. If you are looking for a specific topic, jump directly to the relevant page using the table above.

This guide focuses on practical usage. For complete symbol-level documentation — every method signature, every parameter, every error code — see the [API Reference](../api/index.md).

## See Also

- [Getting Started](../getting-started.md) — a fast-path TCP echo server example to get something running in minutes
- [API Reference](../api/index.md) — complete reference for every public symbol
