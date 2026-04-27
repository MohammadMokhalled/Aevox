# Middleware API Reference

Aevox provides a composable async middleware pipeline for request and response interception.

## Concepts

### MiddlewareNext

A callable with signature:
```cpp
std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)>
```

Represents the "next" handler in the chain — either the next middleware or the final route handler. Must be invoked via `co_await` in a coroutine context.

### MiddlewareFn

Any callable matching the signature:
```cpp
[](aevox::Request& req, std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next) 
    -> aevox::Task<aevox::Response>
```

Examples:
- Lambda (any capture)
- Function pointer
- Functor (with `operator()`)
- Any move-only callable

## Registration

### Global Middleware

Register middleware that runs before **every** route handler:

```cpp
aevox::App app;
app.use([](aevox::Request& req,
           std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
    -> aevox::Task<aevox::Response> {
    // Runs before every handler
    auto res = co_await next(req);
    // Runs after the handler
    co_return res;
});
```

### Scoped Middleware

Register middleware for a path prefix:

```cpp
app.use("/api", [](aevox::Request& req,
                   std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
    -> aevox::Task<aevox::Response> {
    // Runs only for /api/...
    co_return co_await next(req);
});
```

Execution order: `global → global → scoped ("/api") → handler → scoped ("/api") → global → global` (onion model).

## Per-Request Context

Store and retrieve typed values in the request context bag:

```cpp
// Middleware stores a value
app.use([](aevox::Request& req,
           std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
    -> aevox::Task<aevox::Response> {
    req.set("user_id", std::string("user123"));
    co_return co_await next(req);
});

// Handler retrieves the value
app.get("/profile", [](aevox::Request& req) {
    if (auto user_id = req.get<std::string>("user_id")) {
        return aevox::Response::ok("Profile for " + *user_id);
    }
    return aevox::Response::unauthorized();
});
```

- `set<T>(key, value)` — stores a typed value
- `get<T>(key)` — returns `std::optional<T>`, or `std::nullopt` if key absent or type mismatch
- Exact type match is required; no implicit conversions

## Short-Circuiting

Middleware can prevent further execution by not calling `next()`:

```cpp
app.use([](aevox::Request& req,
           std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
    -> aevox::Task<aevox::Response> {
    auto auth = req.header("Authorization");
    if (!auth) {
        // Short-circuit: don't call next(), return response directly
        co_return aevox::Response::unauthorized("Missing Authorization header");
    }
    co_return co_await next(req);
});
```

## Request/Response Modification

### Modify Request Before Handler

```cpp
app.use([](aevox::Request& req,
           std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
    -> aevox::Task<aevox::Response> {
    req.set("request_start_time", std::chrono::system_clock::now());
    co_return co_await next(req);
});
```

### Modify Response After Handler

```cpp
app.use([](aevox::Request& req,
           std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
    -> aevox::Task<aevox::Response> {
    auto res = co_await next(req);
    res = std::move(res).header("X-Powered-By", "Aevox");
    co_return res;
});
```

## Performance

- **Zero overhead**: Apps with no middleware have identical performance to direct dispatch
- **Composable**: Middleware is applied at connection time; no per-request overhead for chain construction
- **Memory efficient**: Middleware state is stored inline in the handler chain; no heap allocations per middleware

## See Also

- [Middleware Usage Guide](../guide/middleware.md) — practical patterns and examples
- [App API Reference](app.md) — top-level `App` class
- [Request API Reference](request.md) — request inspection and context
- [Response API Reference](response.md) — response construction and headers
