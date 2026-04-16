// AEV-006: Integration tests for thread pool, CPU offload, and when_all concurrency.
// ADD ref: Tasks/architecture/AEV-006-arch.md § Test Architecture §8.2
//
// Uses real AsioExecutor with 4 I/O threads and 2 CPU threads.
// All async helpers (pool, sleep, when_all) are exercised via real connection handlers.
// Tests are designed to be run under AddressSanitizer + ThreadSanitizer.

#include <aevox/async.hpp>
#include <aevox/executor.hpp>
#include <aevox/task.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <latch>
#include <string>
#include <thread>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static aevox::ExecutorConfig integration_config()
{
    return {.thread_count = 4, .cpu_pool_threads = 2, .drain_timeout = 5s};
}

static std::uint16_t find_free_port()
{
    asio::io_context        ioc;
    asio::ip::tcp::acceptor a{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return a.local_endpoint().port();
}

static void tcp_connect(std::uint16_t port)
{
    asio::io_context      ioc;
    asio::ip::tcp::socket s{ioc};
    asio::error_code      ec;
    s.connect(asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port}, ec);
}

// ---------------------------------------------------------------------------

TEST_CASE("AEV-006 integration: 1000 concurrent coroutines — all complete",
          "[integration][net][thread-safety]")
{
    constexpr int N = 1000;

    std::atomic<int> handled{0};
    std::latch       all_started{N};

    auto ex   = aevox::make_executor(integration_config());
    auto port = find_free_port();

    auto lr = ex->listen(port, [&](std::uint64_t) -> aevox::Task<void> {
        all_started.count_down();
        // Tiny sleep to create a concurrent coroutine pile.
        co_await aevox::sleep(1ms);
        handled.fetch_add(1, std::memory_order_relaxed);
        co_return;
    });
    REQUIRE(lr.has_value());

    std::jthread driver{[&ex, port] {
        // Fire N connections.
        for (int i = 0; i < N; ++i)
            tcp_connect(port);
        // Wait for all to start, then give them time to complete.
        std::this_thread::sleep_for(500ms);
        ex->stop();
    }};

    auto run_result = ex->run();
    REQUIRE(run_result.has_value());
    REQUIRE(handled.load() == N);
}

TEST_CASE("AEV-006 integration: pool() does not block I/O threads", "[integration][net]")
{
    // Send two connections. The first handler runs a 100ms CPU task via pool().
    // The second handler must be accepted and run concurrently — not blocked.

    std::atomic<int> order{0};
    std::atomic<int> h1_order{0};
    std::atomic<int> h2_order{0};
    std::latch       h2_started{1};

    auto ex   = aevox::make_executor(integration_config());
    auto port = find_free_port();

    auto lr = ex->listen(port, [&](std::uint64_t id) -> aevox::Task<void> {
        if (id == 0) {
            // Handler 1: CPU offload — should NOT block I/O threads.
            co_await aevox::pool([&] {
                // Wait for handler 2 to start before returning, proving
                // the I/O thread was free to accept and run handler 2.
                h2_started.wait();
                std::this_thread::sleep_for(20ms);
            });
            h1_order = order.fetch_add(1, std::memory_order_relaxed) + 1;
        }
        else {
            // Handler 2: started while handler 1 is on the CPU pool.
            h2_started.count_down();
            h2_order = order.fetch_add(1, std::memory_order_relaxed) + 1;
        }
        co_return;
    });
    REQUIRE(lr.has_value());

    std::jthread driver{[&ex, port] {
        std::this_thread::sleep_for(10ms);
        tcp_connect(port); // triggers handler 0
        std::this_thread::sleep_for(5ms);
        tcp_connect(port); // triggers handler 1 (while handler 0 is on CPU pool)
        std::this_thread::sleep_for(300ms);
        ex->stop();
    }};

    auto run_result = ex->run();
    REQUIRE(run_result.has_value());

    // Handler 2 started while handler 1 was on the CPU pool.
    // Therefore handler 2 must have completed first (or simultaneously).
    // The ordering: h2_order < h1_order proves non-blocking.
    INFO("h1_order=" << h1_order.load() << " h2_order=" << h2_order.load());
    REQUIRE(h2_order.load() < h1_order.load());
}

TEST_CASE("AEV-006 integration: thread-safety — 100 concurrent pool() calls",
          "[integration][net][thread-safety]")
{
    constexpr int N = 100;

    std::atomic<int> sum{0};

    auto ex   = aevox::make_executor(integration_config());
    auto port = find_free_port();

    auto lr = ex->listen(port, [&](std::uint64_t) -> aevox::Task<void> {
        // Each handler calls pool() with a simple computation.
        int val = co_await aevox::pool([&] {
            // Simulate brief CPU work.
            int x = 0;
            for (int i = 0; i < 1000; ++i)
                x += i;
            return x;
        });
        sum.fetch_add(val, std::memory_order_relaxed);
        co_return;
    });
    REQUIRE(lr.has_value());

    std::jthread driver{[&ex, port] {
        for (int i = 0; i < N; ++i)
            tcp_connect(port);
        std::this_thread::sleep_for(500ms);
        ex->stop();
    }};

    auto run_result = ex->run();
    REQUIRE(run_result.has_value());

    // Each pool() call computes sum 0..999 = 499500. N calls → N * 499500.
    constexpr int expected_sum = N * 499500;
    REQUIRE(sum.load() == expected_sum);
}

TEST_CASE("AEV-006 integration: when_all — concurrent sleep-based tasks",
          "[integration][net][when_all]")
{
    std::string result_a;
    std::string result_b;

    auto ex   = aevox::make_executor(integration_config());
    auto port = find_free_port();

    auto lr = ex->listen(port, [&](std::uint64_t) -> aevox::Task<void> {
        auto task_a = [&]() -> aevox::Task<std::string> {
            co_await aevox::sleep(15ms);
            co_return std::string{"hello"};
        };
        auto task_b = [&]() -> aevox::Task<std::string> {
            co_await aevox::sleep(15ms);
            co_return std::string{"world"};
        };

        auto [a, b] = co_await aevox::when_all(task_a(), task_b());
        result_a    = a;
        result_b    = b;
        co_return;
    });
    REQUIRE(lr.has_value());

    std::jthread driver{[&ex, port] {
        std::this_thread::sleep_for(10ms);
        tcp_connect(port);
        std::this_thread::sleep_for(300ms);
        ex->stop();
    }};

    auto run_result = ex->run();
    REQUIRE(run_result.has_value());
    REQUIRE(result_a == "hello");
    REQUIRE(result_b == "world");
}
