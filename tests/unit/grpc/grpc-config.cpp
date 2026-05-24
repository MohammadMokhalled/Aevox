// gRPC plugin configuration and unary registration tests.
#include <aevox/app.hpp>
#include <aevox/plugins/grpc.hpp>

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <memory>

namespace {

aevox::grpc::UnaryHandler noop_handler()
{
    return [](aevox::grpc::UnaryRequest&)
               -> aevox::Task<std::expected<aevox::grpc::UnaryResponse, aevox::grpc::GrpcStatus>> {
        co_return aevox::grpc::UnaryResponse{};
    };
}

} // namespace

TEST_CASE("grpc config - default config is valid", "[grpc]")
{
    aevox::grpc::Plugin plugin;
    auto                result = plugin.add_unary_method("/aevox.test.Echo/Echo", noop_handler());
    CHECK(result.has_value());
}

TEST_CASE("grpc config - zero port is rejected", "[grpc]")
{
    auto       plugin = std::make_unique<aevox::grpc::Plugin>(aevox::grpc::PluginConfig{.port = 0});
    aevox::App app;
    auto       result = app.install(std::move(plugin));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == aevox::PluginError::InvalidArgument);
}

TEST_CASE("grpc config - zero max concurrent streams is rejected", "[grpc]")
{
    aevox::grpc::PluginConfig config;
    config.max_concurrent_streams = 0;
    auto       plugin             = std::make_unique<aevox::grpc::Plugin>(config);
    aevox::App app;
    auto       result = app.install(std::move(plugin));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == aevox::PluginError::InvalidArgument);
}

TEST_CASE("grpc config - zero max message size is rejected", "[grpc]")
{
    aevox::grpc::PluginConfig config;
    config.max_message_size = 0;
    auto       plugin       = std::make_unique<aevox::grpc::Plugin>(config);
    aevox::App app;
    auto       result = app.install(std::move(plugin));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == aevox::PluginError::InvalidArgument);
}

TEST_CASE("grpc plugin - unary method registration accepts canonical path", "[grpc]")
{
    aevox::grpc::Plugin plugin;
    auto                result = plugin.add_unary_method("/aevox.test.Echo/Echo", noop_handler());
    CHECK(result.has_value());
}

TEST_CASE("grpc plugin - unary method registration rejects malformed path", "[grpc]")
{
    aevox::grpc::Plugin plugin;
    auto                result = plugin.add_unary_method("aevox.test.Echo/Echo", noop_handler());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == aevox::grpc::GrpcError::InvalidMethod);
}

TEST_CASE("grpc plugin - duplicate unary method returns DuplicateMethod", "[grpc]")
{
    aevox::grpc::Plugin plugin;
    auto                first = plugin.add_unary_method("/aevox.test.Echo/Echo", noop_handler());
    REQUIRE(first.has_value());

    auto second = plugin.add_unary_method("/aevox.test.Echo/Echo", noop_handler());
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error() == aevox::grpc::GrpcError::DuplicateMethod);
}

TEST_CASE("grpc plugin - registration after install is rejected", "[grpc]")
{
    auto       plugin = std::make_unique<aevox::grpc::Plugin>();
    auto*      raw    = plugin.get();
    aevox::App app;
    auto       installed = app.install(std::move(plugin));
    REQUIRE(installed.has_value());

    auto result = raw->add_unary_method("/aevox.test.Echo/Echo", noop_handler());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == aevox::grpc::GrpcError::AlreadyInstalled);
}
