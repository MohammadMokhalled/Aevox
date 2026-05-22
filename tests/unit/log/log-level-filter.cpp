// log-level-filter.cpp: verify runtime level filtering in AsyncLogWriter
// ADD ref: Tasks/architecture/AEV-011-arch.md § Test Architecture

#include <aevox/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "log/async_writer.hpp"
#include "log/spdlog_backend.hpp"

namespace {

[[nodiscard]] std::filesystem::path make_temp_log_path()
{
    static std::size_t counter = 0;
    return std::filesystem::temp_directory_path() /
           std::format("aevox-test-log-level-{}-{}.log", counter++,
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

TEST_CASE("log level filter - trace and debug are dropped when level is info", "[log]")
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.level = aevox::LogLevel::Info;
    config.sinks = {aevox::FileSinkConfig{.path = path.string(), .format = aevox::LogFormat::JSON}};

    {
        aevox::AsyncLogWriter writer(config);
        writer.push(aevox::LogLevel::Trace, "trace msg");
        writer.push(aevox::LogLevel::Debug, "debug msg");
        writer.push(aevox::LogLevel::Info, "info msg");
        writer.push(aevox::LogLevel::Warn, "warn msg");
    } // destructor joins drain thread and flushes all entries

    std::ifstream file(path);
    REQUIRE(file.is_open());

    const std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    file.close();
    std::filesystem::remove(path);

    REQUIRE(content.find("trace msg") == std::string::npos);
    REQUIRE(content.find("debug msg") == std::string::npos);
    REQUIRE(content.find("info msg") != std::string::npos);
    REQUIRE(content.find("warn msg") != std::string::npos);
}

TEST_CASE("log level filter - global trace and debug functions compile", "[log]")
{
    aevox::log::trace("trace {}", "message");
    aevox::log::debug("debug {}", "message");
    SUCCEED("global trace and debug functions compiled");
}
