# Request and Response

> The two primary HTTP value types that every handler, middleware, and route dispatcher interacts with.

## Overview

`aevox::Request` and `aevox::Response` are the public-facing types for every HTTP interaction in an Aevox application. They sit at the third layer of the Aevox stack: above the internal HTTP parser (`aevox::detail::HttpParser`) and below the Router.

`Request` gives handlers read access to the incoming HTTP message: method, path, query string, headers, body bytes, and typed path parameters. It is move-only and owns the raw read buffer for the duration of the handler — every `std::string_view` returned by its accessors is valid for as long as the `Request` object lives.

`Response` gives handlers a way to describe the HTTP reply. It is constructed exclusively through static factory methods and returned by move to the framework, which serializes it to wire bytes. You never write to the socket directly.

Neither type is thread-safe and neither should be shared between coroutines. Create them, use them, return them — that is their entire lifecycle.

## Quick Start

```cpp
#include <aevox/request.hpp>
#include <aevox/response.hpp>

// A route handler receives a Request by reference and returns a Response by value.
aevox::Task<aevox::Response> get_user(aevox::Request& req) {
    // Extract a typed path parameter (/users/:id → "id")
    auto id_result = req.param<int>("id");
    if (!id_result) {
        co_return aevox::Response::bad_request("invalid user id");
    }

    // Read a header
    auto auth = req.header("Authorization");
    if (!auth) {
        co_return aevox::Response::unauthorized();
    }

    // Return a pre-serialized JSON response
    co_return aevox::Response::json(
        std::format(R"({{"id":{},"name":"Alice"}})", *id_result));
}
```

## API Reference

### `aevox::Request`

Immutable view of an HTTP/1.1 request, valid for the handler lifetime.

**Thread-safety:** Not thread-safe. Must be used on the connection strand only.

**Move semantics:** Move-only. A moved-from `Request` has `valid() == false`.

**Ownership:** Owns the raw TCP read buffer. All `std::string_view` fields remain valid for the lifetime of the `Request` object.

---

#### `valid() → bool`

```cpp
[[nodiscard]] bool valid() const noexcept;
```

Returns `true` when the Request holds a live parsed request. Returns `false` after the Request has been moved from. Calling any other accessor on an invalid Request is undefined behaviour.

---

#### `method() → HttpMethod`

```cpp
[[nodiscard]] HttpMethod method() const noexcept;
```

Returns the parsed HTTP method. Unknown or unsupported verbs return `HttpMethod::Unknown`.

**Example:**
```cpp
if (req.method() == aevox::HttpMethod::GET) { ... }
```

---

#### `path() → std::string_view`

```cpp
[[nodiscard]] std::string_view path() const noexcept;
```

Returns the path portion of the request target, without the query string. For a request to `/users/42?sort=asc` this returns `/users/42`. Zero-copy into the owned buffer.

---

#### `query() → std::string_view`

```cpp
[[nodiscard]] std::string_view query() const noexcept;
```

Returns the raw query string, without the leading `?`. For `/users?sort=asc&page=2` this returns `sort=asc&page=2`. Returns an empty `string_view` if no query string is present. Typed query parameter parsing is deferred to a future task.

---

#### `header(name) → std::optional<std::string_view>`

```cpp
[[nodiscard]] std::optional<std::string_view>
header(std::string_view name) const noexcept;
```

Retrieves a header value by name. Lookup is case-insensitive per RFC 7230 §3.2. When a header name appears multiple times, returns the first occurrence.

**Parameters:**
| Parameter | Description |
|---|---|
| `name` | Header field name, e.g. `"content-type"`, `"Content-Type"`, or `"CONTENT-TYPE"` |

**Returns:** The header value as a zero-copy `string_view`, or `std::nullopt` if not present.

**Example:**
```cpp
auto ct = req.header("Content-Type");
if (ct && ct->starts_with("application/json")) { ... }
```

---

#### `body() → std::span<const std::byte>`

```cpp
[[nodiscard]] std::span<const std::byte> body() const noexcept;
```

Returns a non-owning view of the fully assembled request body. Empty for requests with no body (e.g. GET). The span is valid for the lifetime of the `Request`.

---

#### `param<T>(name) → std::expected<T, ParamError>`

```cpp
template <typename T>
    requires aevox::ParamConvertible<T>
[[nodiscard]] std::expected<T, ParamError>
param(std::string_view name) const noexcept;
```

Retrieves a typed path parameter by name. Path parameters are injected by the Router before the handler is called.

**Template parameters:**
| Parameter | Constraint | Description |
|---|---|---|
| `T` | `aevox::ParamConvertible` | `int`, `long`, `double`, `std::string`, or `std::string_view` |

**Conversion rules:**
- Arithmetic types (`int`, `long`, `double`): converted via `std::from_chars`. Returns `BadConversion` if the full string cannot be parsed.
- `std::string_view`: zero-copy; lifetime tied to the `Request`.
- `std::string`: owned copy.

**Example:**
```cpp
auto id = req.param<int>("id");
if (!id) {
    // id.error() == ParamError::NotFound or ParamError::BadConversion
    co_return aevox::Response::bad_request("invalid id");
}
// use *id
```

---

#### `json<T>() → Task<std::expected<T, BodyParseError>>`

```cpp
template <typename T>
    requires aevox::Deserializable<T>
[[nodiscard]] aevox::Task<std::expected<T, BodyParseError>>
json() const;
```

Asynchronously parses the request body as JSON into type `T`. Must be `co_await`-ed. In v0.1, always returns `BodyParseError::NotImplemented`. AEV-009 wires in real glaze deserialization.

