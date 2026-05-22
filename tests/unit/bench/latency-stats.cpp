// Benchmark stats helper unit tests.
// ADD ref: Tasks/architecture/AEV-015-arch.md §8.1

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <vector>

#include "support/bench_stats.hpp"

TEST_CASE("Benchmark stats - empty latency samples return zeroes", "[bench]")
{
    const std::vector<std::chrono::nanoseconds> samples;

    const auto summary = aevox::test_support::summarize_latency(samples);

    CHECK(summary.p50 == std::chrono::nanoseconds{0});
    CHECK(summary.p99 == std::chrono::nanoseconds{0});
    CHECK(summary.p999 == std::chrono::nanoseconds{0});
}

TEST_CASE("Benchmark stats - percentile summary sorts unordered samples", "[bench]")
{
    const std::array samples{std::chrono::nanoseconds{10}, std::chrono::nanoseconds{1},
                             std::chrono::nanoseconds{100}, std::chrono::nanoseconds{50},
                             std::chrono::nanoseconds{20}};

    const auto summary = aevox::test_support::summarize_latency(samples);

    CHECK(summary.p50 == std::chrono::nanoseconds{20});
    CHECK(summary.p99 == std::chrono::nanoseconds{100});
    CHECK(summary.p999 == std::chrono::nanoseconds{100});
}

TEST_CASE("Benchmark stats - percentile summary handles a single sample", "[bench]")
{
    const std::array samples{std::chrono::nanoseconds{42}};

    const auto summary = aevox::test_support::summarize_latency(samples);

    CHECK(summary.p50 == std::chrono::nanoseconds{42});
    CHECK(summary.p99 == std::chrono::nanoseconds{42});
    CHECK(summary.p999 == std::chrono::nanoseconds{42});
}

TEST_CASE("Benchmark stats - operations per second handles zero inputs", "[bench]")
{
    CHECK(aevox::test_support::operations_per_second(0U, std::chrono::seconds{1}) == 0.0);
    CHECK(aevox::test_support::operations_per_second(1U, std::chrono::nanoseconds{0}) == 0.0);
    CHECK(aevox::test_support::operations_per_second(1U, std::chrono::nanoseconds{-1}) == 0.0);
}

TEST_CASE("Benchmark stats - operations per second reports finite rate", "[bench]")
{
    CHECK(aevox::test_support::operations_per_second(200U, std::chrono::seconds{2}) == 100.0);
}
