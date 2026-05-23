# gRPC Plugin API

The official gRPC plugin is optional and is built only when CMake is configured with
`AEVOX_ENABLE_GRPC=ON`. Normal HTTP applications do not link nghttp2 and do not need the gRPC
plugin target.

```cpp
#include <aevox/app.hpp>
#include <aevox/plugins/grpc.hpp>
```

The initial plugin supports unary h2c gRPC methods using raw protobuf message bytes:

```cpp
aevox::App app;

auto grpc = std::make_unique<aevox::grpc::Plugin>(
    aevox::grpc::PluginConfig{.port = 50051});

auto registered = grpc->add_unary_method(
    "/example.Echo/Echo",
    [](aevox::grpc::UnaryRequest& req)
        -> aevox::Task<std::expected<aevox::grpc::UnaryResponse, aevox::grpc::GrpcStatus>> {
        aevox::grpc::UnaryResponse response;
        response.payload.assign(req.payload().begin(), req.payload().end());
        co_return response;
    });

auto installed = app.install(std::move(grpc));
app.listen(8080);
```

`aevox::grpc::UnaryRequest::payload()` returns the decoded message bytes without the five-byte gRPC
message envelope. Applications that use protobuf generated types parse those bytes in application
code.

## Core Plugin API

```cpp
enum class aevox::PluginError : std::uint8_t;

[[nodiscard]] std::string_view aevox::to_string(PluginError error) noexcept;
[[nodiscard]] ErrorCategory aevox::category(PluginError error) noexcept;

class aevox::Plugin {
public:
    virtual ~Plugin() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::expected<void, PluginError> install(App& app) noexcept = 0;
    [[nodiscard]] virtual std::expected<void, PluginError> start() noexcept = 0;
    virtual void stop() noexcept = 0;
};

[[nodiscard]] std::expected<void, PluginError>
aevox::App::install(std::unique_ptr<Plugin> plugin) noexcept;
```

`App::install()` rejects `nullptr` with `PluginError::InvalidArgument`. On success, the app owns
the plugin and starts it from `App::listen()`. On failure, ownership is not transferred into the app.

## gRPC Types

```cpp
enum class aevox::grpc::StatusCode : std::uint8_t {
    Ok, Cancelled, Unknown, InvalidArgument, DeadlineExceeded, NotFound,
    AlreadyExists, PermissionDenied, ResourceExhausted, FailedPrecondition,
    Aborted, OutOfRange, Unimplemented, Internal, Unavailable, DataLoss,
    Unauthenticated
};

enum class aevox::grpc::GrpcError : std::uint8_t {
    InvalidConfig, InvalidMethod, DuplicateMethod, AlreadyInstalled, AlreadyRunning,
    ListenFailed, StartFailed, ProtocolError, MessageTooLarge,
    CompressionUnsupported, Unknown
};

struct aevox::grpc::Metadata {
    std::string name;
    std::string value;
};

struct aevox::grpc::GrpcStatus {
    StatusCode code{StatusCode::Ok};
    std::string message;

    [[nodiscard]] static GrpcStatus ok();
    [[nodiscard]] static GrpcStatus error(StatusCode code, std::string_view message);
};

struct aevox::grpc::PluginConfig {
    std::uint16_t port{50051};
    std::uint16_t max_concurrent_streams{128};
    std::size_t max_message_size{4U * 1024U * 1024U};
    std::uint16_t worker_threads{0};
    std::chrono::milliseconds drain_timeout{std::chrono::milliseconds{5000}};
};

class aevox::grpc::UnaryRequest {
public:
    UnaryRequest(std::string method, std::vector<std::byte> payload,
                 std::vector<Metadata> metadata = {});
    [[nodiscard]] std::string_view method() const noexcept;
    [[nodiscard]] std::span<const std::byte> payload() const noexcept;
    [[nodiscard]] std::span<const Metadata> metadata() const noexcept;
};

struct aevox::grpc::UnaryResponse {
    std::vector<std::byte> payload;
    std::vector<Metadata> trailing_metadata;
};

using aevox::grpc::UnaryHandler =
    std::move_only_function<Task<std::expected<UnaryResponse, GrpcStatus>>(UnaryRequest&)>;
```

`UnaryRequest::method()` is the fully qualified gRPC method path from `:path`, such as
`/example.Echo/Echo`. `payload()` is one decoded uncompressed gRPC message without the five-byte
envelope. `metadata()` contains application metadata after pseudo-headers and reserved gRPC transport
headers are removed.

Return `UnaryResponse` for success. Return `std::unexpected(GrpcStatus::error(...))` to send a
non-OK gRPC status trailer.

## Plugin

```cpp
class aevox::grpc::Plugin final : public aevox::Plugin {
public:
    explicit Plugin(PluginConfig config = {});

    [[nodiscard]] std::expected<void, GrpcError>
    add_unary_method(std::string_view method, UnaryHandler handler) noexcept;

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::expected<void, PluginError> install(aevox::App& app) noexcept override;
    [[nodiscard]] std::expected<void, PluginError> start() noexcept override;
    void stop() noexcept override;
};

[[nodiscard]] std::unique_ptr<aevox::Plugin>
aevox::grpc::make_plugin(PluginConfig config = {});
```

`add_unary_method()` must be called before installation. Method paths must have the form
`/package.Service/Method`; malformed paths return `GrpcError::InvalidMethod`, duplicate paths return
`GrpcError::DuplicateMethod`, and registration after install returns `GrpcError::AlreadyInstalled`.

`make_plugin()` returns an owning `aevox::Plugin` pointer for apps that do not need the concrete
`aevox::grpc::Plugin` type after construction.

## Error Reference

| Error | When returned |
|---|---|
| `PluginError::InvalidArgument` | Null plugin pointer, invalid gRPC config, or moved-from plugin state |
| `PluginError::AlreadyInstalled` | Plugin installed more than once |
| `PluginError::StartFailed` | gRPC listener bind/listen or worker startup failed |
| `PluginError::AlreadyRunning` | `start()` called while running |
| `GrpcError::InvalidConfig` | Port, stream limit, or message limit is zero |
| `GrpcError::InvalidMethod` | Unary method path or handler is invalid |
| `GrpcError::DuplicateMethod` | Same method path registered twice |
| `GrpcError::ProtocolError` | Malformed gRPC envelope |
| `GrpcError::MessageTooLarge` | Request or response payload exceeds `max_message_size` |
| `GrpcError::CompressionUnsupported` | Compressed request messages are not supported in v0.3 |

## Scope

The plugin intentionally supports h2c unary calls only. Streaming RPCs, generated service adapters,
TLS/ALPN, reflection, and health-check services are follow-up work.
