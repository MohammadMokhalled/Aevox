// request-correlation.cpp: verify req.logger() entries carry context fields
// ADD ref: Tasks/architecture/AEV-011-arch.md § Test Architecture

#include <aevox/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

#include "http/request_impl.hpp"
#include "log/async_writer.hpp"
#include "log/log_entry.hpp"

namespace {

[[nodiscard]] std::filesystem::path make_temp_log_path()
{
    static std::size_t counter = 0;
    return std::filesystem::temp_directory_path() /
           std::format("aevox-test-req-corr-{}-{}.log", counter++,
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

TEST_CASE("request correlation - log entries carry request_id and thread_id", "[log]")
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.sinks = {aevox::FileSinkConfig{.path = path.string(), .format = aevox::LogFormat::JSON}};

    {
        aevox::AsyncLogWriter writer(config);

        aevox::RequestContext ctx;
        ctx.request_id = "req-test-007";
        ctx.thread_id  = 42;

        writer.push(aevox::LogLevel::Info, "correlated message", std::cref(ctx));
    } // destructor joins drain thread and flushes all entries

    std::ifstream file(path);
    REQUIRE(file.is_open());

    std::string line;
    std::getline(file, line);
    file.close();
    std::filesystem::remove(path);

    REQUIRE(line.find("\"request_id\":\"req-test-007\"") != std::string::npos);
    REQUIRE(line.find("\"thread_id\":42") != std::string::npos);
}
