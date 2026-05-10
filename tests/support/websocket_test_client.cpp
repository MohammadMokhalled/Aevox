#include "websocket_test_client.hpp"

#include <asio.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace aevox::test {
namespace {

constexpr std::string_view kDefaultWebSocketKey{"dGhlIHNhbXBsZSBub25jZQ=="};
constexpr std::size_t      kMaxHttpHeaderBytes{static_cast<std::size_t>(16U) * 1024U};

std::string error_message(std::string_view operation, const asio::error_code& ec)
{
    return std::format("{} failed: {}", operation, ec.message());
}

std::vector<std::byte> make_masked_text_frame(std::string_view payload)
{
    const std::size_t      payload_len = payload.size();
    std::vector<std::byte> frame;

    frame.push_back(std::byte{0x81});

    if (payload_len < 126) {
        frame.push_back(static_cast<std::byte>(0x80U | payload_len));
    }
    else {
        frame.push_back(std::byte{0xFE});
        frame.push_back(static_cast<std::byte>(payload_len >> 8U));
        frame.push_back(static_cast<std::byte>(payload_len & 0xFFU));
    }

    frame.push_back(std::byte{0x00});
    frame.push_back(std::byte{0x00});
    frame.push_back(std::byte{0x00});
    frame.push_back(std::byte{0x00});

    for (const char c : payload) {
        frame.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }

    return frame;
}

std::vector<std::byte> make_close_frame(std::uint16_t code)
{
    return std::vector<std::byte>{
        std::byte{0x88},
        std::byte{0x82},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        static_cast<std::byte>(code >> 8U),
        static_cast<std::byte>(code & 0xFFU),
    };
}

} // anonymous namespace

struct WebSocketTestClient::Impl
{
    asio::io_context      io;
    asio::ip::tcp::socket socket{io};
};

WebSocketTestClient::WebSocketTestClient(std::unique_ptr<Impl> impl) noexcept
    : impl_{std::move(impl)}
{}

WebSocketTestClient::WebSocketTestClient(WebSocketTestClient&&) noexcept = default;

WebSocketTestClient& WebSocketTestClient::operator=(WebSocketTestClient&&) noexcept = default;

WebSocketTestClient::~WebSocketTestClient() = default;

