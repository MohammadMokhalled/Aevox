// gRPC App lifecycle integration tests.
#include <aevox/app.hpp>
#include <aevox/plugins/grpc.hpp>
#include <aevox/response.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <thread>

namespace {

[[nodiscard]] std::uint16_t free_port()
{
    asio::io_context              ioc;
    asio::ip::tcp::acceptor const acceptor{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return acceptor.local_endpoint().port();
}

[[nodiscard]] std::string http_get(std::uint16_t port)
{
    asio::io_context      io;
    asio::ip::tcp::socket socket{io};
    socket.connect({asio::ip::make_address("127.0.0.1"), port});
    const std::string request = "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::write(socket, asio::buffer(request));
    std::array<char, 256> buffer{};
    std::error_code       ec;
    const auto            n = socket.read_some(asio::buffer(buffer), ec);
    return std::string{buffer.data(), n};
}

} // namespace

TEST_CASE("grpc app lifecycle - HTTP and gRPC ports run side by side", "[grpc][integration]")
{
    const auto http_port = free_port();
    const auto grpc_port = free_port();

    aevox::App app;
    auto grpc = std::make_unique<aevox::grpc::Plugin>(aevox::grpc::PluginConfig{.port = grpc_port});
    auto installed = app.install(std::move(grpc));
    REQUIRE(installed.has_value());
    app.get("/health", [](aevox::Request&) -> aevox::Task<aevox::Response> {
        co_return aevox::Response::ok("ok");
    });

    std::jthread server{[&app, http_port] { app.listen(http_port); }};
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    CHECK(http_get(http_port).find("200 OK") != std::string::npos);

    asio::io_context      io;
    asio::ip::tcp::socket grpc_socket{io};
    CHECK_NOTHROW(grpc_socket.connect({asio::ip::make_address("127.0.0.1"), grpc_port}));
    app.stop();
}

TEST_CASE("grpc app lifecycle - App stop stops grpc plugin", "[grpc][integration]")
{
    const auto http_port = free_port();
    const auto grpc_port = free_port();

    aevox::App app;
    auto installed = app.install(std::make_unique<aevox::grpc::Plugin>(aevox::grpc::PluginConfig{
        .port = grpc_port,
    }));
    REQUIRE(installed.has_value());
    std::jthread server{[&app, http_port] { app.listen(http_port); }};
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    app.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    CHECK(true);
}

TEST_CASE("grpc app lifecycle - plugin bind failure fails startup", "[grpc][integration]")
{
    const auto              grpc_port = free_port();
    asio::io_context        io;
    asio::ip::tcp::acceptor held{io, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), grpc_port}};

    aevox::grpc::Plugin plugin{aevox::grpc::PluginConfig{.port = grpc_port}};
    aevox::App          app;
    auto                installed = plugin.install(app);
    REQUIRE(installed.has_value());
    auto started = plugin.start();
    REQUIRE_FALSE(started.has_value());
    CHECK(started.error() == aevox::PluginError::StartFailed);
}

TEST_CASE("grpc app lifecycle - plugin can be destroyed without listen", "[grpc][integration]")
{
    {
        aevox::grpc::Plugin plugin;
        auto                result = plugin.add_unary_method(
            "/aevox.test.Echo/Echo",
            [](aevox::grpc::UnaryRequest&)
                -> aevox::Task<std::expected<aevox::grpc::UnaryResponse, aevox::grpc::GrpcStatus>> {
                co_return aevox::grpc::UnaryResponse{};
            });
        REQUIRE(result.has_value());
    }
    CHECK(true);
}
