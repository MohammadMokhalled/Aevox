// traceparent-parse-invalid.cpp: verify invalid traceparent headers are rejected
// ADD ref: Tasks/architecture/AEV-012-arch.md §8.1

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string_view>

#include "http/traceparent.hpp"

TEST_CASE("traceparent parse - empty header returns nullopt", "[http][tracing]")
{
    const auto result = aevox::detail::parse_traceparent("");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("traceparent parse - wrong length returns nullopt", "[http][tracing]")
{
    // Too short
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-0")
            .has_value());
    // Too long (extra char at end)
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01x")
            .has_value());
    // 54 chars (one short)
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b-01")
            .has_value());
}

TEST_CASE("traceparent parse - wrong version returns nullopt", "[http][tracing]")
{
    // Version "01" is not supported
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("01-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01")
            .has_value());
    // Version "ff" is not supported
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("ff-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01")
            .has_value());
}

TEST_CASE("traceparent parse - non-hex chars returns nullopt", "[http][tracing]")
{
    // Non-hex in trace-id position
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("00-4bf92f3577b34da6a3ce929d0e0e47g6-00f067aa0ba902b7-01")
            .has_value());
    // Non-hex in parent-id position
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902z7-01")
            .has_value());
    // Non-hex in flags position
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-xy")
            .has_value());
    // Space in trace-id
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("00-4bf92f3577b34da6a3ce 929d0e0e4736-00f067aa0ba902b7-01")
            .has_value());
}

TEST_CASE("traceparent parse - all-zero trace_id returns nullopt", "[http][tracing]")
{
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("00-00000000000000000000000000000000-00f067aa0ba902b7-01")
            .has_value());
}

TEST_CASE("traceparent parse - all-zero parent_id returns nullopt", "[http][tracing]")
{
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("00-4bf92f3577b34da6a3ce929d0e0e4736-0000000000000000-01")
            .has_value());
}

TEST_CASE("traceparent parse - missing separators returns nullopt", "[http][tracing]")
{
    // No separators at all
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("004bf92f3577b34da6a3ce929d0e0e473600f067aa0ba902b701")
            .has_value());
    // First separator missing
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("004bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01")
            .has_value());
    // Second separator missing
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("00-4bf92f3577b34da6a3ce929d0e0e473600f067aa0ba902b7-01")
            .has_value());
    // Third separator missing
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b701")
            .has_value());
    // Wrong separator character
    REQUIRE_FALSE(
        aevox::detail::parse_traceparent("00:4bf92f3577b34da6a3ce929d0e0e4736:00f067aa0ba902b7:01")
            .has_value());
}
