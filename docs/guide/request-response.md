# Request and Response

Every handler in Aevox receives an `aevox::Request` by reference and returns an `aevox::Response` by value. This page explains how to read data from the request and how to build responses.

## The Request Object

`aevox::Request` provides read-only access to the incoming HTTP request. It is move-only and owns the raw read buffer for the lifetime of the handler, so all string values it returns are zero-copy views that remain valid until the handler returns.

### Method and Path

```cpp
app.get("/echo", [](aevox::Request& req) {
    // req.method() returns aevox::HttpMethod (GET, POST, PUT, ...)
    auto method = req.method();  // aevox::HttpMethod::GET for this route

    // req.path() returns the path without the query string
    auto path = req.path();      // "/echo"

    return aevox::Response::ok(std::format("{} {}", aevox::to_string(method), path));
});
```

### Path Parameters

Path parameters are values captured from the URL by the router. Use `req.param<T>("name")` to retrieve them as a typed value. The return type is `std::expected<T, ParamError>` — always check it before using the value.

```cpp
app.get("/users/{id:int}", [](aevox::Request& req) {
    auto id = req.param<int>("id");
    if (!id) {
        // id.error() is ParamError::NotFound or ParamError::BadConversion
        return aevox::Response::bad_request("missing or invalid user id");
    }
    return aevox::Response::ok(std::format("User id: {}", *id));
});
```

You can also use `.value_or()` as a shorthand when a default is acceptable:

```cpp
auto name = req.param<std::string_view>("name").value_or("stranger");
```

### Query Parameters

`req.query()` returns the raw query string — the part of the URL after `?`, without the leading `?` character. Typed extraction of individual query parameters is not yet available in v0.1.

```cpp
app.get("/search", [](aevox::Request& req) {
    // For a request to /search?q=hello&page=2
    auto qs = req.query();  // "q=hello&page=2"
    return aevox::Response::ok(std::format("query: {}", qs));
});
```

### Headers

`req.header(name)` retrieves a header value by name. The lookup is case-insensitive. It returns `std::optional<std::string_view>`.

```cpp
app.post("/upload", [](aevox::Request& req) {
    auto ct = req.header("Content-Type");
    if (!ct) {
        return aevox::Response::bad_request("Content-Type required");
    }
    if (!ct->starts_with("application/json")) {
        return aevox::Response::bad_request("expected application/json");
    }
    return aevox::Response::ok("accepted");
});
```

### Body

`req.body()` returns a `std::span<const std::byte>` over the request body bytes. For a GET request this is an empty span.

```cpp
app.post("/echo-body", [](aevox::Request& req) {
    auto body = req.body();
    // Convert bytes to a string_view for display
    auto text = std::string_view{
        reinterpret_cast<const char*>(body.data()), body.size()};
    return aevox::Response::ok(text);
});
```

## Building Responses

`aevox::Response` is constructed exclusively through static factory methods. There is no public constructor. Build the response, optionally chain header setters, and return it by value.

### Factory Methods

| Method | HTTP Status | Default Content-Type |
|---|---|---|
| `Response::ok(body = "")` | 200 | `text/plain` |
| `Response::created(body = "")` | 201 | `text/plain` |
| `Response::not_found(body = "")` | 404 | `text/plain` |
| `Response::bad_request(body = "")` | 400 | `text/plain` |
| `Response::unauthorized(body = "")` | 401 | `text/plain` |
| `Response::forbidden(body = "")` | 403 | `text/plain` |
| `Response::json(std::string body)` | 200 | `application/json` |

```cpp
return aevox::Response::ok("Hello");
return aevox::Response::not_found("no such resource");
return aevox::Response::bad_request("missing field: name");
return aevox::Response::json(R"({"status":"ok"})");
```

### Setting Headers

Use the fluent `.header(name, value)` method to add headers to any response. It can be chained:

```cpp
return aevox::Response::ok("<html><body>Hello</body></html>")
    .content_type("text/html")
    .header("X-Frame-Options", "DENY")
    .header("Cache-Control", "no-store");
```

### Status Codes

The factory methods cover the most common cases. The table above lists all available factories with their HTTP status codes.

## Error Handling

The `req.param<T>()` method is the most common place where error handling is needed in a handler. Here is the recommended pattern:

```cpp
app.get("/orders/{id:int}", [](aevox::Request& req) {
    auto id = req.param<int>("id");
    if (!id) {
        return aevox::Response::bad_request("invalid order id");
    }
    // Now *id is safe to use
    return aevox::Response::ok(std::format("Order {}", *id));
});
```

Never call `req.param<T>().value()` without first checking the result — if the parameter is missing or conversion fails, `value()` on an unexpected result terminates the program. Use `if (!id)` or `.value_or(default)`.

## See Also

- [Routing](routing.md) — how path parameters are captured and which patterns are supported
- [Error Handling](error-handling.md) — the full `std::expected` error model and all error types
- [API Reference — Request and Response](../api/request-response.md) — complete symbol reference including `json<T>()` and the middleware context store
