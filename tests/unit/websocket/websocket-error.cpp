// Unit tests: aevox::WebSocketError structured error type.
// ADD ref: Tasks/architecture/AEV-010-arch.md §8 (Unit Tests)
#include <aevox/websocket_error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>

TEST_CASE("WebSocketError - message accessible and non-empty", "[websocket]")
{
    const aevox::WebSocketError err{aevox::WebSocketErrorCode::InvalidHandshake,
                                    "Missing Sec-WebSocket-Key header"};

    // message() should return a non-empty view.
    REQUIRE_FALSE(err.message().empty());
    REQUIRE(err.message() == "Missing Sec-WebSocket-Key header");
    REQUIRE(err.code() == aevox::WebSocketErrorCode::InvalidHandshake);
    REQUIRE(aevox::category(err) == aevox::ErrorCategory::Protocol);
}

TEST_CASE("WebSocketError - code identifiable with all defined enum values", "[websocket]")
{
    // Verify all five enum values are distinct and that code() returns the
    // value passed to the constructor.
    using EC = aevox::WebSocketErrorCode;

    const auto codes = {
        EC::InvalidHandshake, EC::ProtocolError, EC::Closed, EC::SendFailed, EC::FrameTooLarge,
    };

    // Verify each code is round-tripped correctly.
    for (auto c : codes) {
        const aevox::WebSocketError err{c, "test"};
        REQUIRE(err.code() == c);
        REQUIRE_FALSE(aevox::to_string(c).empty());
    }

    // Verify all codes are distinct (no two are equal).
    const std::array<EC, 5> arr{
        EC::InvalidHandshake, EC::ProtocolError, EC::Closed, EC::SendFailed, EC::FrameTooLarge,
    };
    const auto arr_span = std::span<const EC>{arr};
    for (std::size_t i = 0; i < arr_span.size(); ++i) {
        for (std::size_t j = i + 1; j < arr_span.size(); ++j) {
            REQUIRE(arr_span[i] != arr_span[j]);
        }
    }
}

TEST_CASE("WebSocketError - code categories are exhaustive", "[websocket]")
{
    using EC = aevox::WebSocketErrorCode;

    CHECK(aevox::category(EC::InvalidHandshake) == aevox::ErrorCategory::Protocol);
    CHECK(aevox::category(EC::ProtocolError) == aevox::ErrorCategory::Protocol);
    CHECK(aevox::category(EC::Closed) == aevox::ErrorCategory::State);
    CHECK(aevox::category(EC::SendFailed) == aevox::ErrorCategory::Io);
    CHECK(aevox::category(EC::FrameTooLarge) == aevox::ErrorCategory::Validation);
}
