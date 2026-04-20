# Routing

The router matches incoming HTTP requests to handler functions. You register handlers with `app.get()`, `app.post()`, and related methods before calling `app.listen()`. This page covers all routing features.

## Route Patterns

A route pattern is a string starting with `/`. Segments separated by `/` are matched left to right against the incoming request path.

| Syntax | Type | Example pattern | Matches |
|---|---|---|---|
| `/literal` | Static | `/users` | Only the exact path `/users` |
| `/{name}` or `/{name:string}` | String parameter | `/users/{id}` | `/users/alice`, `/users/42` |
| `/{name:int}` | Integer parameter | `/items/{count:int}` | `/items/10` (not `/items/abc`) |
| `/{name:uint}` | Unsigned integer | `/pages/{page:uint}` | `/pages/3` (not `/pages/-1`) |
| `/{name:float}` | Float parameter | `/scale/{factor:float}` | `/scale/1.5` |
| `/{name:double}` | Double parameter | `/coords/{lat:double}` | `/coords/51.5074` |
| `/{name...}` | Wildcard tail | `/files/{path...}` | `/files/a/b/c.txt` |

```cpp
// Static segment
app.get("/status", [](aevox::Request&) {
    return aevox::Response::ok("running");
});

// String parameter — captured as std::string_view
app.get("/users/{id}", [](aevox::Request& req) {
    auto id = req.param<std::string_view>("id").value_or("unknown");
    return aevox::Response::ok(std::format("User: {}", id));
});

// Typed integer parameter
app.get("/items/{count:int}", [](aevox::Request& req, int count) {
    return aevox::Response::ok(std::format("Count: {}", count));
});

// Wildcard tail — captures everything after /files/
app.get("/files/{path...}", [](aevox::Request& req) {
    auto path = req.param<std::string_view>("path").value_or("");
    return aevox::Response::ok(std::format("File: {}", path));
});
```

Regex routing is not available in v0.1. Patterns must not contain regular expression syntax.

## HTTP Methods

Register handlers for each HTTP method using the corresponding method on `App`:

```cpp
app.get(    "/resource", handler);   // GET
app.post(   "/resource", handler);   // POST
app.put(    "/resource", handler);   // PUT
app.patch(  "/resource", handler);   // PATCH
app.del(    "/resource", handler);   // DELETE (note: del, not delete)
app.options("/resource", handler);   // OPTIONS
```

`del` is used instead of `delete` because `delete` is a reserved C++ keyword.

## Route Groups

A route group applies a shared path prefix to a set of routes. Use `app.group(prefix)` to create one:

```cpp
aevox::App app;
auto api = app.group("/api/v1");

api.get("/users",    [](aevox::Request&) { return aevox::Response::ok("list users"); });
api.post("/users",   [](aevox::Request&) { return aevox::Response::ok("create user"); });
api.get("/products", [](aevox::Request&) { return aevox::Response::ok("list products"); });
// Registers: GET /api/v1/users, POST /api/v1/users, GET /api/v1/products

app.listen(8080);
```

The child router returned by `group()` holds a reference into the parent's trie. It must not outlive the `App` that created it. Use route groups only during the registration phase, before calling `listen()`.

## Match Priority

When multiple route patterns could match the same path, the router picks the most specific match. At each path segment, priority is:

1. Static segments first
2. Named parameters second
3. Wildcard last

Example: suppose you register both `/users/me` and `/users/{id}`. A request to `/GET /users/me` matches the static route `/users/me` — not the parameter route — because static segments have higher priority.

```cpp
app.get("/users/me",   [](aevox::Request&) { return aevox::Response::ok("current user"); });
app.get("/users/{id}", [](aevox::Request& req) {
    auto id = req.param<std::string_view>("id").value_or("?");
    return aevox::Response::ok(std::format("user {}", id));
});
// GET /users/me     → "current user"   (static wins)
// GET /users/42     → "user 42"         (parameter matches)
```

Route registration order does not affect match priority. The trie always uses the priority rules above.

## Error Responses

The router produces these automatic error responses without any handler code:

| Condition | HTTP Status | When it occurs |
|---|---|---|
| No path match | 404 Not Found | No trie node reached for the request path |
| Path matched, wrong method | 405 Method Not Allowed | A node exists for the path but not for the HTTP method |
| Typed parameter conversion failed | 400 Bad Request | `from_chars` conversion fails for a typed segment |

To force a 400 manually, return `Response::bad_request()` from a handler. For example, the route below shows how a typed parameter mismatch triggers a 400 automatically:

```bash
# Register: app.get("/items/{count:int}", handler)
curl -i http://localhost:8080/items/abc
# Response: HTTP/1.1 400 Bad Request
# The router tried to parse "abc" as int and failed
```

## Handler Signatures

All four of these signatures are accepted by `get()`, `post()`, and the other registration methods:

```cpp
// Synchronous, no extracted parameters
[](aevox::Request& req) -> aevox::Response {
    return aevox::Response::ok("sync");
}

// Asynchronous
[](aevox::Request& req) -> aevox::Task<aevox::Response> {
    co_return aevox::Response::ok("async");
}

// One typed parameter (extracted from the pattern by the framework)
[](aevox::Request& req, int id) -> aevox::Response {
    return aevox::Response::ok(std::format("id={}", id));
}

// Two typed parameters
[](aevox::Request& req, int page, int size) -> aevox::Response {
    return aevox::Response::ok(std::format("page={} size={}", page, size));
}
```

The typed parameter values in the arity-1 and arity-2 signatures are already converted — the framework runs the conversion before calling the handler. If conversion fails, the router returns a 400 before the handler is called.

## See Also

- [Request and Response](request-response.md) — reading parameters and building responses inside a handler
- [Async Patterns](async-patterns.md) — writing asynchronous handlers with `co_await`
- [API Reference — Router and App](../api/router.md) — complete reference including AppConfig and RouteError
