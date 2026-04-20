// Executor async-helper dispatch latency benchmarks.
// ADD ref: Tasks/architecture/AEV-006-arch.md § Test Architecture §8.3
//
// Measures:
//   1. Task dispatch round-trip: co_spawn → Task<void> completes.
//   2. pool() dispatch round-trip: co_await pool(noop) returns.
//
// Targets (PRD §9.7):
//   Task dispatch ≤ 10µs
//   pool() dispatch ≤ 20µs

#define ANKERL_NANOBENCH_IMPLEMENT
#include <aevox/async.hpp>
#include <aevox/executor.hpp>
#include <aevox/task.hpp>

#include <asio.hpp>

#include <nanobench.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <latch>
#include <thread>

using namespace std::chrono_literals;

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

int main()
{
    // -------------------------------------------------------------------------
    // Benchmark 1: Task dispatch latency
    // Measure the round-trip time for a minimal connection handler.
    // -------------------------------------------------------------------------
    {
        constexpr int iters_per_epoch = 200;

        auto             port    = find_free_port();
        std::atomic<int> handled = 0;

        auto ex =
            aevox::make_executor({.thread_count = 2, .cpu_pool_threads = 2, .drain_timeout = 5s});
        auto lr = ex->listen(port, [&](std::uint64_t, aevox::TcpStream) -> aevox::Task<void> {
            handled.fetch_add(1, std::memory_order_relaxed);
            co_return;
        });
        if (!lr.has_value()) {
            std::cerr << "listen() failed\n";
            return 1;
        }

        std::thread runner{[&ex] { (void)ex->run(); }};

        ankerl::nanobench::Bench()
            .minEpochIterations(iters_per_epoch)
            .warmup(50)
            .title("Task dispatch latency (accept - handler returns)")
            .run("round-trip", [&] {
                int before = handled.load(std::memory_order_relaxed);
                tcp_connect(port);
                // Spin until the handler increments.
                while (handled.load(std::memory_order_relaxed) == before) {
                    std::this_thread::yield();
                }
            });

        ex->stop();
        runner.join();

        std::cout << "Task dispatch: " << handled.load() << " handlers processed\n";
    }

    // -------------------------------------------------------------------------
    // Benchmark 2: pool() dispatch round-trip
    // Measure the overhead of suspending on pool() with a no-op callable.
    // -------------------------------------------------------------------------
    {
        constexpr int iters_per_epoch = 200;

        auto             port    = find_free_port();
        std::atomic<int> handled = 0;

        auto ex =
            aevox::make_executor({.thread_count = 2, .cpu_pool_threads = 2, .drain_timeout = 5s});
        auto lr = ex->listen(port, [&](std::uint64_t, aevox::TcpStream) -> aevox::Task<void> {
            co_await aevox::pool([] { /* no-op */ });
            handled.fetch_add(1, std::memory_order_relaxed);
            co_return;
        });
        if (!lr.has_value()) {
            std::cerr << "listen() failed (bench 2)\n";
            return 1;
        }

        std::thread runner{[&ex] { (void)ex->run(); }};

        ankerl::nanobench::Bench()
            .minEpochIterations(iters_per_epoch)
            .warmup(50)
            .title("pool() dispatch round-trip (accept - pool(noop) - handler returns)")
            .run("pool-round-trip", [&] {
                int before = handled.load(std::memory_order_relaxed);
                tcp_connect(port);
                while (handled.load(std::memory_order_relaxed) == before) {
                    std::this_thread::yield();
                }
            });

        ex->stop();
        runner.join();

        std::cout << "pool() dispatch: " << handled.load() << " handlers processed\n";
    }

    return 0;
}
