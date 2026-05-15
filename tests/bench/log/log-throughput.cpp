// log-throughput.cpp: measure sustained logging throughput
// ADD ref: Tasks/architecture/AEV-011-arch.md § Test Architecture
//
// Target: drain rate >= 1M entries/sec, drop rate < 0.1%.

#include <aevox/log.hpp>

#include <nanobench.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <format>
#include <thread>
#include <vector>

#include "log/async_writer.hpp"

namespace {

std::filesystem::path make_temp_log_path()
{
    return std::filesystem::temp_directory_path() /
           std::format("aevox-bench-throughput-{}.log",
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

int main()
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.ring_buffer_entries = 65536;
    config.sinks = {aevox::FileSinkConfig{.path = path.string(), .format = aevox::LogFormat::JSON}};

    aevox::AsyncLogWriter writer(config);
    aevox::Logger         logger = writer.make_logger(nullptr);

    constexpr std::size_t kThreadCount = 8;
    constexpr auto        kDuration    = std::chrono::seconds(1);

    std::barrier             start_barrier(kThreadCount);
    std::atomic<bool>        stop_flag{false};
    std::atomic<std::size_t> pushed{0};

    auto worker = [&](std::size_t /*tid*/) {
        start_barrier.arrive_and_wait();
        std::size_t local_count = 0;
        while (!stop_flag.load(std::memory_order_relaxed)) {
            logger.info("benchmark entry {}", local_count);
            ++local_count;
        }
        pushed.fetch_add(local_count, std::memory_order_relaxed);
    };

    std::vector<std::jthread> threads;
    threads.reserve(kThreadCount);
    for (std::size_t i = 0; i < kThreadCount; ++i) {
        threads.emplace_back(worker, i);
    }

    std::this_thread::sleep_for(kDuration);
    stop_flag.store(true, std::memory_order_relaxed);

    for (auto& t : threads) {
        t.join();
    }

    writer.flush();
    std::filesystem::remove(path);

    const std::size_t        total = pushed.load(std::memory_order_relaxed);
    ankerl::nanobench::Bench bench;
    bench.title("log throughput").unit("entries/sec").minEpochIterations(1);

    bench.run("8-thread sustained push", [&] { ankerl::nanobench::doNotOptimizeAway(total); });
}