---

#### `set<T>(key, value)` / `get<T>(key)`

```cpp
template <typename T>
void set(std::string_view key, T&& value);

template <typename T>
[[nodiscard]] std::optional<T> get(std::string_view key) const;
```

Per-request middleware context bag. `set` stores any value type-erased via `std::any`. `get` retrieves it with exact type matching — returns `std::nullopt` if absent or if the stored type does not exactly match `T`.

**Example (middleware setting, handler reading):**
```cpp
// In middleware:
req.set("auth.user", std::string{"alice"});

// In handler:
auto user = req.get<std::string>("auth.user");
if (user) { /* *user == "alice" */ }
```

---

### `aevox::HttpMethod`

```cpp
enum class HttpMethod : std::uint8_t {
    GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS,
    Unknown  // any unrecognised verb
};

[[nodiscard]] std::string_view to_string(HttpMethod m) noexcept;
```

---

### `aevox::Response`

A fully formed HTTP/1.1 response. Created via static factory methods and returned by move to the framework.

**Thread-safety:** Not thread-safe. A value type — create, modify, return.

**Move semantics:** Move-only. A moved-from `Response` has `status_code() == 0`, empty body, no headers.

---

#### Factory methods

| Method | Status | Default Content-Type |
|---|---|---|
| `Response::ok(body = "")` | 200 | `text/plain` |
| `Response::created(body = "")` | 201 | `text/plain` |
| `Response::not_found(body = "")` | 404 | `text/plain` |
| `Response::bad_request(body = "")` | 400 | `text/plain` |
| `Response::unauthorized(body = "")` | 401 | `text/plain` |
| `Response::forbidden(body = "")` | 403 | `text/plain` |
| `Response::json(std::string body)` | 200 | `application/json` |
| `Response::json<T>(T&& value)` | 200 | `application/json` (stub in v0.1) |
| `Response::stream(content_type)` | 200 | given content type |

---

#### Fluent builder

```cpp
// lvalue — modifies in place, returns reference
Response& content_type(std::string_view ct) &;
Response& header(std::string_view name, std::string_view value) &;

// rvalue — consumes *this, returns new Response (efficient chaining)
Response  content_type(std::string_view ct) &&;
Response  header(std::string_view name, std::string_view value) &&;
```

**Example:**
```cpp
auto res = aevox::Response::ok("<html>...</html>")
               .content_type("text/html")
               .header("X-Frame-Options", "DENY");
```

---

#### `status_code() → int`

```cpp
[[nodiscard]] int status_code() const noexcept;
```

Returns the HTTP status code. Returns 0 for a moved-from Response.

---

#### `body_view() → std::string_view`

```cpp
[[nodiscard]] std::string_view body_view() const noexcept;
```

Returns a read-only view of the response body. Valid for the lifetime of this Response.

---

## Error Reference

### `aevox::ParamError`

| Error | Meaning | How to handle |
|---|---|---|
| `ParamError::NotFound` | No path parameter with the given name was captured by the router | Check the route pattern; return `bad_request()` |
| `ParamError::BadConversion` | The raw string cannot be parsed as the requested type | Return `bad_request()` with a diagnostic message |

### `aevox::BodyParseError`

| Error | Meaning | How to handle |
|---|---|---|
| `BodyParseError::NotImplemented` | JSON parsing not wired in v0.1 | Expected until AEV-009 is implemented |
| `BodyParseError::BadJson` | Body is not valid JSON (reserved for AEV-009) | Return `bad_request()` |
| `BodyParseError::TypeMismatch` | JSON does not match target type (reserved for AEV-009) | Return `bad_request()` |

### `aevox::SerializeError`

| Error | Meaning | How to handle |
|---|---|---|
| `SerializeError::NotImplemented` | JSON serialization not wired in v0.1 | Expected until AEV-009 |
| `SerializeError::TypeNotSupported` | Type cannot be serialized (reserved for AEV-009) | Use `Response::json(std::string)` overload |

## Thread Safety

Neither `Request` nor `Response` is thread-safe. Both are single-connection value types:

- `Request` is created by the framework, passed to the handler by reference, and must not outlive the handler coroutine or be shared with other coroutines.
- `Response` is created in the handler and returned by move. Never share a `Response` between threads.

## Move Semantics

Both types are move-only (copy-deleted). The moved-from state is valid but empty:

```cpp
auto r1 = aevox::Response::ok("body");
auto r2 = std::move(r1);
// r2.status_code() == 200, r2.body_view() == "body"
// r1.status_code() == 0,   r1.body_view().empty()
```

## v0.1 Limitations

- **JSON parsing (`req.json<T>()`):** Always returns `BodyParseError::NotImplemented`. Real implementation wired in AEV-009.
- **JSON serialization (`Response::json<T>()`):** Always produces sentinel body `{"error":"not_implemented"}`. Real implementation wired in AEV-009. Use `Response::json(std::string)` to pass a pre-serialized string.
- **Streaming (`Response::stream()`):** Returns a normal Response with empty body. The streaming write API is designed in AEV-006.
- **Duplicate headers:** `Request::header()` returns the first occurrence of a repeated header name. Multi-value header support is deferred.
- **Query parameter parsing:** `Request::query()` returns the raw query string. Typed extraction (e.g. `req.query_param<int>("page")`) is deferred.

## See Also

- [Task](task.md) — `aevox::Task<T>` coroutine return type used by `json<T>()`
- [Executor](executor.md) — the I/O execution layer below Request/Response
- [Async Helpers](async.md) — `pool()`, `sleep()`, `when_all()`
