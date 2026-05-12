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
concept Serializable = true;   // v0.1 placeholder
```

**Purpose:** Constrains `Response::json<T>()`. In v0.1 this is an unconstrained placeholder that allows compilation of `json<T>()` for any type. The actual glaze-backed constraint will replace it in a future release.

**Note:** In v0.1, `Response::json<T>()` returns `SerializeError::NotImplemented` at runtime for all `T` except `std::string`, which has a non-template overload.

**Example:**

```cpp
// Compiles in v0.1, but may fail at runtime depending on the JSON backend.
return aevox::Response::json(my_struct);
```

---

## `Deserializable`

```cpp
template <typename T>
concept Deserializable = true;   // v0.1 placeholder
```

**Purpose:** Constrains `Request::json<T>()`. In v0.1 this is an unconstrained placeholder. `Request::json<T>()` returns `BodyParseError::NotImplemented` at runtime for all `T` except direct string deserialization.

**Example:**

```cpp
// Compiles in v0.1, but may fail at runtime depending on the JSON backend.
auto result = co_await req.json<MyStruct>();
```

---

## See Also

- [Request and Response](request-response.md) — `Request::param<T>()`, `Request::json<T>()`, `Response::json<T>()`
- [JSON](json.md) — JSON serialization and deserialization API
