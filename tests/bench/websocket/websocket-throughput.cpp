// WebSocket benchmarks: frame parse throughput, topic-bus fanout.
// Establishes baselines for AEV-015 performance regression gate.
// ADD ref: Tasks/architecture/AEV-010-arch.md §8 (Benchmarks)

#include <nanobench.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "net/topic_bus.hpp"
#include "net/websocket_frame.hpp"

// =============================================================================
// MockSubscriber for topic bus bench
// =============================================================================

namespace {

class BenchSubscriber : public aevox::net::TopicSubscriber
{
public:
    void send_from_bus(std::string_view /*message*/) override
    {
        ++count;
    }

    std::size_t count{0};
};

// Pre-build a 64-byte unmasked server-to-client text frame for parsing bench.
std::vector<std::byte> build_64b_unmasked_frame()
{
    std::vector<std::byte> payload(64, std::byte{'A'});
    return aevox::net::emit_frame(aevox::net::Opcode::Text, true,
                                  std::span<const std::byte>{payload});
}

} // anonymous namespace

// =============================================================================
// main() — nanobench runs, no TEST_CASE wrapper (consistent with other bench files)
// =============================================================================

int main()
{
    // -------------------------------------------------------------------------
    // Baseline 1: frame parse throughput (no I/O — pre-built buffer)
    // -------------------------------------------------------------------------

    const auto                       frame_64b = build_64b_unmasked_frame();
    const std::span<const std::byte> frame_span{frame_64b};
    constexpr std::size_t            kMaxPayload = 10UZ * 1024UZ * 1024UZ;

    ankerl::nanobench::Bench().minEpochIterations(1000000).run(
        "WebSocket frame parse - 64B unmasked", [&] {
            auto result =
                aevox::net::parse_frame(frame_span, kMaxPayload, false /* expect_masked */);
            ankerl::nanobench::doNotOptimizeAway(result);
        });

    // -------------------------------------------------------------------------
    // Baseline 2: topic bus publish fan-out to 100 subscribers
    // -------------------------------------------------------------------------

    aevox::net::TopicBus                          bus;
    std::vector<std::shared_ptr<BenchSubscriber>> subs;
    subs.reserve(100);
    for (int i = 0; i < 100; ++i) {
        auto sub = std::make_shared<BenchSubscriber>();
        bus.subscribe("bench-topic", sub);
        subs.push_back(std::move(sub));
    }

    ankerl::nanobench::Bench().minEpochIterations(1000).run(
        "TopicBus publish - 100 subscribers", [&] {
            const std::size_t delivered = bus.publish("bench-topic", "hello", nullptr);
            ankerl::nanobench::doNotOptimizeAway(delivered);
        });

    return 0;
}
