// AEV-001: accept_loop throughput baseline.
// ADD ref: Tasks/architecture/AEV-001-arch.md § Test Architecture (Rev.2)
//
// Measures: accepted connections per second on loopback.
// Target: >= 500,000 accepts/sec (baseline for PRD §9 throughput target).
// Config: 2-thread pool (bench machine may be constrained in CI).
//
// Methodology: each iteration connects + triggers handler entry (co_return
// immediately). Not a full round-trip — HTTP parsing is AEV-003.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <aevox/executor.hpp>
#include <aevox/task.hpp>

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <latch>
#include <thread>

using namespace std::chrono_literals;

static std::uint16_t find_free_port() {
    asio::io_context ioc;
    asio::ip::tcp::acceptor a{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return a.local_endpoint().port();
}

int main() {
    constexpr int CONNECTIONS_PER_EPOCH = 500;

    auto port = find_free_port();
    std::atomic<int> handled{0};

    auto ex = aevox::make_executor({.thread_count = 2, .drain_timeout = 5s});
    auto lr = ex->listen(port, [&handled](std::uint64_t) -> aevox::Task<void> {
        handled.fetch_add(1, std::memory_order_relaxed);
        co_return;
    });

    if (!lr.has_value()) {
        std::fprintf(stderr, "listen() failed: %s\n",
                     std::string{aevox::to_string(lr.error())}.c_str());
        return 1;
    }

    std::jthread runner{[&ex] { (void)ex->run(); }};

    // Warm up: drain any OS connection backlog.
    {
        asio::io_context ioc;
        for (int i = 0; i < 10; ++i) {
            asio::ip::tcp::socket s{ioc};
            asio::error_code ec;
            s.connect(asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port}, ec);
        }
        std::this_thread::sleep_for(10ms);
        handled.store(0);
    }

    ankerl::nanobench::Bench bench;
    bench.title("AEV-001: accept_loop loopback throughput")
         .unit("connection")
         .minEpochIterations(CONNECTIONS_PER_EPOCH)
         .warmup(3);

    bench.run("AEV-001: accept_loop loopback throughput", [&] {
        asio::io_context ioc;
        asio::ip::tcp::socket s{ioc};
        asio::error_code ec;
        s.connect(asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port}, ec);
        s.close(ec);
        ankerl::nanobench::doNotOptimizeAway(handled.load());
    });

    ex->stop();
    // runner joins on scope exit (jthread destructor).

    // Report whether we hit the target.
    // nanobench prints the result; we verify separately via CI thresholds.
    return 0;
}
