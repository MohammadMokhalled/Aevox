// log-hot-path-latency.cpp: measure latency of a single log.info() call
// ADD ref: Tasks/architecture/AEV-011-arch.md § Test Architecture
//
// Target: median <= 100 ns per req.logger().info(...) call (ring buffer push only).

#include <aevox/log.hpp>

#include <nanobench.h>

#include <chrono>
#include <filesystem>
#include <format>

#include "log/async_writer.hpp"

namespace {

std::filesystem::path make_temp_log_path()
{
    return std::filesystem::temp_directory_path() /
           std::format("aevox-bench-hot-path-{}.log",
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

int main()
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.sinks = {aevox::FileSinkConfig{.path = path.string(), .format = aevox::LogFormat::JSON}};

    aevox::AsyncLogWriter writer(config);
    aevox::Logger         logger = writer.make_logger();

    ankerl::nanobench::Bench bench;
    bench.title("log hot path latency").unit("ns").relative(true);

    bench.run("logger.info(\"test\", 42)", [&] {
        logger.info("test {}", 42);
        ankerl::nanobench::doNotOptimizeAway(logger);
    });

    writer.flush();
    std::filesystem::remove(path);
}
