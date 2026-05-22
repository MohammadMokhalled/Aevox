// logger-hot-path.cpp: benchmark AEV-029 enqueue and saturated drop paths
// ADD ref: Tasks/architecture/AEV-029-arch.md §8

#include <aevox/log.hpp>

#include <nanobench.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "http/http_parser.hpp"
#include "http/request_impl.hpp"
#include "log/log_record.hpp"
#include "log/log_writer.hpp"

namespace {

[[nodiscard]] aevox::Request make_request()
{
    const std::string raw = "GET /bench HTTP/1.1\r\nHost: localhost\r\n\r\n";

    std::vector<std::byte> buffer;
    buffer.reserve(raw.size());
    for (const char ch : raw) {
        buffer.push_back(static_cast<std::byte>(ch));
    }

    aevox::detail::ParsedRequest parsed;
    parsed.method     = "GET";
    parsed.target     = "/bench";
    parsed.keep_alive = false;

    auto request = aevox::make_request_from_impl(std::move(buffer), std::move(parsed));
    auto impl    = aevox::get_mutable_request_impl(request);
    if (impl) {
        impl->get().set_request_id("0000000000000029");
    }
    return request;
}

[[nodiscard]] std::filesystem::path make_temp_log_path()
{
    return std::filesystem::temp_directory_path() /
           std::format("aevox-aev029-bench-{}.log",
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

int main()
{
    aevox::LogConfig config;
    config.destination = aevox::LogDestination::Disabled;

    auto writer = aevox::detail::LogWriter::create(config);
    if (!writer) {
        return 1;
    }
    aevox::detail::install_log_writer(*writer);

    ankerl::nanobench::Bench bench;
    bench.title("AEV-029 logger hot path");
    bench.minEpochIterations(10000);

    bench.run("disabled global write", [] { aevox::log::write(aevox::LogLevel::Info, "message"); });

    const auto       request_log_path = make_temp_log_path();
    aevox::LogConfig request_config;
    request_config.destination    = aevox::LogDestination::File;
    request_config.file_path      = request_log_path.string();
    request_config.queue_capacity = 1000000;
    auto request_writer           = aevox::detail::LogWriter::create(request_config);
    if (!request_writer) {
        aevox::detail::reset_log_writer();
        return 1;
    }
    aevox::detail::install_log_writer(*request_writer);
    auto request = make_request();

    bench.run("request write enqueue",
              [&request] { aevox::log::write(request, aevox::LogLevel::Info, "message"); });
    (void)aevox::log::flush();
    aevox::detail::reset_log_writer();
    request_writer->reset();
    std::filesystem::remove(request_log_path);

    aevox::LogConfig saturated_config;
    saturated_config.destination    = aevox::LogDestination::Stdout;
    saturated_config.queue_capacity = 1;
    aevox::detail::LogWriter saturated{saturated_config};
    saturated.enqueue(aevox::detail::LogRecord{.timestamp   = std::chrono::system_clock::now(),
                                               .level       = aevox::LogLevel::Info,
                                               .message     = "held",
                                               .request     = std::nullopt,
                                               .status_code = std::nullopt,
                                               .duration    = std::nullopt});

    bench.run("saturated queue drop", [&saturated] {
        saturated.enqueue(aevox::detail::LogRecord{.timestamp   = std::chrono::system_clock::now(),
                                                   .level       = aevox::LogLevel::Info,
                                                   .message     = "drop",
                                                   .request     = std::nullopt,
                                                   .status_code = std::nullopt,
                                                   .duration    = std::nullopt});
    });

    return 0;
}
