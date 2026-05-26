# gRPC

Aevox provides first-party gRPC support as an optional plugin. It runs on a separate h2c port from
the HTTP/1.1 app listener, keeping native HTTP/2 out of the core framework.

## Build

Configure with the gRPC feature enabled:

```bash
cmake --preset default-tests -DAEVOX_ENABLE_GRPC=ON
cmake --build --preset default-tests --target aevox_grpc_plugin
```

Applications link both `aevox_core` and `aevox_grpc_plugin`.

Enable the matching vcpkg feature when configuring with vcpkg manifest features:

```bash
cmake --preset default-tests -DAEVOX_ENABLE_GRPC=ON -DVCPKG_MANIFEST_FEATURES=grpc
```

## Unary Method

```cpp
auto grpc = std::make_unique<aevox::grpc::Plugin>(
    aevox::grpc::PluginConfig{.port = 50051});

auto ok = grpc->add_unary_method(
    "/example.Echo/Echo",
    [](aevox::grpc::UnaryRequest& req)
        -> aevox::Task<std::expected<aevox::grpc::UnaryResponse, aevox::grpc::GrpcStatus>> {
        aevox::grpc::UnaryResponse response;
        response.payload.assign(req.payload().begin(), req.payload().end());
        co_return response;
    });

auto installed = app.install(std::move(grpc));
```

The handler receives decoded request bytes. Return `std::unexpected(aevox::grpc::GrpcStatus::error(...))`
to send a non-OK gRPC status.

`PluginConfig::drain_timeout` is accepted as milliseconds. The current executor drain setting is
seconds-based, so sub-second values are rounded up to one second by the plugin.

## Lifecycle

Installed plugins start automatically when `App::listen()` starts and stop when `App::stop()` or the
`App` destructor runs. gRPC uses its own executor so HTTP/1.1 routes and gRPC methods can run side by
side on separate ports.

For public TLS, terminate at the reverse proxy and forward to the plugin's h2c listener. Do not proxy
gRPC traffic through the core HTTP/1.1 app port; see [Deployment](deployment.md) for the boundary.
