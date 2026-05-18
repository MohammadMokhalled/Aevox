// traceparent-parse-valid.cpp: verify valid traceparent headers parse correctly
// ADD ref: Tasks/architecture/AEV-012-arch.md §8.1

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "http/traceparent.hpp"

TEST_CASE("traceparent parse - valid header round-trips", "[http][tracing]")
{
    const auto result =
        aevox::detail::parse_traceparent("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");

    if (!result) { FAIL("parse_traceparent should return a value"); }
    const auto& tp = *result;

    // trace_id
    std::string_view trace_id{tp.trace_id.data(), tp.trace_id.size()};
    REQUIRE(trace_id == "4bf92f3577b34da6a3ce929d0e0e4736");

    // parent_id
    std::string_view parent_id{tp.parent_id.data(), tp.parent_id.size()};
    REQUIRE(parent_id == "00f067aa0ba902b7");

    // flags
    REQUIRE(tp.flags == 0x01);
}

TEST_CASE("traceparent parse - flags byte variants accepted", "[http][tracing]")
{
    const auto parse_flags = [](std::string_view flags_suffix) -> std::uint8_t {
        const std::string header =
            std::format("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-{}", flags_suffix);
        const auto result = aevox::detail::parse_traceparent(header);
        if (!result) { FAIL("parse_traceparent should return a value"); }
        return result->flags;
    };

    REQUIRE(parse_flags("00") == 0x00);
    REQUIRE(parse_flags("01") == 0x01);
    REQUIRE(parse_flags("ff") == 0xff);
    REQUIRE(parse_flags("0f") == 0x0f);
    REQUIRE(parse_flags("a0") == 0xa0);
}

TEST_CASE("traceparent parse - uppercase hex accepted and normalized to lowercase",
          "[http][tracing]")
{
    const auto result =
        aevox::detail::parse_traceparent("00-4BF92F3577B34DA6A3CE929D0E0E4736-00F067AA0BA902B7-01");

    if (!result) { FAIL("parse_traceparent should return a value"); }
    const auto& tp = *result;

    std::string_view trace_id{tp.trace_id.data(), tp.trace_id.size()};
    REQUIRE(trace_id == "4bf92f3577b34da6a3ce929d0e0e4736");

    std::string_view parent_id{tp.parent_id.data(), tp.parent_id.size()};
    REQUIRE(parent_id == "00f067aa0ba902b7");
}

TEST_CASE("traceparent parse - mixed case hex accepted", "[http][tracing]")
{
    const auto result =
        aevox::detail::parse_traceparent("00-4bF92f3577B34dA6a3cE929d0e0E4736-00f067Aa0bA902b7-Ab");

    if (!result) { FAIL("parse_traceparent should return a value"); }
    REQUIRE(result->flags == 0xab);
}
