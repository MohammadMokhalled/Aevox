// logger-file-output.cpp: verify AEV-029 file destination behavior
// ADD ref: Tasks/architecture/AEV-029-arch.md §8

#include <aevox/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>

#include "log/log_writer.hpp"

namespace {

[[nodiscard]] std::filesystem::path make_temp_log_path()
{
    static int counter{0};
    return std::filesystem::temp_directory_path() /
           std::format("aevox-aev029-file-{}-{}.log", counter++,
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path)
{
    std::ifstream file{path};
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("AEV-029: file logger opens writes flushes and closes", "[integration][log]")
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.destination = aevox::LogDestination::File;
    config.file_path   = path.string();
    config.format      = aevox::LogFormat::Json;

    auto writer = aevox::detail::LogWriter::create(config);
    REQUIRE(writer.has_value());
    aevox::detail::install_log_writer(*writer);

    aevox::log::info("file output works");
    REQUIRE(aevox::log::flush().has_value());
    aevox::detail::reset_log_writer();
    writer->reset();

    const auto content = read_file(path);
    std::filesystem::remove(path);
    REQUIRE(content.find("\"message\":\"file output works\"") != std::string::npos);
}

TEST_CASE("AEV-029: missing file path fails startup with LogError", "[integration][log]")
{
    aevox::LogConfig config;
    config.destination = aevox::LogDestination::File;
    config.file_path   = std::nullopt;

    auto writer = aevox::detail::LogWriter::create(config);

    REQUIRE_FALSE(writer.has_value());
    REQUIRE(writer.error() == aevox::LogError::FilePathRequired);
    REQUIRE(aevox::to_string(writer.error()) == "file path required for file log destination");
}
