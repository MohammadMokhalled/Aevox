// spdlog-backend.cpp: verify JSON output format
// ADD ref: Tasks/architecture/AEV-011-arch.md § Test Architecture

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "log/log_entry.hpp"
#include "log/spdlog_backend.hpp"

namespace {

[[nodiscard]] std::filesystem::path make_temp_log_path()
{
    static std::size_t counter = 0;
    return std::filesystem::temp_directory_path() /
           std::format("aevox-test-spdlog-backend-{}-{}.log", counter++,
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

TEST_CASE("spdlog backend writes correct JSON fields", "[log]")
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.sinks = {aevox::FileSinkConfig{
        .path = path.string(), .rotate_mb = 1, .keep_files = 1, .format = aevox::LogFormat::JSON}};

    {
        aevox::SpdlogBackend backend(config);

        aevox::LogEntry entry;
        entry.level = aevox::LogLevel::Info;
        entry.set_message("hello world");
        entry.request_id = "req-42";
        entry.thread_id  = 7;
        entry.timestamp  = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>{
                std::chrono::nanoseconds{1'234'567'890}});

        backend.write(entry);
        backend.flush();
    }

    std::ifstream file(path);
    REQUIRE(file.is_open());

    std::string line;
    std::getline(file, line);
    file.close();
    std::filesystem::remove(path);

    REQUIRE(line.find("\"timestamp\":") != std::string::npos);
    REQUIRE(line.find("1970-01-01T00:00:01.234567890Z") != std::string::npos);
    REQUIRE(line.find("\"level\":\"INFO\"") != std::string::npos);
    REQUIRE(line.find("\"message\":\"hello world\"") != std::string::npos);
    REQUIRE(line.find("\"request_id\":\"req-42\"") != std::string::npos);
    REQUIRE(line.find("\"thread_id\":7") != std::string::npos);
}
