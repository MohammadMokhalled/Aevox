# API Reference: Error Categories

Primary header: `<aevox/error.hpp>`. Module-specific `category(...)` overloads are declared beside their error types in the relevant module headers.

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

These overloads map module-specific errors to `ErrorCategory`. Core Aevox errors use
`aevox::category(...)`; module-owned errors declare the helper in the module namespace.

```cpp
[[nodiscard]] aevox::ErrorCategory aevox::category(aevox::IoError e) noexcept;
[[nodiscard]] aevox::ErrorCategory aevox::category(aevox::ExecutorError e) noexcept;
[[nodiscard]] aevox::ErrorCategory aevox::category(aevox::ParamError e) noexcept;
[[nodiscard]] aevox::ErrorCategory aevox::category(aevox::ConfigError e) noexcept;
[[nodiscard]] aevox::ErrorCategory aevox::category(const aevox::ConfigErrorDetail& detail) noexcept;
[[nodiscard]] aevox::ErrorCategory aevox::category(aevox::LogError error) noexcept;
[[nodiscard]] aevox::ErrorCategory aevox::category(aevox::JsonErrorCode code) noexcept;
[[nodiscard]] aevox::ErrorCategory aevox::category(const aevox::JsonError& error) noexcept;
[[nodiscard]] aevox::ErrorCategory aevox::category(aevox::WebSocketErrorCode code) noexcept;
[[nodiscard]] aevox::ErrorCategory aevox::category(const aevox::WebSocketError& error) noexcept;
[[nodiscard]] aevox::ErrorCategory aevox::category(aevox::PluginError error) noexcept;
[[nodiscard]] aevox::ErrorCategory
aevox::middleware::category(aevox::middleware::StaticFilesConfigError error) noexcept;
[[nodiscard]] aevox::ErrorCategory aevox::grpc::category(aevox::grpc::GrpcError error) noexcept;
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
