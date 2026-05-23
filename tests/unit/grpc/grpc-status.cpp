// gRPC status and error helper tests.
#include <aevox/plugin.hpp>
#include <aevox/plugins/grpc.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("grpc status - ok has StatusCode Ok", "[grpc]")
{
    const auto status = aevox::grpc::GrpcStatus::ok();
    CHECK(status.code == aevox::grpc::StatusCode::Ok);
    CHECK(status.message.empty());
}

TEST_CASE("grpc status - error stores code and message", "[grpc]")
{
    const auto status =
        aevox::grpc::GrpcStatus::error(aevox::grpc::StatusCode::InvalidArgument, "bad input");
    CHECK(status.code == aevox::grpc::StatusCode::InvalidArgument);
    CHECK(status.message == "bad input");
}

TEST_CASE("grpc error - to_string returns non-empty text", "[grpc]")
{
    CHECK_FALSE(aevox::grpc::to_string(aevox::grpc::GrpcError::InvalidConfig).empty());
}

TEST_CASE("grpc error - category maps validation and protocol errors", "[grpc]")
{
    CHECK(aevox::grpc::category(aevox::grpc::GrpcError::InvalidConfig) ==
          aevox::ErrorCategory::Validation);
    CHECK(aevox::grpc::category(aevox::grpc::GrpcError::ProtocolError) ==
          aevox::ErrorCategory::Protocol);
}

TEST_CASE("plugin error - to_string returns non-empty text", "[core]")
{
    CHECK_FALSE(aevox::to_string(aevox::PluginError::InvalidArgument).empty());
}

TEST_CASE("plugin error - category maps lifecycle errors", "[core]")
{
    CHECK(aevox::category(aevox::PluginError::InvalidArgument) == aevox::ErrorCategory::Validation);
    CHECK(aevox::category(aevox::PluginError::AlreadyRunning) == aevox::ErrorCategory::State);
}
