#pragma once
// Test-only helpers for benchmark summary reporting.
// ADD ref: Tasks/architecture/AEV-015-arch.md §3

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace aevox::test_support {

struct LatencySummary
{
    std::chrono::nanoseconds p50{};
    std::chrono::nanoseconds p99{};
    std::chrono::nanoseconds p999{};
};

[[nodiscard]] inline LatencySummary summarize_latency(
    std::span<const std::chrono::nanoseconds> samples) noexcept
{
    if (samples.empty()) {
        return {};
    }

    std::vector<std::chrono::nanoseconds> sorted{samples.begin(), samples.end()};
    std::ranges::sort(sorted);

    const auto nearest_rank = [&sorted](double percentile) noexcept {
        const auto rank =
            static_cast<std::size_t>(std::ceil(percentile * static_cast<double>(sorted.size()))) -
            1U;
        return sorted[rank];
    };

    return {
        .p50  = nearest_rank(0.50),
        .p99  = nearest_rank(0.99),
        .p999 = nearest_rank(0.999),
    };
}

[[nodiscard]] inline double operations_per_second(std::uint64_t            operations,
                                                  std::chrono::nanoseconds elapsed) noexcept
{
    if (operations == 0U || elapsed <= std::chrono::nanoseconds::zero()) {
        return 0.0;
    }

    const auto seconds = std::chrono::duration<double>{elapsed}.count();
    return static_cast<double>(operations) / seconds;
}

} // namespace aevox::test_support