namespace {

std::expected<void, std::string> write_exact(asio::ip::tcp::socket& socket, asio::io_context& io,
                                             std::span<const std::byte>          data,
                                             std::chrono::steady_clock::duration timeout)
{
    asio::error_code ec;
    bool             completed = false;
    bool             timed_out = false;

    asio::steady_timer timer{io};
    timer.expires_after(timeout);

    asio::async_write(socket, asio::buffer(data.data(), data.size()),
                      [&](const asio::error_code& write_ec, std::size_t) {
                          ec        = write_ec;
                          completed = true;
                          timer.cancel();
                      });

    timer.async_wait([&](const asio::error_code& timer_ec) {
        if (!timer_ec && !completed) {
            timed_out = true;
            socket.cancel();
        }
    });

    io.restart();
    io.run();

    if (timed_out) {
        return std::unexpected{"write timed out"};
    }
    if (!completed) {
        return std::unexpected{"write did not complete"};
    }
    if (ec) {
        return std::unexpected{error_message("write", ec)};
    }
    return {};
}

std::expected<void, std::string> read_exact(asio::ip::tcp::socket& socket, asio::io_context& io,
                                            std::span<std::uint8_t>             out,
                                            std::chrono::steady_clock::duration timeout)
{
    asio::error_code ec;
    bool             completed = false;
    bool             timed_out = false;

    asio::steady_timer timer{io};
    timer.expires_after(timeout);

    asio::async_read(socket, asio::buffer(out.data(), out.size()),
                     asio::transfer_exactly(out.size()),
                     [&](const asio::error_code& read_ec, std::size_t) {
                         ec        = read_ec;
                         completed = true;
                         timer.cancel();
                     });

    timer.async_wait([&](const asio::error_code& timer_ec) {
        if (!timer_ec && !completed) {
            timed_out = true;
            socket.cancel();
        }
    });

    io.restart();
    io.run();

    if (timed_out) {
        return std::unexpected{"read timed out"};
    }
    if (!completed) {
        return std::unexpected{"read did not complete"};
    }
    if (ec) {
        return std::unexpected{error_message("read", ec)};
    }
    return {};
}

std::expected<void, std::string> connect_socket(asio::ip::tcp::socket& socket, asio::io_context& io,
                                                std::uint16_t                       port,
                                                std::chrono::steady_clock::duration timeout)
{
    asio::error_code ec;
    bool             connected = false;
    bool             timed_out = false;

    asio::steady_timer deadline_timer{io};
    asio::steady_timer retry_timer{io};
    deadline_timer.expires_after(timeout);

    const asio::ip::tcp::endpoint endpoint{asio::ip::address_v4::loopback(), port};

    std::function<void()> attempt_connect;
    attempt_connect = [&] {
        if (connected || timed_out) {
            return;
        }

        if (socket.is_open()) {
            socket.close(ec);
        }
        socket.open(asio::ip::tcp::v4(), ec);
        if (ec) {
            timed_out = true;
            deadline_timer.cancel();
            return;
        }

        socket.async_connect(endpoint, [&](const asio::error_code& connect_ec) {
            ec = connect_ec;
            if (!connect_ec) {
                connected = true;
                deadline_timer.cancel();
                retry_timer.cancel();
                return;
            }

            if (timed_out) {
                return;
            }

            retry_timer.expires_after(std::chrono::milliseconds{1});
            retry_timer.async_wait([&](const asio::error_code& wait_ec) {
                if (!wait_ec) {
                    attempt_connect();
                }
            });
        });
    };

    deadline_timer.async_wait([&](const asio::error_code& timer_ec) {
        if (!timer_ec && !connected) {
            timed_out = true;
            socket.cancel();
            retry_timer.cancel();
        }
    });

    attempt_connect();

    io.restart();
    io.run();

    if (timed_out) {
        return std::unexpected{"connect timed out"};
    }
    if (!connected) {
        return std::unexpected{ec ? error_message("connect", ec) : std::string{"connect failed"}};
    }
    return {};
}

std::expected<std::string, std::string> read_http_headers(
    asio::ip::tcp::socket& socket, asio::io_context& io,
    std::chrono::steady_clock::duration timeout)
{
    std::string                 response;
    std::array<std::uint8_t, 1> byte{};

    while (response.find("\r\n\r\n") == std::string::npos) {
        if (response.size() >= kMaxHttpHeaderBytes) {
            return std::unexpected{"HTTP header response exceeded 16 KiB"};
        }

        auto read_result = read_exact(socket, io, std::span<std::uint8_t>{byte}, timeout);
        if (!read_result) {
            return std::unexpected{read_result.error()};
        }
        response += static_cast<char>(byte[0]);
    }

    return response;
}

std::expected<std::string, std::string> read_frame_payload(
    asio::ip::tcp::socket& socket, asio::io_context& io,
    std::chrono::steady_clock::duration timeout, std::optional<std::uint8_t> expected_opcode)
{
    std::array<std::uint8_t, 2> header{};
    auto header_result = read_exact(socket, io, std::span<std::uint8_t>{header}, timeout);
    if (!header_result) {
        return std::unexpected{header_result.error()};
    }

    const std::uint8_t opcode = header[0] & 0x0FU;
    if (expected_opcode && opcode != *expected_opcode) {
        return std::unexpected{std::format("unexpected opcode: {}", opcode)};
    }

    std::uint64_t payload_len = header[1] & 0x7FU;
    if (payload_len == 126) {
        std::array<std::uint8_t, 2> ext{};
        auto ext_result = read_exact(socket, io, std::span<std::uint8_t>{ext}, timeout);
        if (!ext_result) {
            return std::unexpected{ext_result.error()};
        }
        payload_len = (static_cast<std::uint64_t>(ext[0]) << 8U) | ext[1];
    }
    else if (payload_len == 127) {
        std::array<std::uint8_t, 8> ext{};
        auto ext_result = read_exact(socket, io, std::span<std::uint8_t>{ext}, timeout);
        if (!ext_result) {
            return std::unexpected{ext_result.error()};
        }
        payload_len = 0;
        for (const auto b : ext) {
            payload_len = (payload_len << 8U) | b;
        }
    }

    std::string payload(payload_len, '\0');
    if (payload_len == 0) {
        return payload;
    }

    const auto payload_span =
        std::span<std::uint8_t>{reinterpret_cast<std::uint8_t*>(payload.data()), payload.size()};
    auto payload_result = read_exact(socket, io, payload_span, timeout);
    if (!payload_result) {
        return std::unexpected{payload_result.error()};
    }
    return payload;
}

} // anonymous namespace

