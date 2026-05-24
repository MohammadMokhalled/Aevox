# Concepts

`include/aevox/concepts.hpp` defines the framework-wide C++23 concepts used to constrain template parameters in the public API.

---

## `ParamConvertible`

```cpp
template <typename T>
concept ParamConvertible = std::integral<T> || std::floating_point<T> ||
                           std::same_as<T, std::string_view> || std::same_as<T, std::string>;
```

**Purpose:** Constrains `Request::param<T>()`. Only types satisfying this concept can be used as typed path or query parameters.

**Supported types:**

| Category | Types | Conversion |
|---|---|---|
| Integral | `int`, `long`, `std::int64_t`, etc. | `std::from_chars` |
| Floating-point | `float`, `double` | `std::from_chars` (C++17 / C++23) |
| String view | `std::string_view` | Zero-copy reference into the connection buffer |
| String | `std::string` | Owned copy |

**Example:**

```cpp
app.get("/items/{id:int}", [](aevox::Request& req) {
    auto id = req.param<int>("id");          // OK — int is ParamConvertible
    auto name = req.param<std::string>("name"); // OK — std::string is ParamConvertible
    // auto x = req.param<std::vector<int>>("x"); // ERROR — not ParamConvertible
    ...
});
```

---

## `Serializable`

```cpp
template <typename T>
concept Serializable = true;
```

**Purpose:** Constrains `Response::json<T>()` at the public API boundary. The current public concept is intentionally broad so application aggregate types do not need to include or name the JSON backend in user code.

Serialization is performed by the active JSON backend. If a type cannot be serialized, `Response::json<T>()` returns a valid 500 response with a structured JSON error body instead of throwing.

**Example:**

```cpp
struct Product {
    std::string id;
    std::string name;
};

return aevox::Response::json(Product{.id = "p1", .name = "Widget"});
```

---

## `Deserializable`

```cpp
template <typename T>
concept Deserializable = true;
```

**Purpose:** Constrains `Request::json<T>()` at the public API boundary. The current public concept is intentionally broad so handlers can request ordinary application aggregate types without depending on backend-specific concepts.

Deserialization returns `std::expected<T, aevox::JsonError>`. Parse failures, type mismatches, missing required fields, and unsupported target types use the error branch.

**Example:**

```cpp
auto result = co_await req.json<MyStruct>();
if (!result) {
    co_return aevox::Response::bad_request(std::string{result.error().message()});
}
```

---

## See Also

- [Request and Response](request-response.md) — `Request::param<T>()`, `Request::json<T>()`, `Response::json<T>()`
- [JSON](json.md) — JSON serialization and deserialization API
