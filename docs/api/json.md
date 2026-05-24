# API Reference: JSON

Headers: `<aevox/json_error.hpp>`, `<aevox/json_backend.hpp>`

The JSON layer provides structured error reporting (`aevox::JsonError`), a pluggable backend concept (`aevox::JsonBackend<B>`), and the request and response operations `Request::json<T>()` and `Response::json(value)`. The default backend is an implementation detail; no backend-specific types appear in the public headers.

---

## aevox::JsonError

```cpp
// include/aevox/json_error.hpp
namespace aevox {

class JsonError {
public:
    explicit JsonError(JsonErrorCode code, std::string message) noexcept;
    explicit JsonError(std::string message) noexcept;
    [[nodiscard]] JsonErrorCode code() const noexcept;
    [[nodiscard]] std::string_view message() const noexcept;
};

} // namespace aevox
```

Value type returned via `std::unexpected` from all JSON operations. Never thrown.

### JsonErrorCode

```cpp
enum class JsonErrorCode : std::uint8_t {
    ParseError,
    TypeMismatch,
    MissingField,
    SerializationFailed,
    InvalidUtf8,
    Unknown,
};
```

Use `code()` for stable branching and `message()` for diagnostics.

### Constructors

```cpp
explicit JsonError(JsonErrorCode code, std::string message) noexcept;
explicit JsonError(std::string message) noexcept;
```

Constructs a `JsonError` with a stable code and a human-readable description. The message-only constructor is preserved for source compatibility and sets `code()` to `JsonErrorCode::Unknown`.

### code()

```cpp
[[nodiscard]] JsonErrorCode code() const noexcept;
```

Returns the stable JSON error discriminator.

### message()

```cpp
[[nodiscard]] std::string_view message() const noexcept;
```

Returns a non-owning view into the stored message string, valid for the lifetime of the `JsonError` object. Returns an empty view for a moved-from `JsonError`.

### Helpers

```cpp
[[nodiscard]] std::string_view to_string(JsonErrorCode code) noexcept;
[[nodiscard]] ErrorCategory category(JsonErrorCode code) noexcept;
[[nodiscard]] ErrorCategory category(const JsonError& error) noexcept;
```

### Thread-safety and move semantics

`JsonError` is a value type — safe to copy and move across threads. A moved-from `JsonError` has an empty `message()` and remains valid and destructible.

---

## aevox::JsonBackend concept

```cpp
// include/aevox/json_backend.hpp
namespace aevox {

template <typename B>
concept JsonBackend = requires(const B& backend,
                               std::string_view input,
                               const detail::JsonConceptProbe& probe) {
    { backend.template deserialize<detail::JsonConceptProbe>(input) }
        -> std::same_as<std::expected<detail::JsonConceptProbe, JsonError>>;
    { backend.serialize(probe) }
        -> std::same_as<std::expected<std::string, JsonError>>;
};

} // namespace aevox
```

Compile-time contract for JSON backend implementations. A type `B` satisfies `JsonBackend` if it provides both `deserialize<T>(std::string_view)` and `serialize(const T&)` with the correct return types.

### Requirements

| Operation | Signature | Notes |
|---|---|---|
| `deserialize<T>` | `std::expected<T, JsonError> deserialize(std::string_view) const` | Must be callable on `const B&` |
| `serialize` | `std::expected<std::string, JsonError> serialize(const T&) const` | Must be callable on `const B&` |

Both operations must be callable on a `const` backend — backends must carry no mutable state and must support concurrent calls from separate threads without synchronization.

### Writing a custom backend

```cpp
#include <aevox/json_backend.hpp>

struct MyBackend {
    template <typename T>
    [[nodiscard]] std::expected<T, aevox::JsonError>
    deserialize(std::string_view input) const {
        T result{};
        // parse input into result ...
        if (/* parse failed */) {
            return std::unexpected(aevox::JsonError{
                aevox::JsonErrorCode::ParseError,
                "parse failed: ..."});
        }
        return result;
    }

    template <typename T>
    [[nodiscard]] std::expected<std::string, aevox::JsonError>
    serialize(const T& value) const {
        // convert value to JSON string ...
        return std::string{/* json */};
    }
};

static_assert(aevox::JsonBackend<MyBackend>);
```

