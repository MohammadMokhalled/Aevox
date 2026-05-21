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
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

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
class LogEntry
{
public:
    static constexpr std::size_t kInlineCapacity = 256;

    void set_level(LogLevel level) noexcept
    {
        level_ = level;
    }

    [[nodiscard]] LogLevel level() const noexcept
    {
        return level_;
    }

    void set_request_id(std::string request_id)
    {
        request_id_ = std::move(request_id);
    }

    [[nodiscard]] std::string_view request_id() const noexcept
    {
        return request_id_;
    }

    void set_trace_id(std::string trace_id)
    {
        trace_id_ = std::move(trace_id);
    }

    [[nodiscard]] std::string_view trace_id() const noexcept
    {
        return trace_id_;
    }

    void set_span_id(std::string span_id)
    {
        span_id_ = std::move(span_id);
    }

    [[nodiscard]] std::string_view span_id() const noexcept
    {
        return span_id_;
    }

    void set_thread_id(std::size_t thread_id) noexcept
    {
        thread_id_ = thread_id;
    }

    [[nodiscard]] std::size_t thread_id() const noexcept
    {
        return thread_id_;
    }

    void set_timestamp(std::chrono::system_clock::time_point timestamp) noexcept
    {
        timestamp_ = timestamp;
    }

    [[nodiscard]] std::chrono::system_clock::time_point timestamp() const noexcept
    {
        return timestamp_;
    }

    /**
     * @brief Copies `msg` into inline storage or overflow string.
     *
     * No heap allocation when msg.size() <= kInlineCapacity.
     */
    void set_message(std::string_view msg) noexcept
    {
        if (msg.size() > kInlineCapacity) {
            overflow_message_ = std::string(msg);
            message_len_      = 0;
        }
        else {
            message_len_ = static_cast<std::uint16_t>(msg.size());
            std::ranges::copy(msg, inline_message_.begin());
            overflow_message_.clear();
        }
    }

    /**
     * @brief Returns the message content.
     */
    [[nodiscard]] std::string_view message() const noexcept
    {
        if (!overflow_message_.empty()) {
            return overflow_message_;
        }
        return {inline_message_.data(), message_len_};
    }

private:
    LogLevel                          level_{LogLevel::Info};
    std::array<char, kInlineCapacity> inline_message_{};
    std::uint16_t                     message_len_{0};
    std::string overflow_message_;                      // Only used when message > kInlineCapacity.
    std::string request_id_;                            // Empty for global logs.
    std::string trace_id_;                              // Empty unless traceparent was valid.
    std::string span_id_;                               // Empty unless traceparent was valid.
    std::size_t thread_id_{0};                          // std::hash<std::thread::id>{}(id).
    std::chrono::system_clock::time_point timestamp_{}; // UTC wall-clock time.
};

} // namespace aevox
