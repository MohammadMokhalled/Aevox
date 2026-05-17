#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/error.hpp>
#include <aevox/executor.hpp>
#include <aevox/json_error.hpp>
#include <aevox/request.hpp>
#include <aevox/tcp_stream.hpp>
#include <aevox/websocket_error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <optional>
#include <string_view>
#include <type_traits>

namespace {

struct ErrorModelDto
{
    int value{};
};

} // namespace

TEST_CASE("Error model - category labels are stable", "[error]")
{
    CHECK(aevox::to_string(aevox::ErrorCategory::Io) == "io");
    CHECK(aevox::to_string(aevox::ErrorCategory::Protocol) == "protocol");
    CHECK(aevox::to_string(aevox::ErrorCategory::Parse) == "parse");
    CHECK(aevox::to_string(aevox::ErrorCategory::Validation) == "validation");
    CHECK(aevox::to_string(aevox::ErrorCategory::NotFound) == "not found");
    CHECK(aevox::to_string(aevox::ErrorCategory::Serialization) == "serialization");
    CHECK(aevox::to_string(aevox::ErrorCategory::State) == "state");
    CHECK(aevox::to_string(aevox::ErrorCategory::Unknown) == "unknown");
}

TEST_CASE("Error model - module errors map to categories", "[error]")
{
    CHECK(aevox::category(aevox::IoError::Eof) == aevox::ErrorCategory::Io);
    CHECK(aevox::category(aevox::IoError::Cancelled) == aevox::ErrorCategory::State);
    CHECK(aevox::category(aevox::IoError::Reset) == aevox::ErrorCategory::Io);
    CHECK(aevox::category(aevox::IoError::Timeout) == aevox::ErrorCategory::Io);
    CHECK(aevox::category(aevox::IoError::Unknown) == aevox::ErrorCategory::Unknown);

    CHECK(aevox::category(aevox::ExecutorError::BindFailed) == aevox::ErrorCategory::Io);
    CHECK(aevox::category(aevox::ExecutorError::ListenFailed) == aevox::ErrorCategory::Io);
    CHECK(aevox::category(aevox::ExecutorError::AcceptFailed) == aevox::ErrorCategory::Io);
    CHECK(aevox::category(aevox::ExecutorError::AlreadyRunning) == aevox::ErrorCategory::State);
    CHECK(aevox::category(aevox::ExecutorError::NotRunning) == aevox::ErrorCategory::State);

    CHECK(aevox::category(aevox::ParamError::NotFound) == aevox::ErrorCategory::NotFound);
    CHECK(aevox::category(aevox::ParamError::BadConversion) == aevox::ErrorCategory::Validation);

    CHECK(aevox::category(aevox::ConfigError::FileNotFound) == aevox::ErrorCategory::NotFound);
    CHECK(aevox::category(aevox::ConfigError::ParseError) == aevox::ErrorCategory::Parse);
    CHECK(aevox::category(aevox::ConfigError::InvalidValue) == aevox::ErrorCategory::Validation);

    CHECK(aevox::category(aevox::JsonErrorCode::ParseError) == aevox::ErrorCategory::Parse);
    CHECK(aevox::category(aevox::JsonErrorCode::TypeMismatch) == aevox::ErrorCategory::Validation);
    CHECK(aevox::category(aevox::JsonErrorCode::MissingField) == aevox::ErrorCategory::Validation);
    CHECK(aevox::category(aevox::JsonErrorCode::SerializationFailed) ==
          aevox::ErrorCategory::Serialization);
    CHECK(aevox::category(aevox::JsonErrorCode::InvalidUtf8) == aevox::ErrorCategory::Validation);
    CHECK(aevox::category(aevox::JsonErrorCode::Unknown) == aevox::ErrorCategory::Unknown);

    CHECK(aevox::category(aevox::WebSocketErrorCode::InvalidHandshake) ==
          aevox::ErrorCategory::Protocol);
    CHECK(aevox::category(aevox::WebSocketErrorCode::ProtocolError) ==
          aevox::ErrorCategory::Protocol);
    CHECK(aevox::category(aevox::WebSocketErrorCode::Closed) == aevox::ErrorCategory::State);
    CHECK(aevox::category(aevox::WebSocketErrorCode::SendFailed) == aevox::ErrorCategory::Io);
    CHECK(aevox::category(aevox::WebSocketErrorCode::FrameTooLarge) ==
          aevox::ErrorCategory::Validation);
}

TEST_CASE("Error model - public error messages are non-empty", "[error]")
{
    CHECK_FALSE(aevox::to_string(aevox::IoError::Eof).empty());
    CHECK_FALSE(aevox::to_string(aevox::ExecutorError::BindFailed).empty());
    CHECK_FALSE(aevox::to_string(aevox::ParamError::NotFound).empty());
    CHECK_FALSE(aevox::to_string(aevox::ConfigError::FileNotFound).empty());
    CHECK_FALSE(aevox::to_string(aevox::JsonErrorCode::ParseError).empty());
    CHECK_FALSE(aevox::to_string(aevox::WebSocketErrorCode::InvalidHandshake).empty());
}

TEST_CASE("Error model - fallible public signatures are expected based", "[error]")
{
    using AppCreateResult = decltype(aevox::App::create({}, std::nullopt));
    static_assert(
        std::same_as<AppCreateResult, std::expected<aevox::App, aevox::ConfigErrorDetail>>);

    using ParamResult =
        decltype(std::declval<const aevox::Request&>().param<int>(std::string_view{}));
    static_assert(std::same_as<ParamResult, std::expected<int, aevox::ParamError>>);

    using JsonResult = decltype(std::declval<const aevox::Request&>().json<ErrorModelDto>());
    static_assert(
        std::same_as<JsonResult, aevox::Task<std::expected<ErrorModelDto, aevox::JsonError>>>);

    using ReadResult = decltype(std::declval<aevox::TcpStream&>().read());
    static_assert(std::same_as<ReadResult,
                               aevox::Task<std::expected<std::vector<std::byte>, aevox::IoError>>>);

    using WriteResult =
        decltype(std::declval<aevox::TcpStream&>().write(std::span<const std::byte>{}));
    static_assert(std::same_as<WriteResult, aevox::Task<std::expected<void, aevox::IoError>>>);

    using HeaderResult = decltype(std::declval<const aevox::Request&>().header(std::string_view{}));
    static_assert(std::same_as<HeaderResult, std::optional<std::string_view>>);

    static_assert(std::is_trivially_copyable_v<aevox::ErrorCategory>);
    static_assert(std::is_trivially_copyable_v<aevox::IoError>);
    static_assert(std::is_trivially_copyable_v<aevox::ExecutorError>);
    static_assert(std::is_trivially_copyable_v<aevox::ParamError>);
    static_assert(std::is_trivially_copyable_v<aevox::ConfigError>);
    static_assert(std::is_trivially_copyable_v<aevox::JsonErrorCode>);
    static_assert(std::is_trivially_copyable_v<aevox::WebSocketErrorCode>);

    REQUIRE(true);
}
