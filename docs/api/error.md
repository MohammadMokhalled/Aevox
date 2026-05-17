# API Reference: Error Categories

Header: `<aevox/error.hpp>`

Aevox keeps precise module-specific error types, such as `IoError`, `ConfigError`, `JsonError`, and `WebSocketError`. `aevox::ErrorCategory` is a small shared classifier for logging, metrics, and generic application code. It does not replace module-specific error codes.

## `aevox::ErrorCategory`

```cpp
enum class ErrorCategory : std::uint8_t {
    Io,
    Protocol,
    Parse,
    Validation,
    NotFound,
    Serialization,
    State,
    Unknown,
};
```

Use the exact module error enum for precise recovery logic. Use `ErrorCategory` when the caller only needs a broad classification.

## `to_string(ErrorCategory)`

```cpp
[[nodiscard]] std::string_view to_string(ErrorCategory category) noexcept;
```

Returns a stable static label for logging and diagnostics.

## Category helpers

These overloads map module-specific errors to `ErrorCategory`:

```cpp
[[nodiscard]] ErrorCategory category(IoError e) noexcept;
[[nodiscard]] ErrorCategory category(ExecutorError e) noexcept;
[[nodiscard]] ErrorCategory category(ParamError e) noexcept;
[[nodiscard]] ErrorCategory category(ConfigError e) noexcept;
[[nodiscard]] ErrorCategory category(const ConfigErrorDetail& detail) noexcept;
[[nodiscard]] ErrorCategory category(JsonErrorCode code) noexcept;
[[nodiscard]] ErrorCategory category(const JsonError& error) noexcept;
[[nodiscard]] ErrorCategory category(WebSocketErrorCode code) noexcept;
[[nodiscard]] ErrorCategory category(const WebSocketError& error) noexcept;
```

Example:

```cpp
auto body = co_await req.json<CreateItem>();
if (!body) {
    if (aevox::category(body.error()) == aevox::ErrorCategory::Validation) {
        co_return aevox::Response::bad_request(std::string{body.error().message()});
    }
    co_return aevox::Response::bad_request("invalid JSON");
}
```