---

## aevox::Request::json\<T\>()

```cpp
// include/aevox/request.hpp
template <typename T>
    requires aevox::Deserializable<T>
[[nodiscard]] aevox::Task<std::expected<T, aevox::JsonError>> json() const;
```

Deserializes the request body as JSON into type `T`. Must be called with `co_await` inside a coroutine handler.

### Parameters

None. `T` is a template argument specifying the target type.

### Return value

`aevox::Task<std::expected<T, aevox::JsonError>>` — awaiting it yields either a populated `T` or an `aevox::JsonError` describing the failure. Error cases include truncated input, invalid UTF-8, type mismatches, and missing required fields.

### Example

```cpp
app.post("/items", [](aevox::Request& req) -> aevox::Task<aevox::Response> {
    struct CreateItemBody { std::string name; double price{}; };

    auto body = co_await req.json<CreateItemBody>();
    if (!body) {
        co_return aevox::Response::bad_request(
            std::string{body.error().message()});
    }
    co_return aevox::Response::ok(
        std::format("created: {} at {}", body->name, body->price));
});
```

### Notes

- **Body caching:** the body bytes are stable for the full request lifetime. Each call to `json<T>()` re-parses. Cache the result if the same type is needed more than once.
- **Multiple types:** calling with two different types `T` and `U` on the same request is supported — each call parses independently.
- **No I/O:** the coroutine suspends and resumes synchronously (no I/O is performed). The `co_await` is required for consistency with the async handler signature.
- **Thread-safety:** safe to call concurrently on separate `Request` instances. Not safe to call from multiple threads on the same `Request` instance.

---

## aevox::Response::json(value)

```cpp
// include/aevox/response.hpp
template <typename T>
    requires aevox::Serializable<T>
[[nodiscard]] static Response json(const T& value);
```

Serializes `value` to JSON and returns a 200 OK response with `Content-Type: application/json`. Synchronous — no coroutine required.

### Parameters

`value` — const reference to the value to serialize. Both lvalues and rvalues bind.

### Return value

- On success: `Response` with status 200, `Content-Type: application/json`, and the serialized JSON body.
- On failure: `Response` with status 500 and a JSON error body:
  ```json
  {"error":"json_serialization_failed","detail":"<message>"}
  ```
  Serialization failure is rare for well-formed aggregate structs.

### Example

```cpp
struct Product { std::string id; std::string name; double price{}; };

app.get("/products/{id}", [](aevox::Request& req) -> aevox::Task<aevox::Response> {
    auto id = req.param<std::string>("id");
    if (!id) {
        co_return aevox::Response::not_found();
    }
    co_return aevox::Response::json(
        Product{.id = *id, .name = "Widget", .price = 9.99});
});
```

Also accepts a pre-serialized `std::string` for cases where you have already produced JSON:

```cpp
[[nodiscard]] static Response json(std::string body);
```

This overload sets `Content-Type: application/json` and status 200 without any serialization step.

### Notes

- **Synchronous:** serialization completes in microseconds for typical structs. For very large bodies (>1 MiB), offload to `co_await aevox::pool(...)` before calling this factory.
- **Thread-safety:** the default backend carries no mutable state — safe to call concurrently from separate handlers.

---

## Default Backend Boundary

The default backend is stateless and confined to `src/json/`. Application code never includes its headers, names its concrete type, or handles backend-specific errors. All public failures are reported as `aevox::JsonError`.

Any aggregate struct with public fields can be serialized or deserialized without an application-level registration macro. The codec is instantiated for each distinct `T` at the call site.

---

## See Also

- [User Guide — JSON](../guide/json.md) — practical examples and patterns
- [Request and Response API](request-response.md) — full `Request` and `Response` API
- [Error Handling Guide](../guide/error-handling.md) — working with `std::expected`
