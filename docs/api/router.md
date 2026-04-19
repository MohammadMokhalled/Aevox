# Router and App

`aevox::Router` and `aevox::App` are the primary public API for registering HTTP route handlers and running the server.

---

## Quick Start

```cpp
#include <aevox/app.hpp>

int main() {
    aevox::App app;

    app.get("/hello", [](aevox::Request& req) {
        return aevox::Response::ok("Hello, World!");
    });

    app.get("/users/{id:int}", [](aevox::Request& req, int id) {
        return aevox::Response::ok("User #" + std::to_string(id));
    });

    app.listen(8080);  // blocks until SIGINT/SIGTERM
}
```

---

## AppConfig

| Field | Type | Default | Description |
|---|---|---|---|
| `port` | `uint16_t` | `8080` | TCP port to bind |
| `host` | `std::string` | `"0.0.0.0"` | Bind address |
| `backlog` | `int` | `1024` | TCP listen backlog |
| `reuse_port` | `bool` | `true` | Enable `SO_REUSEPORT` |
| `executor` | `ExecutorConfig` | `{}` | Thread pool configuration |
| `max_body_size` | `std::size_t` | `10 MiB` | Maximum request body size |
| `request_timeout` | `std::chrono::seconds` | `30s` | Per-request timeout |

---

## App API

| Method | Description |
|---|---|
| `App(AppConfig)` | Construct with configuration (port binding deferred to `listen()`) |
| `get(pattern, handler)` | Register a GET handler |
| `post(pattern, handler)` | Register a POST handler |
| `put(pattern, handler)` | Register a PUT handler |
| `patch(pattern, handler)` | Register a PATCH handler |
| `del(pattern, handler)` | Register a DELETE handler (`delete` is a reserved keyword) |
| `options(pattern, handler)` | Register an OPTIONS handler |
| `group(prefix)` | Returns a child `Router` scoped to a path prefix |
| `router()` | Returns the internal `Router` by reference |
| `listen(port)` | Binds to `port` and blocks until stopped |
| `listen()` | Binds to `AppConfig::port` |
| `stop()` | Thread-safe. Signals the executor to stop and drain |

---

## Router API

| Method | Description |
|---|---|
| `get/post/put/patch/del/options(pattern, handler)` | Register a method handler |
| `group(prefix)` | Returns a child `Router` for prefix-scoped registration |
| `dispatch(req)` | Walks the trie and invokes the matching handler (thread-safe) |
| `valid()` | Returns `false` if the Router has been moved from |

---

## Route Patterns

Three segment types are supported:

| Syntax | Type | Example |
|---|---|---|
| `/literal` | Static | `/users`, `/api/v1/status` |
| `/{name}` or `/{name:string}` | String parameter | `/users/{id}` |
| `/{name:int}` | Typed int parameter | `/items/{count:int}` |
| `/{name:uint}` | Typed unsigned int | `/pages/{page:uint}` |
| `/{name:float}` | Typed float | `/scale/{factor:float}` |
| `/{name:double}` | Typed double | `/coords/{lat:double}` |
| `/{name...}` | Wildcard tail | `/files/{path...}` |

Patterns must start with `/`. A wildcard may only appear as the final segment. Regex routing is not available in v0.1 (ADR-4).

**Match priority** at each level: static > named parameter > wildcard.

---

## Supported Handler Signatures

All of the following are accepted by `get()`, `post()`, etc.:

```cpp
// Arity 0 — synchronous
[](aevox::Request& req) -> aevox::Response { ... }

// Arity 0 — asynchronous
[](aevox::Request& req) -> aevox::Task<aevox::Response> { co_return ...; }

// Arity 1 — typed parameter (T ∈ {int, unsigned int, float, double, std::string})
[](aevox::Request& req, T value) -> aevox::Response { ... }

// Arity 2 — two typed parameters
[](aevox::Request& req, T0 a, T1 b) -> aevox::Response { ... }
```

> **Note:** `std::function` requires `CopyConstructible` captures. Handlers that capture `std::unique_ptr` or other move-only types must wrap them in `std::shared_ptr`. This limitation is tracked in AEV-015.

---

## Error Responses

| Condition | HTTP Status | Trigger |
|---|---|---|
| No path match | 404 Not Found | No trie node reached for the request path |
| Path match, method mismatch | 405 Method Not Allowed | Trie node reached but no handler for the method; `Allow` header set |
| Typed parameter conversion failure | 400 Bad Request | `from_chars` conversion fails for a typed parameter |
| Malformed request body | 400 Bad Request | llhttp parser returns a fatal error |

---

## Route Groups

```cpp
aevox::App app;
auto api = app.group("/api/v1");
api.get("/users",    handler_a);
api.post("/users",   handler_b);
api.get("/products", handler_c);
// Registers: GET /api/v1/users, POST /api/v1/users, GET /api/v1/products
```

> **Lifetime:** The child `Router` returned by `group()` holds a raw pointer into the parent's trie. It must not outlive the `App`. Use child routers only during the registration phase (before `listen()`).

---

## RouteError

`aevox::RouteError` is an enum for test introspection. The dispatch path never throws — it always produces a `Response`.

| Value | Meaning |
|---|---|
| `NotFound` | No route matched → 404 |
| `MethodNotAllowed` | Path matched, method did not → 405 |
| `BadParam` | Typed parameter conversion failed → 400 |
