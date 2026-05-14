// ring-buffer-overflow.cpp: bounded queue drops oldest when full
// ADD ref: Tasks/architecture/AEV-011-arch.md § Test Architecture

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "log/log_entry.hpp"
#include "log/ring_buffer.hpp"

TEST_CASE("ring buffer overflow - excess push returns false and increments drop count", "[log]")
{
    aevox::LockFreeQueue queue(4); // real capacity rounded to power of 2 = 4

    for (std::size_t i = 0; i < 4; ++i) {
        aevox::LogEntry entry;
        entry.level = aevox::LogLevel::Info;
        entry.set_message(std::to_string(i));
        REQUIRE( // NOLINT(bugprone-use-after-move) — loop creates a fresh instance
            queue.try_push(std::move(entry)));
    }

    // 5th push should fail.
    aevox::LogEntry overflow;
    overflow.level = aevox::LogLevel::Warn;
    overflow.set_message("overflow");
    // NOLINTNEXTLINE(bugprone-use-after-move) — variable is not reused after this point.
    REQUIRE_FALSE(queue.try_push(std::move(overflow)));
    REQUIRE(queue.dropped_count() == 1);

    // Existing entries remain readable.
    for (std::size_t i = 0; i < 4; ++i) {
        aevox::LogEntry out;
        REQUIRE(queue.try_pop(out));
        REQUIRE(out.message() == std::to_string(i));
    }
}
