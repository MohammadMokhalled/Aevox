# Middleware Plugin

Demonstrates how to write, register, and compose middleware in Aevox. Two middlewares are shown: a global logger and a scoped auth guard.

**Source:** [`examples/middleware-plugin/main.cpp`](https://github.com/MohammadMokhalled/Aevox/blob/main/examples/middleware-plugin/main.cpp)

---

## Build and run

```bash
export VCPKG_ROOT=$HOME/vcpkg
cmake --preset default
cmake --build --preset default --target middleware-plugin
./build/debug/examples/middleware-plugin/middleware-plugin
```

The server listens on port 8080. Stop it with `Ctrl-C`.

---

## Routes

| Method | Path | Middleware | Response |
|---|---|---|---|
| `GET` | `/` | logger only | `200 Welcome to the middleware-plugin example.` |
| `GET` | `/health` | logger only | `200 ok` |
| `GET` | `/api/data` | logger + auth | `200 Protected data. Auth check passed.` or `401` |

---

## Test it

```bash
# Public route — no auth required
curl http://localhost:8080/

# Health check — no auth required
curl http://localhost:8080/health

# Protected route — missing header → 401
curl http://localhost:8080/api/data

# Protected route — valid header → 200
curl -H "Authorization: secret-token-123" http://localhost:8080/api/data
```

---

## Full source

```cpp
#include <aevox/app.hpp>

#include <format>
#include <iostream>
#include <string_view>

constexpr std::string_view kExpectedToken = "secret-token-123";

struct AuthMiddleware
{
    std::string required_token;

    [[nodiscard]] aevox::Task<aevox::Response> operator()(aevox::Request& req, auto next) const
    {
        auto auth = req.header("Authorization");
        if (!auth || *auth != required_token) {
            co_return aevox::Response::unauthorized("Missing or invalid Authorization token");
        }
        co_return co_await next(req);
    }
};

int main()
{
    aevox::App app;

    // Global logger — runs for every request
    app.use([](aevox::Request& req, auto next) -> aevox::Task<aevox::Response> {
        std::cout << std::format("[logger] {} {}\n", aevox::to_string(req.method()), req.path());
        auto res = co_await next(req);
        std::cout << std::format("[logger] -> {}\n", res.status_code());
        co_return res;
    });

    // Scoped auth guard — runs only for paths starting with /api
    app.use("/api", AuthMiddleware{.required_token = std::string{kExpectedToken}});

    app.get("/", [](aevox::Request&) {
        return aevox::Response::ok("Welcome to the middleware-plugin example.");
    });

    app.get("/api/data", [](aevox::Request&) {
        return aevox::Response::ok("Protected data. Auth check passed.");
    });

    app.get("/health", [](aevox::Request&) { return aevox::Response::ok("ok"); });

    app.listen(8080);
}
```

---

## What this covers

- **`app.use(middleware)`** — registers a global middleware (onion wrapper for every route)
- **`app.use(prefix, middleware)`** — registers a scoped middleware that only runs when the request path starts with `prefix`
- **Onion execution order** — pre-logic runs outer-to-inner, post-logic runs inner-to-outer
- **Short-circuiting** — a middleware can return a response without calling `next(req)`
- **Stateful middleware** — `AuthMiddleware` holds per-instance configuration (`required_token`)
- **Coroutine middleware signatures** — `Task<Response>(Request&, auto next)`

---

## API reference

| Type | Header | Docs |
|---|---|---|
| `aevox::MiddlewareFn` | `<aevox/middleware.hpp>` | [Middleware](../api/middleware.md) |
| `aevox::App::use()` | `<aevox/app.hpp>` | [Router and App](../api/router.md) |

---

## See Also

- [User Guide — Middleware](../guide/middleware.md) — deeper explanation of the middleware pipeline
- [Hello World example](hello-world.md) — the simplest possible Aevox server
