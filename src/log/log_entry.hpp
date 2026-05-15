#pragma once
// src/log/log_entry.hpp
//
// INTERNAL — serializable payload pushed to the ring buffer.
//
// Design: Tasks/architecture/AEV-011-arch.md §4.1

#include <aevox/log.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

namespace aevox {

/**
 * @brief Internal representation of a single log entry.
 *
 * Pre-formatted by the caller on the hot path. The background drain thread
 * passes this to the active LogBackend for sink dispatch.
 *
 * @note Message storage: small messages (≤ 256 bytes) are stored inline in
 *       `inline_message` to avoid heap allocation on the hot path. Larger
 *       messages fall back to `overflow_message`.
 */
struct LogEntry
{
    static constexpr std::size_t kInlineCapacity = 256;

    LogLevel                          level{LogLevel::Info};
    std::array<char, kInlineCapacity> inline_message{};
    std::uint16_t                     message_len{0};
    std::string                       overflow_message; // Only used when message > kInlineCapacity.
    std::string                       request_id;       // Empty for global logs.
    std::string                       correlation_id;   // Empty unless tracing is active.
    std::size_t                       thread_id{0};     // std::hash<std::thread::id>{}(id).
    std::chrono::system_clock::time_point timestamp{};  // UTC wall-clock time.

    /**
     * @brief Copies `msg` into inline storage or overflow string.
     *
     * No heap allocation when msg.size() <= kInlineCapacity.
     */
    void set_message(std::string_view msg) noexcept
    {
        if (msg.size() > kInlineCapacity) {
            overflow_message = std::string(msg);
            message_len      = 0;
        }
        else {
            message_len = static_cast<std::uint16_t>(msg.size());
            std::ranges::copy_n(msg.begin(), message_len, inline_message.begin());
            overflow_message.clear();
        }
    }

    /**
     * @brief Returns the message content.
     */
    [[nodiscard]] std::string_view message() const noexcept
    {
        if (!overflow_message.empty()) {
            return overflow_message;
        }
        return {inline_message.data(), message_len};
    }
};

} // namespace aevox
