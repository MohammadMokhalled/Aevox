# Middleware Guide

Middleware allows you to intercept and process requests before they reach route handlers, and responses before they're sent to clients.

## Basic Middleware

The simplest middleware just passes the request through:

```cpp
#include <aevox/app.hpp>

int main() {
    aevox::App app;
    
    app.use([](aevox::Request& req,
               std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
        -> aevox::Task<aevox::Response> {
        // Do something before the handler
        std::cout << "Incoming: " << req.path() << "\n";
        
        // Call the next middleware or handler
        auto res = co_await next(req);
        
        // Do something with the response
        std::cout << "Outgoing: " << res.status_code() << "\n";
        co_return res;
    });
    
    app.get("/hello", [](aevox::Request&) {
        return aevox::Response::ok("Hello!");
    });
    
    app.listen(8080);
}
```

## Authentication

Check for an authorization header and prevent access if missing:

```cpp
app.use([](aevox::Request& req,
           std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
    -> aevox::Task<aevox::Response> {
    auto auth = req.header("Authorization");
    if (!auth) {
        co_return aevox::Response::unauthorized("Missing auth header");
    }
    
    // Store the user info in the request context for handlers to access
    req.set("auth_token", *auth);
    co_return co_await next(req);
});

app.get("/protected", [](aevox::Request& req) {
    if (auto token = req.get<std::string>("auth_token")) {
        return aevox::Response::ok("Authenticated!");
    }
    return aevox::Response::unauthorized();
});
```

## CORS Headers

Add CORS headers to all responses:

```cpp
app.use([](aevox::Request& req,
           std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
    -> aevox::Task<aevox::Response> {
    auto res = co_await next(req);
    
    // Add CORS headers
    res = std::move(res)
        .header("Access-Control-Allow-Origin", "*")
        .header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE")
        .header("Access-Control-Allow-Headers", "Content-Type");
    
    co_return res;
});
```

## Logging

Use the built-in logger middleware to log requests and responses:

```cpp
app.use(aevox::middleware::logger({
    .exclude_paths = {"/health"},
}));
```

## Scoped Middleware

Apply middleware only to specific paths:

```cpp
// This middleware runs on all requests
app.use([](aevox::Request& req,
           std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
    -> aevox::Task<aevox::Response> {
    std::cout << "Global: " << req.path() << "\n";
    co_return co_await next(req);
});

// This middleware runs only on /api/...
app.use("/api", [](aevox::Request& req,
                   std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
    -> aevox::Task<aevox::Response> {
    std::cout << "API scoped: " << req.path() << "\n";
    co_return co_await next(req);
});

app.get("/hello", [](aevox::Request&) {
    return aevox::Response::ok("Hello!");
});

app.get("/api/users", [](aevox::Request&) {
    return aevox::Response::ok("Users!");
});
```

Output:
```
GET /hello
  Global: /hello
GET /api/users
  Global: /api/users
  API scoped: /api/users
```

## Request Filtering

Reject requests that don't match your requirements:

```cpp
app.use([](aevox::Request& req,
           std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
    -> aevox::Task<aevox::Response> {
    // Only allow GET and POST
    auto method = req.method();
    if (method != aevox::HttpMethod::GET && method != aevox::HttpMethod::POST) {
        co_return aevox::Response::method_not_allowed()
            .header("Allow", "GET, POST");
    }
    
    co_return co_await next(req);
});
```

## Execution Order

Middleware executes in an "onion" pattern — the outermost middleware runs first, then each inner middleware, then the handler, then each middleware runs its cleanup code in reverse order:

```cpp
app.use([](auto& req, auto next) -> auto {
    std::cout << "A: before\n";
    auto res = co_await next(req);
    std::cout << "A: after\n";
    co_return res;
});

app.use([](auto& req, auto next) -> auto {
    std::cout << "B: before\n";
    auto res = co_await next(req);
    std::cout << "B: after\n";
    co_return res;
});

app.get("/", [](auto&) {
    std::cout << "Handler\n";
    return aevox::Response::ok("OK");
});
```

Output:
```
A: before
B: before
Handler
B: after
A: after
```

## See Also

- [Middleware API Reference](../api/middleware.md) — complete API documentation
- [Request and Response Guide](request-response.md) — request inspection, context, and response construction
