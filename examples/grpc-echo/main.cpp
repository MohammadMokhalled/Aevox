// Minimal raw-byte unary gRPC echo example.
#include <aevox/app.hpp>
#include <aevox/plugins/grpc.hpp>
#include <aevox/response.hpp>

#include <expected>
#include <iostream>
#include <memory>

int main()
{
    aevox::App app;

    auto grpc = std::make_unique<aevox::grpc::Plugin>(aevox::grpc::PluginConfig{.port = 50051});

    auto registered = grpc->add_unary_method(
        "/aevox.examples.Echo/Echo",
        [](aevox::grpc::UnaryRequest& req)
            -> aevox::Task<std::expected<aevox::grpc::UnaryResponse, aevox::grpc::GrpcStatus>> {
            aevox::grpc::UnaryResponse response;
            response.payload.assign(req.payload().begin(), req.payload().end());
            co_return response;
        });
    if (!registered) {
        std::cerr << "failed to register gRPC method: "
                  << aevox::grpc::to_string(registered.error()) << '\n';
        return 1;
    }

    auto installed = app.install(std::move(grpc));
    if (!installed) {
        std::cerr << "failed to install gRPC plugin: " << aevox::to_string(installed.error())
                  << '\n';
        return 1;
    }

    app.get("/health", [](aevox::Request&) -> aevox::Task<aevox::Response> {
        co_return aevox::Response::ok("ok");
    });

    std::cout << "HTTP listening on http://127.0.0.1:8080\n";
    std::cout << "gRPC h2c listening on 127.0.0.1:50051\n";
    app.listen(8080);
}
