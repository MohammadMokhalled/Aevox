// Unit tests: aevox::net::TopicBus pub/sub registry.
// ADD ref: Tasks/architecture/AEV-010-arch.md §8 (Unit Tests)
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <barrier>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "net/topic_bus.hpp"

// =============================================================================
// MockSubscriber — minimal TopicSubscriber for testing.
// Records delivered messages.
// =============================================================================

class MockSubscriber : public aevox::net::TopicSubscriber
{
public:
    void send_from_bus(std::string_view message) override
    {
        ++delivery_count;
        last_message = std::string{message};
    }

    std::atomic<int> delivery_count{0};
    std::string      last_message;
};

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("TopicBus - subscribe and publish on same topic", "[websocket]")
{
    aevox::net::TopicBus bus;

    auto sub1 = std::make_shared<MockSubscriber>();
    auto sub2 = std::make_shared<MockSubscriber>();

    bus.subscribe("room1", sub1);
    bus.subscribe("room1", sub2);

    // Publish from a null sender (no self-suppression).
    const std::size_t delivered = bus.publish("room1", "hello");

    REQUIRE(delivered == 2);
    REQUIRE(sub1->delivery_count.load() == 1);
    REQUIRE(sub2->delivery_count.load() == 1);
    REQUIRE(sub1->last_message == "hello");
    REQUIRE(sub2->last_message == "hello");
}

TEST_CASE("TopicBus - publish to empty topic delivers nothing", "[websocket]")
{
    aevox::net::TopicBus bus;

    // No subscribers registered — publish should return 0 and not crash.
    const std::size_t delivered = bus.publish("nonexistent-topic", "msg");
    REQUIRE(delivered == 0);
}

TEST_CASE("TopicBus - concurrent subscribe and publish is race-free", "[websocket]")
{
    // Launch 8 std::jthread workers: 4 subscribing, 4 publishing to the same
    // topic simultaneously. Coordinated via std::barrier. Should not crash or
    // produce data races under AddressSanitizer.

    using namespace std::chrono_literals;
    constexpr int kWorkers    = 8;
    constexpr int kIterations = 100;

    aevox::net::TopicBus bus;

    // Use a barrier with kWorkers participants.
    std::barrier     sync_point{kWorkers};
    std::atomic<int> total_publishes{0};

    std::vector<std::jthread> workers;
    workers.reserve(static_cast<std::size_t>(kWorkers));

    for (int i = 0; i < kWorkers; ++i) {
        workers.emplace_back([&, i] {
            // Each thread creates its own subscriber.
            auto sub = std::make_shared<MockSubscriber>();

            // Wait for all threads to be ready before starting.
            sync_point.arrive_and_wait();

            if (i < kWorkers / 2) {
                // Subscribing threads: subscribe to "shared-topic" kIterations times.
                for (int iter = 0; iter < kIterations; ++iter) {
                    bus.subscribe("shared-topic", sub);
                }
            }
            else {
                // Publishing threads: publish to "shared-topic" kIterations times.
                for (int iter = 0; iter < kIterations; ++iter) {
                    total_publishes.fetch_add(static_cast<int>(bus.publish("shared-topic", "ping")),
                                              std::memory_order_relaxed);
                }
            }

            // Wait for all threads to finish before destructing.
            sync_point.arrive_and_wait();
        });
    }

    // All workers join on destruction (std::jthread).
    workers.clear();

    // The test passes if no crash / ASAN violation occurred.
    // total_publishes may be any non-negative value.
    REQUIRE(total_publishes.load() >= 0);
}
