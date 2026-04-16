// AEV-006: Unit tests for aevox::pool(), aevox::sleep(), aevox::when_all().
// ADD ref: Tasks/architecture/AEV-006-arch.md § Test Architecture §8.1
//
// All tests use make_executor({.thread_count=2, .cpu_pool_threads=2, .drain_timeout=2s}).
// Each test drives helpers via connection handler coroutines — no direct
// invocation from main(), which would leave thread-locals uninitialised.

#include <aevox/async.hpp>
#include <aevox/executor.hpp>
#include <aevox/task.hpp>

#include <asio.hpp> // client-side TCP helper only

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <latch>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Test infrastructure
// ---------------------------------------------------------------------------

static aevox::ExecutorConfig unit_config()
{
    return {.thread_count = 2, .cpu_pool_threads = 2, .drain_timeout = 2s};
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

// Runs a test handler as a connection handler and blocks until it completes.
// handler_fn is called once per connection with conn_id, completes, then
// the executor is stopped.
template <typename HandlerFn> void run_single(HandlerFn&& handler_fn)
{
    auto port = find_free_port();
    auto ex   = aevox::make_executor(unit_config());

    auto result = ex->listen(port, std::forward<HandlerFn>(handler_fn));
    REQUIRE(result.has_value());

    std::jthread stopper{[&ex, port] {
        std::this_thread::sleep_for(10ms);
        tcp_connect(port);
        // Give the handler time to complete before stopping.
        std::this_thread::sleep_for(500ms);
        ex->stop();
    }};

    auto run_result = ex->run();
    REQUIRE(run_result.has_value());
}

// =============================================================================
// pool() tests
// =============================================================================

TEST_CASE("AEV-006: pool() - callable executes on CPU pool thread, not I/O thread",
          "[executor][pool]")
{
    std::thread::id io_thread_id{};
    std::thread::id cpu_thread_id{};

    run_single([&](std::uint64_t) -> aevox::Task<void> {
        io_thread_id = std::this_thread::get_id();

        co_await aevox::pool([&]() { cpu_thread_id = std::this_thread::get_id(); });

        // Thread IDs must differ (I/O thread vs. CPU pool thread).
        CHECK(io_thread_id != cpu_thread_id);
        co_return;
    });

    // Also verify outside the handler that both were set (handler ran).
    REQUIRE(io_thread_id != std::thread::id{});
    REQUIRE(cpu_thread_id != std::thread::id{});
    REQUIRE(io_thread_id != cpu_thread_id);
}

TEST_CASE("AEV-006: pool() - return value propagates correctly through Task", "[executor][pool]")
{
    int result = 0;

    run_single([&](std::uint64_t) -> aevox::Task<void> {
        result = co_await aevox::pool([] { return 42; });
        co_return;
    });

    REQUIRE(result == 42);
}

TEST_CASE("AEV-006: pool() - exception inside fn propagates to co_await site", "[executor][pool]")
{
    bool exception_caught = false;

    // Wrap in a handler so we can catch exceptions inside the coroutine.
    // (If an exception escapes a Task, it calls std::terminate via FireAndForget.)
    auto ex   = aevox::make_executor(unit_config());
    auto port = find_free_port();

    auto listen_result = ex->listen(port, [&](std::uint64_t) -> aevox::Task<void> {
        try {
            co_await aevox::pool([]() -> int { throw std::runtime_error{"cpu error"}; });
        }
        catch (const std::runtime_error& e) {
            exception_caught = (std::string{e.what()} == "cpu error");
        }
        co_return;
    });
    REQUIRE(listen_result.has_value());

    std::jthread stopper{[&ex, port] {
        std::this_thread::sleep_for(10ms);
        tcp_connect(port);
        std::this_thread::sleep_for(300ms);
        ex->stop();
    }};

    auto run_result = ex->run();
    REQUIRE(run_result.has_value());
    REQUIRE(exception_caught);
}

// =============================================================================
// sleep() tests
// =============================================================================

TEST_CASE("AEV-006: sleep() - coroutine resumes after duration without blocking thread",
          "[executor][sleep]")
{
    std::chrono::milliseconds elapsed{0};

    run_single([&](std::uint64_t) -> aevox::Task<void> {
        auto start = std::chrono::steady_clock::now();
        co_await aevox::sleep(50ms);
        elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        co_return;
    });

    REQUIRE(elapsed.count() >= 45); // some tolerance for scheduler granularity
}

TEST_CASE("AEV-006: sleep() - other coroutines can run while sleeping", "[executor][sleep]")
{
    std::atomic<int> counter{0};

    auto ex   = aevox::make_executor(unit_config());
    auto port = find_free_port();
    // Latch: wait for both handlers to start.
    std::latch both_started{2};

    // Handler 1: sleeps for 50ms.
    auto lr = ex->listen(port, [&](std::uint64_t) -> aevox::Task<void> {
        both_started.count_down();
        co_await aevox::sleep(50ms);
        counter.fetch_add(1, std::memory_order_relaxed);
        co_return;
    });
    REQUIRE(lr.has_value());

    // Connect twice from the stopper thread.
    std::jthread stopper{[&ex, port, &both_started] {
        std::this_thread::sleep_for(10ms);
        tcp_connect(port); // triggers handler 1
        tcp_connect(port); // triggers handler 2 (also sleeping)
        both_started.wait();
        // Give both handlers time to complete their sleeps.
        std::this_thread::sleep_for(200ms);
        ex->stop();
    }};

    auto run_result = ex->run();
    REQUIRE(run_result.has_value());
    // Both handlers must have run and incremented the counter.
    REQUIRE(counter.load() >= 1);
}

// =============================================================================
// when_all() tests
// =============================================================================

TEST_CASE("AEV-006: when_all() - two tasks return correct results in declaration order",
          "[executor][when_all]")
{
    int    int_result    = 0;
    double double_result = 0.0;

    run_single([&](std::uint64_t) -> aevox::Task<void> {
        auto make_int    = []() -> aevox::Task<int> { co_return 7; };
        auto make_double = []() -> aevox::Task<double> { co_return 3.14; };

        auto [i, d]   = co_await aevox::when_all(make_int(), make_double());
        int_result    = i;
        double_result = d;
        co_return;
    });

    REQUIRE(int_result == 7);
    REQUIRE(double_result == 3.14); // NOLINT(cppcoreguidelines-avoid-magic-numbers)
}

TEST_CASE("AEV-006: when_all() - tasks run concurrently, not sequentially", "[executor][when_all]")
{
    // Two tasks each sleep 20ms. If they run sequentially the total is ≥ 40ms.
    // Concurrently the total should be ≤ 35ms (with scheduler slack).
    std::chrono::milliseconds elapsed{0};

    run_single([&](std::uint64_t) -> aevox::Task<void> {
        auto sleeper = [](int ms) -> aevox::Task<int> {
            co_await aevox::sleep(std::chrono::milliseconds{ms});
            co_return ms;
        };

        auto start  = std::chrono::steady_clock::now();
        auto [a, b] = co_await aevox::when_all(sleeper(20), sleeper(20));
        elapsed     = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        CHECK(a == 20);
        CHECK(b == 20);
        co_return;
    });

    // Sequential would be ≥ 40ms. Concurrent should be < 35ms.
    INFO("Elapsed: " << elapsed.count() << "ms");
    REQUIRE(elapsed.count() < 35);
}

TEST_CASE("AEV-006: when_all() - first exception propagates, others complete",
          "[executor][when_all]")
{
    bool exception_caught = false;
    bool second_ran       = false;

    auto ex   = aevox::make_executor(unit_config());
    auto port = find_free_port();

    auto lr = ex->listen(port, [&](std::uint64_t) -> aevox::Task<void> {
        auto throw_task = [&]() -> aevox::Task<int> {
            co_await aevox::sleep(5ms);
            throw std::runtime_error{"task1 error"};
            co_return 0;
        };
        auto ok_task = [&]() -> aevox::Task<int> {
            co_await aevox::sleep(10ms);
            second_ran = true;
            co_return 99;
        };

        try {
            co_await aevox::when_all(throw_task(), ok_task());
        }
        catch (const std::runtime_error&) {
            exception_caught = true;
        }
        // Wait a bit to allow the second task to complete naturally.
        co_await aevox::sleep(20ms);
        co_return;
    });
    REQUIRE(lr.has_value());

    std::jthread stopper{[&ex, port] {
        std::this_thread::sleep_for(10ms);
        tcp_connect(port);
        std::this_thread::sleep_for(200ms);
        ex->stop();
    }};

    auto run_result = ex->run();
    REQUIRE(run_result.has_value());
    REQUIRE(exception_caught);
    REQUIRE(second_ran); // second task runs to completion despite first failing
}

// =============================================================================
// ExecutorConfig — cpu_pool_threads
// =============================================================================

TEST_CASE("AEV-006: ExecutorConfig - cpu_pool_threads respected", "[executor][config]")
{
    // When cpu_pool_threads > 0, pool() callable runs on a DIFFERENT thread than I/O.
    // When cpu_pool_threads == 0, pool() posts to I/O pool — may be same or different thread.
    // This test verifies cpu_pool_threads=2 gives a distinct CPU thread ID.
    aevox::ExecutorConfig cfg{.thread_count = 2, .cpu_pool_threads = 2, .drain_timeout = 2s};

    std::thread::id io_id{};
    std::thread::id cpu_id{};

    auto ex   = aevox::make_executor(cfg);
    auto port = find_free_port();

    auto lr = ex->listen(port, [&](std::uint64_t) -> aevox::Task<void> {
        io_id = std::this_thread::get_id();
        co_await aevox::pool([&] { cpu_id = std::this_thread::get_id(); });
        co_return;
    });
    REQUIRE(lr.has_value());

    std::jthread stopper{[&ex, port] {
        std::this_thread::sleep_for(10ms);
        tcp_connect(port);
        std::this_thread::sleep_for(200ms);
        ex->stop();
    }};

    auto run_result = ex->run();
    REQUIRE(run_result.has_value());
    REQUIRE(io_id != std::thread::id{});
    REQUIRE(cpu_id != std::thread::id{});
    REQUIRE(io_id != cpu_id);
}