std::expected<WebSocketTestClient, std::string> WebSocketTestClient::connect(
    std::uint16_t port, std::string_view path, std::chrono::steady_clock::duration timeout)
{
    auto impl = std::make_unique<Impl>();

    auto connect_result = connect_socket(impl->socket, impl->io, port, timeout);
    if (!connect_result) {
        return std::unexpected{connect_result.error()};
    }

    const std::string request_text = std::format("GET {} HTTP/1.1\r\n"
                                                 "Host: localhost:{}\r\n"
                                                 "Upgrade: websocket\r\n"
                                                 "Connection: Upgrade\r\n"
                                                 "Sec-WebSocket-Key: {}\r\n"
                                                 "Sec-WebSocket-Version: 13\r\n"
                                                 "\r\n",
                                                 path, port, kDefaultWebSocketKey);
    auto              write_result =
        write_exact(impl->socket, impl->io,
                    std::as_bytes(std::span<const char>{request_text.data(), request_text.size()}),
                    timeout);
    if (!write_result) {
        return std::unexpected{write_result.error()};
    }

    auto response = read_http_headers(impl->socket, impl->io, timeout);
    if (!response) {
        return std::unexpected{response.error()};
    }
    if (response->find("HTTP/1.1 101") == std::string::npos) {
        return std::unexpected{std::format("upgrade failed: {}", *response)};
    }

    return WebSocketTestClient{std::move(impl)};
}

std::expected<std::string, std::string> WebSocketTestClient::request(
    std::uint16_t port, std::string_view request_text, std::chrono::steady_clock::duration timeout)
{
    auto impl = std::make_unique<Impl>();

    auto connect_result = connect_socket(impl->socket, impl->io, port, timeout);
    if (!connect_result) {
        return std::unexpected{connect_result.error()};
    }

    auto write_result =
        write_exact(impl->socket, impl->io,
                    std::as_bytes(std::span<const char>{request_text.data(), request_text.size()}),
                    timeout);
    if (!write_result) {
        return std::unexpected{write_result.error()};
    }

    return read_http_headers(impl->socket, impl->io, timeout);
}

std::expected<void, std::string> WebSocketTestClient::send_text(
    std::string_view payload, std::chrono::steady_clock::duration timeout)
{
    auto frame = make_masked_text_frame(payload);
    return write_exact(impl_->socket, impl_->io, std::span<const std::byte>{frame}, timeout);
}

std::expected<void, std::string> WebSocketTestClient::send_text_in_parts(
    std::string_view payload, std::size_t split_offset, std::chrono::steady_clock::duration timeout)
{
    auto first_part = send_text_first_part(payload, split_offset, timeout);
    if (!first_part) {
        return std::unexpected{first_part.error()};
    }

    return send_text_second_part(payload, split_offset, timeout);
}

