// Integration tests for the TCP accept loop.
// ADD ref: Tasks/architecture/AEV-001-arch.md § Test Architecture (Rev.2)
//
// Setup: make_executor({.thread_count=2, .drain_timeout=2s}), listen on port 0
// (OS-assigned), run() on a background jthread. Tests connect via loopback.
//
// No Asio types are used to test framework behaviour — Asio is used on the
// CLIENT side only (to make TCP connections), not to test the server internals.

#include <aevox/executor.hpp>
#include <aevox/task.hpp>

#include <asio.hpp> // client-side connection helper only

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

namespace {

aevox::ExecutorConfig int_test_config()
{
    return {.thread_count = 2, .drain_timeout = 2s};
}

// Query the OS-assigned port from the acceptor.
// We cannot ask aevox::Executor for this (not in the public API yet), so we
// pass the port via an out-parameter set inside the handler on first connection.
// A simpler approach: use a fixed known-free port (risky on CI).
// Cleanest approach: bind a temporary acceptor to port 0, record the port, close it,
// then bind the test executor to that port (possible TOCTOU, but acceptable in tests).
std::uint16_t find_free_port()
{
    asio::io_context              ioc;
    asio::ip::tcp::acceptor const a{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return a.local_endpoint().port();
}

// Open a TCP connection to loopback:port and close it immediately.
void tcp_connect(std::uint16_t port)
{
    asio::io_context      ioc;
    asio::ip::tcp::socket s{ioc};
    asio::error_code      ec;
    auto const            ep = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port};
    ec                       = s.connect(ep, ec);
    // Ignore ec — the server may have closed before we read; that's fine.
}

} // namespace

// ---------------------------------------------------------------------------

TEST_CASE("integration - single client connect triggers handler coroutine", "[net][integration]")
{
    auto port = find_free_port();

    std::atomic<int>        handler_count{0};
    std::mutex              handler_mtx;
    std::condition_variable handler_cv;

    auto ex = aevox::make_executor(int_test_config());
    auto lr = ex->listen(port, [&](std::uint64_t, aevox::TcpStream) -> aevox::Task<void> {
        {
            std::scoped_lock const lk(handler_mtx);
            handler_count.store(1, std::memory_order_relaxed);
            handler_cv.notify_one();
        }
        co_return;
    });
    REQUIRE(lr.has_value());

    // Run the executor in background.
    std::jthread const runner{[&ex] { [[maybe_unused]] auto run_result = ex->run(); }};

    // Connect from the test thread.
    tcp_connect(port);

    // Wait for the handler to fire.
    {
        std::unique_lock lk(handler_mtx);
        bool const       reached = handler_cv.wait_for(lk, 2s, [&] {
            return handler_count.load(std::memory_order_relaxed) == 1;
        });
        REQUIRE(reached);
    }

    ex->stop();
}

TEST_CASE("integration - handler receives monotonically increasing conn_id", "[net][integration]")
{
    auto port = find_free_port();

    constexpr int              kN = 5;
    std::atomic<int>           count{0};
    std::vector<std::uint64_t> ids(kN);
    std::atomic<int>           handled{0};
    std::mutex                 handled_mtx;
    std::condition_variable    handled_cv;

    auto ex = aevox::make_executor(int_test_config());
    auto lr = ex->listen(port, [&](std::uint64_t id, aevox::TcpStream) -> aevox::Task<void> {
        int const idx = count.fetch_add(1, std::memory_order_relaxed);
        if (idx < kN)
            ids[static_cast<std::size_t>(idx)] = id;
        {
            std::scoped_lock const lk(handled_mtx);
            handled.store(idx + 1, std::memory_order_relaxed);
            handled_cv.notify_one();
        }
        co_return;
    });
    REQUIRE(lr.has_value());

    std::jthread const runner{[&ex] { [[maybe_unused]] auto run_result = ex->run(); }};

    for (int i = 0; i < kN; ++i) {
        tcp_connect(port);
    }

    {
        std::unique_lock lk(handled_mtx);
        bool const       reached = handled_cv.wait_for(lk, 5s, [&] {
            return handled.load(std::memory_order_relaxed) == kN;
        });
        REQUIRE(reached);
    }
    ex->stop();

    // Verify strict monotonic increase (IDs may arrive out of order across threads,
    // but the set must be consecutive starting from some base value).
    std::ranges::sort(ids);
    for (int i = 1; i < kN; ++i) {
        REQUIRE(ids[static_cast<std::size_t>(i)] == ids[static_cast<std::size_t>(i - 1)] + 1);
    }
}

TEST_CASE("integration - 1000 sequential connections all dispatched without drops",
          "[net][integration]")
{
    auto port = find_free_port();

    constexpr int           kN = 1000;
    std::atomic<int>        handled{0};
    std::mutex              done_mtx;
    std::condition_variable done_cv;

    auto ex = aevox::make_executor({.thread_count = 4, .drain_timeout = 5s});
    auto lr = ex->listen(port, [&](std::uint64_t, aevox::TcpStream) -> aevox::Task<void> {
        handled.fetch_add(1, std::memory_order_relaxed);
        {
            std::scoped_lock const lk(done_mtx);
            done_cv.notify_one();
        }
        co_return;
    });
    REQUIRE(lr.has_value());

    std::jthread const runner{[&ex] { [[maybe_unused]] auto run_result = ex->run(); }};

    for (int i = 0; i < kN; ++i) {
        tcp_connect(port);
    }

    {
        std::unique_lock lk(done_mtx);
        bool const       reached = done_cv.wait_for(lk, 30s, [&] {
            return handled.load(std::memory_order_relaxed) == kN;
        });
        REQUIRE(reached);
    }
    REQUIRE(handled.load() == kN);

    ex->stop();
}

TEST_CASE("integration - stop() drains in-flight handlers before run() returns",
          "[net][integration]")
{
    auto port = find_free_port();

    // Handler that takes a little time to finish (simulating I/O work).
    std::atomic<int> completed{0};
    // Use atomic<bool> instead of latch{1}: if a spurious second connection
    // arrives (e.g. a CI environment probe under ASan's wider timing window),
    // calling count_down() on an already-zero latch is UB. atomic::store(true)
    // is idempotent and safe regardless of how many handlers fire.
    std::atomic<bool> handler_started{false};

    auto ex = aevox::make_executor(int_test_config());
    auto lr = ex->listen(port, [&](std::uint64_t, aevox::TcpStream) -> aevox::Task<void> {
        handler_started.store(true, std::memory_order_release);
        handler_started.notify_all();
        // Yield control briefly to simulate async work.
        // In real code this would be co_await some_io_operation().
        std::this_thread::sleep_for(50ms);
        completed.fetch_add(1, std::memory_order_relaxed);
        co_return;
    });
    REQUIRE(lr.has_value());

    std::jthread runner{[&ex] { [[maybe_unused]] auto run_result = ex->run(); }};

    // Trigger one handler.
    tcp_connect(port);
    handler_started.wait(false, std::memory_order_acquire);

    // Stop while handler is still running.
    ex->stop();
    // run() returns only after drain completes — runner thread joins here.
    runner.join(); // jthread destructor calls join()

    // After run() has returned, all in-flight handlers must have completed.
    // Assert >=1 (not ==1): a spurious second connection may have been accepted
    // (CI probe, wider ASan timing window). The drain invariant — every handler
    // that started before stop() completes before run() returns — holds for any
    // number of in-flight handlers.
    REQUIRE(completed.load() >= 1);
}
