# Aevox

**A modern C++23 web framework** designed for developers who need to handle millions of concurrent requests without sacrificing developer experience.

```cpp
#include <aevox/app.hpp>

int main() {
    aevox::App app;

    app.get("/hello", [](aevox::Request& req) {
        return aevox::Response::ok("Hello, World!");
    });

    app.listen(8080);
}
```

## Core Philosophy

- **Zero-cost abstractions** — you pay only for what you use
- **Minimal boilerplate** — a working endpoint in under 5 lines
- **Maximum throughput** — 10M+ req/s target on commodity hardware
- **Modern C++ throughout** — C++23 minimum, no legacy patterns

## Key Features

- Native C++23 coroutines — async feels as natural as sync
- Hybrid thread pool + coroutine execution model
- Composable, testable middleware pipeline
- Built-in WebSocket support
- Structured, type-safe request/response model
- Cross-platform: Linux, macOS, Windows

## What Aevox Is NOT

- Not an HTTP/2 or HTTP/3 server — delegate to Nginx/Caddy
- Not an ORM or database framework
- Not a TLS certificate manager
