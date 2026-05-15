// ring-buffer-basics.cpp: single-producer single-consumer FIFO test
// ADD ref: Tasks/architecture/AEV-011-arch.md § Test Architecture

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "log/log_entry.hpp"
#include "log/ring_buffer.hpp"

TEST_CASE("ring buffer basics - push and pop preserves order", "[log]")
{
    aevox::LockFreeQueue queue(16);

    constexpr std::size_t kCount = 10;
    for (std::size_t i = 0; i < kCount; ++i) {
        aevox::LogEntry entry;
        entry.level = aevox::LogLevel::Info;
        entry.set_message(std::to_string(i));
        REQUIRE(queue.try_push(entry));
    }

    for (std::size_t i = 0; i < kCount; ++i) {
        aevox::LogEntry out;
        REQUIRE(queue.try_pop(out));
        REQUIRE(out.message() == std::to_string(i));
    }

    // Queue is now empty.
    aevox::LogEntry empty;
    REQUIRE_FALSE(queue.try_pop(empty));
}
