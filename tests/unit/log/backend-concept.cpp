// backend-concept.cpp: compile-time concept checks
// ADD ref: Tasks/architecture/AEV-011-arch.md § Test Architecture

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "log/log_backend.hpp"
#include "log/log_entry.hpp"

// =============================================================================
// Minimal conforming backend
// =============================================================================

namespace {

struct MockBackend
{
    std::vector<std::string> messages;

    void write(const aevox::LogEntry& entry)
    {
        messages.emplace_back(entry.message());
    }

    void flush() {}
};

struct MissingWrite
{
    void flush() {}
};

struct MissingFlush
{
    void write(const aevox::LogEntry&) {}
};

} // namespace

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("LogBackend concept - MockBackend satisfies the concept", "[log]")
{
    static_assert(aevox::LogBackend<MockBackend>, "MockBackend must satisfy aevox::LogBackend");
    SUCCEED("MockBackend satisfies aevox::LogBackend");
}

TEST_CASE("LogBackend concept - type missing write does not satisfy", "[log]")
{
    static_assert(!aevox::LogBackend<MissingWrite>,
                  "MissingWrite must NOT satisfy aevox::LogBackend");
    SUCCEED("MissingWrite correctly does not satisfy aevox::LogBackend");
}

TEST_CASE("LogBackend concept - type missing flush does not satisfy", "[log]")
{
    static_assert(!aevox::LogBackend<MissingFlush>,
                  "MissingFlush must NOT satisfy aevox::LogBackend");
    SUCCEED("MissingFlush correctly does not satisfy aevox::LogBackend");
}
