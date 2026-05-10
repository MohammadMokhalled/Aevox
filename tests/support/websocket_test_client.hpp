#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace aevox::test {

/**
 * @brief Deadline-bounded WebSocket client for integration tests.
 *
 * Owns one loopback WebSocket connection and exposes only standard C++ test-facing
 * operations. The backend transport is hidden behind the private implementation.
 *
 * @note Thread-safety: not thread-safe; use from one test thread at a time.
 * @note Move semantics: move-only. A moved-from client is valid only for destruction
 *       or assignment.
 * @note Ownership: owns the underlying connection until `close()` or destruction.
 */
class WebSocketTestClient
{
public:
    WebSocketTestClient(WebSocketTestClient&&) noexcept;
    WebSocketTestClient& operator=(WebSocketTestClient&&) noexcept;
    WebSocketTestClient(const WebSocketTestClient&)            = delete;
    WebSocketTestClient& operator=(const WebSocketTestClient&) = delete;
    ~WebSocketTestClient();

    [[nodiscard]] static std::expected<WebSocketTestClient, std::string> connect(
        std::uint16_t port, std::string_view path, std::chrono::steady_clock::duration timeout);

    [[nodiscard]] static std::expected<std::string, std::string> request(
        std::uint16_t port, std::string_view request_text,
        std::chrono::steady_clock::duration timeout);

    [[nodiscard]] std::expected<void, std::string> send_text(
        std::string_view payload, std::chrono::steady_clock::duration timeout);

    [[nodiscard]] std::expected<void, std::string> send_text_in_parts(
        std::string_view payload, std::size_t split_offset,
        std::chrono::steady_clock::duration timeout);

    [[nodiscard]] std::expected<void, std::string> send_text_first_part(
        std::string_view payload, std::size_t split_offset,
        std::chrono::steady_clock::duration timeout);

    [[nodiscard]] std::expected<void, std::string> send_text_second_part(
        std::string_view payload, std::size_t split_offset,
        std::chrono::steady_clock::duration timeout);

    [[nodiscard]] std::expected<std::string, std::string> read_text(
        std::chrono::steady_clock::duration timeout);

    [[nodiscard]] std::expected<void, std::string> send_close(
        std::uint16_t code, std::chrono::steady_clock::duration timeout);

    [[nodiscard]] std::expected<std::uint16_t, std::string> read_close(
        std::chrono::steady_clock::duration timeout);

    [[nodiscard]] std::expected<bool, std::string> has_readable_data(
        std::chrono::steady_clock::duration timeout);

    void close() noexcept;

private:
    struct Impl;

    explicit WebSocketTestClient(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::uint16_t free_loopback_port();

} // namespace aevox::test
