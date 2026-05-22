// logger-filtering.cpp: verify AEV-029 level filtering and bounded queue behavior
// ADD ref: Tasks/architecture/AEV-029-arch.md §8

#include <aevox/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>

#include "log/log_record.hpp"
#include "log/log_writer.hpp"

TEST_CASE("AEV-029: disabled logger drops all entries", "[log]")
{
    aevox::LogConfig config;
    config.destination = aevox::LogDestination::Disabled;

    auto writer = aevox::detail::LogWriter::create(config);
    REQUIRE(writer.has_value());

    aevox::detail::install_log_writer(*writer);
    aevox::log::info("hidden");
    REQUIRE(aevox::log::flush().has_value());
    const auto stats = aevox::log::stats();
    aevox::detail::reset_log_writer();

    REQUIRE(stats.accepted == 0);
    REQUIRE(stats.dropped == 1);
    REQUIRE(stats.written == 0);
}

TEST_CASE("AEV-029: level filter drops entries below threshold", "[log]")
{
    aevox::LogConfig config;
    config.level       = aevox::LogLevel::Warn;
    config.destination = aevox::LogDestination::Disabled;

    auto writer = aevox::detail::LogWriter::create(config);
    REQUIRE(writer.has_value());

    aevox::detail::install_log_writer(*writer);
    aevox::log::info("too low");
    REQUIRE(aevox::log::flush().has_value());
    const auto stats = aevox::log::stats();
    aevox::detail::reset_log_writer();

    REQUIRE(stats.accepted == 0);
    REQUIRE(stats.dropped == 1);
}

TEST_CASE("AEV-029: bounded queue increments dropped count when full", "[log]")
{
    aevox::LogConfig config;
    config.destination    = aevox::LogDestination::Stdout;
    config.queue_capacity = 1;

    aevox::detail::LogWriter writer{config};
    writer.enqueue(aevox::detail::LogRecord{.timestamp   = std::chrono::system_clock::now(),
                                            .level       = aevox::LogLevel::Info,
                                            .message     = "first",
                                            .request     = std::nullopt,
                                            .status_code = std::nullopt,
                                            .duration    = std::nullopt});
    writer.enqueue(aevox::detail::LogRecord{.timestamp   = std::chrono::system_clock::now(),
                                            .level       = aevox::LogLevel::Info,
                                            .message     = "second",
                                            .request     = std::nullopt,
                                            .status_code = std::nullopt,
                                            .duration    = std::nullopt});

    const auto stats = writer.stats();
    REQUIRE(stats.accepted == 1);
    REQUIRE(stats.dropped == 1);
}

TEST_CASE("AEV-029: flush returns error for failed file destination", "[log]")
{
    aevox::LogConfig config;
    config.destination = aevox::LogDestination::File;
    config.file_path   = std::nullopt;

    auto writer = aevox::detail::LogWriter::create(config);

    REQUIRE_FALSE(writer.has_value());
    REQUIRE(writer.error() == aevox::LogError::FilePathRequired);
}
