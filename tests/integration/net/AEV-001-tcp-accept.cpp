// AEV-001: Integration tests for the TCP accept loop.
// ADD ref: Tasks/architecture/AEV-001-arch.md § Test Architecture (Rev.2)
//
// Setup: make_executor({.thread_count=2, .drain_timeout=2s}), listen on port 0
// (OS-assigned), run() on a background jthread. Tests connect via loopback.
//
// No Asio types are used to test framework behaviour — Asio is used on the
// CLIENT side only (to make TCP connections), not to test the server internals.

#include <catch2/catch_test_macros.hpp>

#include <aevox/executor.hpp>
#include <aevox/task.hpp>

#include <asio.hpp>   // client-side connection helper only

#include <atomic>
#include <chrono>
#include <latch>
#include <thread>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static aevox::ExecutorConfig int_test_config() {
    return {.thread_count = 2, .drain_timeout = 2s};
}

// Query the OS-assigned port from the acceptor.
// We cannot ask aevox::Executor for this (not in the public API yet), so we
// pass the port via an out-parameter set inside the handler on first connection.
// A simpler approach: use a fixed known-free port (risky on CI).
// Cleanest approach: bind a temporary acceptor to port 0, record the port, close it,
// then bind the test executor to that port (possible TOCTOU, but acceptable in tests).
static std::uint16_t find_free_port() {
    asio::io_context ioc;
    asio::ip::tcp::acceptor a{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return a.local_endpoint().port();
}

// Open a TCP connection to loopback:port and close it immediately.
static void tcp_connect(std::uint16_t port) {
    asio::io_context ioc;
    asio::ip::tcp::socket s{ioc};
    asio::error_code ec;
    s.connect(asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port}, ec);
    // Ignore ec — the server may have closed before we read; that's fine.
}

// ---------------------------------------------------------------------------

TEST_CASE("AEV-001: integration — single client connect triggers handler coroutine", "[net][integration]") {
    auto port = find_free_port();

    std::latch handler_entered{1};

    auto ex = aevox::make_executor(int_test_config());
    auto lr = ex->listen(port, [&handler_entered](std::uint64_t) -> aevox::Task<void> {
        handler_entered.count_down();
        co_return;
    });
    REQUIRE(lr.has_value());

    // Run the executor in background.
    std::jthread runner{[&ex] {
        (void)ex->run();
    }};

    // Connect from the test thread.
    tcp_connect(port);

    // Wait for the handler to fire.
    bool reached = handler_entered.try_wait_for(2s);
    REQUIRE(reached);

    ex->stop();
}

TEST_CASE("AEV-001: integration — handler receives monotonically increasing conn_id", "[net][integration]") {
    auto port = find_free_port();

    constexpr int N = 5;
    std::atomic<int> count{0};
    std::vector<std::uint64_t> ids(N);
    std::latch all_handled{N};

    auto ex = aevox::make_executor(int_test_config());
    auto lr = ex->listen(port, [&](std::uint64_t id) -> aevox::Task<void> {
        int idx = count.fetch_add(1, std::memory_order_relaxed);
        if (idx < N) ids[static_cast<std::size_t>(idx)] = id;
        all_handled.count_down();
        co_return;
    });
    REQUIRE(lr.has_value());

    std::jthread runner{[&ex] { (void)ex->run(); }};

    for (int i = 0; i < N; ++i) {
        tcp_connect(port);
    }

    bool reached = all_handled.try_wait_for(5s);
    REQUIRE(reached);
    ex->stop();

    // Verify strict monotonic increase (IDs may arrive out of order across threads,
    // but the set must be consecutive starting from some base value).
    std::ranges::sort(ids);
    for (int i = 1; i < N; ++i) {
        REQUIRE(ids[static_cast<std::size_t>(i)] == ids[static_cast<std::size_t>(i-1)] + 1);
    }
}

TEST_CASE("AEV-001: integration — 1000 sequential connections all dispatched without drops", "[net][integration]") {
    auto port = find_free_port();

    constexpr int N = 1000;
    std::atomic<int> handled{0};
    std::latch all_done{N};

    auto ex = aevox::make_executor({.thread_count = 4, .drain_timeout = 5s});
    auto lr = ex->listen(port, [&](std::uint64_t) -> aevox::Task<void> {
        handled.fetch_add(1, std::memory_order_relaxed);
        all_done.count_down();
        co_return;
    });
    REQUIRE(lr.has_value());

    std::jthread runner{[&ex] { (void)ex->run(); }};

    for (int i = 0; i < N; ++i) {
        tcp_connect(port);
    }

    bool reached = all_done.try_wait_for(30s);
    REQUIRE(reached);
    REQUIRE(handled.load() == N);

    ex->stop();
}

TEST_CASE("AEV-001: integration — stop() drains in-flight handlers before run() returns", "[net][integration]") {
    auto port = find_free_port();

    // Handler that takes a little time to finish (simulating I/O work).
    std::atomic<int> completed{0};
    std::latch handler_started{1};

    auto ex = aevox::make_executor(int_test_config());
    auto lr = ex->listen(port, [&](std::uint64_t) -> aevox::Task<void> {
        handler_started.count_down();
        // Yield control briefly to simulate async work.
        // In real code this would be co_await some_io_operation().
        std::this_thread::sleep_for(50ms);
        completed.fetch_add(1, std::memory_order_relaxed);
        co_return;
    });
    REQUIRE(lr.has_value());

    std::jthread runner{[&ex] { (void)ex->run(); }};

    // Trigger one handler.
    tcp_connect(port);
    handler_started.wait();

    // Stop while handler is still running.
    ex->stop();
    // run() returns only after drain completes — runner thread joins here.
    runner.join(); // jthread destructor calls join()

    // After run() has returned, the in-flight handler must have completed.
    REQUIRE(completed.load() == 1);
}
