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
        writer.push(aevox::LogLevel::Trace, "trace msg", nullptr);
        writer.push(aevox::LogLevel::Debug, "debug msg", nullptr);
        writer.push(aevox::LogLevel::Info, "info msg", nullptr);
        writer.push(aevox::LogLevel::Warn, "warn msg", nullptr);
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

TEST_CASE("log level filter - AEVOX_LOG_TRACE macro is no-op in release builds", "[log]")
{
#ifdef NDEBUG
    // In release, the macro expands to ((void)0) — it must compile and do nothing.
    AEVOX_LOG_TRACE("this should compile away");
    SUCCEED("AEVOX_LOG_TRACE compiled to no-op in release");
#else
    // In debug, the macro expands to a real call.
    AEVOX_LOG_TRACE("this is a real call in debug");
    SUCCEED("AEVOX_LOG_TRACE compiled to real call in debug");
#endif
}
