# Hello World

The v0.1 milestone example. Demonstrates the complete Aevox public API —
`App`, `Router`, `Request`, `Response` — in a single file under 25 lines of
application code.

**Source:** [`examples/hello-world/main.cpp`](https://github.com/MohammadMokhalled/Aevox/blob/main/examples/hello-world/main.cpp)

---

## Build and run

```bash
export VCPKG_ROOT=$HOME/vcpkg
cmake --preset default
cmake --build --preset default --target hello-world
./build/debug/examples/hello-world/hello-world
```

The server listens on port 8080. Stop it with `Ctrl-C`.

---

## Routes

| Method | Path | Response |
|---|---|---|
| `GET` | `/` | `200 Hello, World!` |
| `GET` | `/hello/{name}` | `200 Hello, <name>!` |
| `GET` | `/health` | `200 ok` |
| any | anything else | `404 Not Found` |

---

## Test it

```bash
curl http://localhost:8080/
curl http://localhost:8080/hello/Alice
curl http://localhost:8080/health
curl http://localhost:8080/missing
```

---

## Full source

```cpp
#include <aevox/app.hpp>

#include <format>
#include <string_view>

int main()
{
    aevox::App app;

    app.get("/", [](aevox::Request&) { return aevox::Response::ok("Hello, World!"); });

    app.get("/hello/{name}", [](aevox::Request& req) {
        auto name = req.param<std::string_view>("name").value_or("stranger");
        return aevox::Response::ok(std::format("Hello, {}!", name));
    });

    app.get("/health", [](aevox::Request&) { return aevox::Response::ok("ok"); });

    app.listen(8080);
}
```

---

## What this covers

- **`aevox::App`** — default construction and `listen(port)`
- **`app.get(pattern, handler)`** — registering sync handlers
- **Static routes** — `/` and `/health`
- **Named path parameters** — `/hello/{name}` captured as `std::string_view`
- **`Request::param<T>(name)`** — typed parameter extraction with `.value_or()`
- **`Response::ok(body)`** — 200 text/plain response factory
- **Automatic 404** — unmatched routes handled by the router
- **Clean shutdown** — SIGINT triggers graceful drain via `App::listen()`

---

## API reference

| Type | Header | Docs |
|---|---|---|
| `aevox::App` / `AppConfig` | `<aevox/app.hpp>` | [Router and App](../api/router.md) |
| `aevox::Request` | `<aevox/request.hpp>` | [Request and Response](../api/request-response.md) |
| `aevox::Response` | `<aevox/response.hpp>` | [Request and Response](../api/request-response.md) |

---

## See Also

- [User Guide — First HTTP Server](../guide/first-http-server.md) — deeper walkthrough of the same example with full error handling
- [User Guide — Routing](../guide/routing.md) — all route pattern types, groups, and match priority