std::expected<void, std::string> WebSocketTestClient::send_text_first_part(
    std::string_view payload, std::size_t split_offset, std::chrono::steady_clock::duration timeout)
{
    auto frame = make_masked_text_frame(payload);
    if (split_offset == 0 || split_offset >= frame.size()) {
        return std::unexpected{std::format("invalid split offset: {}", split_offset)};
    }

    return write_exact(impl_->socket, impl_->io,
                       std::span<const std::byte>{frame.data(), split_offset}, timeout);
}

std::expected<void, std::string> WebSocketTestClient::send_text_second_part(
    std::string_view payload, std::size_t split_offset, std::chrono::steady_clock::duration timeout)
{
    auto frame = make_masked_text_frame(payload);
    if (split_offset == 0 || split_offset >= frame.size()) {
        return std::unexpected{std::format("invalid split offset: {}", split_offset)};
    }

    return write_exact(impl_->socket, impl_->io,
                       std::span<const std::byte>{frame.data() + split_offset,
                                                  frame.size() - split_offset},
                       timeout);
}

std::expected<std::string, std::string> WebSocketTestClient::read_text(
    std::chrono::steady_clock::duration timeout)
{
    return read_frame_payload(impl_->socket, impl_->io, timeout, std::uint8_t{0x1});
}

std::expected<void, std::string> WebSocketTestClient::send_close(
    std::uint16_t code, std::chrono::steady_clock::duration timeout)
{
    auto frame = make_close_frame(code);
    return write_exact(impl_->socket, impl_->io, std::span<const std::byte>{frame}, timeout);
}

std::expected<std::uint16_t, std::string> WebSocketTestClient::read_close(
    std::chrono::steady_clock::duration timeout)
{
    auto payload = read_frame_payload(impl_->socket, impl_->io, timeout, std::uint8_t{0x8});
    if (!payload) {
        return std::unexpected{payload.error()};
    }
    if (payload->size() < 2) {
        return std::unexpected{"close frame missing status code"};
    }
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(static_cast<unsigned char>((*payload)[0])) << 8U) |
        static_cast<std::uint16_t>(static_cast<unsigned char>((*payload)[1])));
}

std::expected<bool, std::string> WebSocketTestClient::has_readable_data(
    std::chrono::steady_clock::duration timeout)
{
    asio::error_code ec;
    bool             completed = false;
    bool             readable  = false;

    asio::steady_timer deadline_timer{impl_->io};
    asio::steady_timer poll_timer{impl_->io};
    deadline_timer.expires_after(timeout);

    std::function<void()> poll;
    poll = [&] {
        if (completed) {
            return;
        }

        const auto available = impl_->socket.available(ec);
        if (ec) {
            completed = true;
            deadline_timer.cancel();
            poll_timer.cancel();
            return;
        }
        if (available > 0U) {
            readable  = true;
            completed = true;
            deadline_timer.cancel();
            poll_timer.cancel();
            return;
        }

        poll_timer.expires_after(std::chrono::milliseconds{1});
        poll_timer.async_wait([&](const asio::error_code& wait_ec) {
            if (!wait_ec) {
                poll();
            }
        });
    };

    deadline_timer.async_wait([&](const asio::error_code& timer_ec) {
        if (!timer_ec) {
            completed = true;
            poll_timer.cancel();
        }
    });

    asio::post(impl_->io, poll);

    impl_->io.restart();
    impl_->io.run();

    if (ec) {
        return std::unexpected{error_message("readable check", ec)};
    }
    return readable;
}

void WebSocketTestClient::close() noexcept
{
    if (!impl_) {
        return;
    }

    asio::error_code ec;
    impl_->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    impl_->socket.close(ec);
}

std::uint16_t free_loopback_port()
{
    asio::io_context              io;
    asio::ip::tcp::acceptor const acceptor{io, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return acceptor.local_endpoint().port();
}

} // namespace aevox::test
