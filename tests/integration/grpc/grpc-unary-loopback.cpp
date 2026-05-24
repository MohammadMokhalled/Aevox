// gRPC unary loopback integration tests.
#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/plugins/grpc.hpp>
#include <aevox/response.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <nghttp2/nghttp2.h>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

[[nodiscard]] std::uint16_t free_port()
{
    asio::io_context              ioc;
    asio::ip::tcp::acceptor const acceptor{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return acceptor.local_endpoint().port();
}

[[nodiscard]] std::vector<std::byte> bytes(std::string_view text)
{
    auto span = std::as_bytes(std::span{text.data(), text.size()});
    return {span.begin(), span.end()};
}

[[nodiscard]] std::vector<std::byte> envelope(std::span<const std::byte> payload,
                                              bool                       compressed = false)
{
    std::vector<std::byte> out;
    out.reserve(payload.size() + 5U);
    out.push_back(compressed ? std::byte{1} : std::byte{0});
    const auto length = static_cast<std::uint32_t>(payload.size());
    out.push_back(static_cast<std::byte>((length >> 24U) & 0xFFU));
    out.push_back(static_cast<std::byte>((length >> 16U) & 0xFFU));
    out.push_back(static_cast<std::byte>((length >> 8U) & 0xFFU));
    out.push_back(static_cast<std::byte>(length & 0xFFU));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

struct GrpcReply
{
    std::vector<std::byte> data;
    std::string            status;
    std::string            message;
    bool                   status_in_response_headers{};
    bool                   status_in_trailers{};
};

struct ClientState
{
    asio::ip::tcp::socket* socket{};
    std::vector<std::byte> request_body;
    std::size_t            request_offset{};
    GrpcReply              reply;
    bool                   done{};
    std::int32_t           stream_id{};
};

nghttp2_nv nv(std::string_view name, std::string_view value)
{
    return nghttp2_nv{
        .name     = reinterpret_cast<std::uint8_t*>(const_cast<char*>(name.data())),
        .value    = reinterpret_cast<std::uint8_t*>(const_cast<char*>(value.data())),
        .namelen  = name.size(),
        .valuelen = value.size(),
        .flags    = NGHTTP2_NV_FLAG_NONE,
    };
}

ssize_t data_read(nghttp2_session*, std::int32_t, std::uint8_t* buf, std::size_t length,
                  std::uint32_t* flags, nghttp2_data_source* source, void*)
{
    auto& state     = *static_cast<ClientState*>(source->ptr);
    auto  remaining = state.request_body.size() - state.request_offset;
    auto  to_copy   = std::min(length, remaining);
    if (to_copy > 0U) {
        std::memcpy(buf, state.request_body.data() + state.request_offset, to_copy);
        state.request_offset += to_copy;
    }
    if (state.request_offset >= state.request_body.size()) {
        *flags |= NGHTTP2_DATA_FLAG_EOF;
    }
    return static_cast<ssize_t>(to_copy);
}

int on_header(nghttp2_session*, const nghttp2_frame* frame, const std::uint8_t* name,
              std::size_t namelen, const std::uint8_t* value, std::size_t valuelen, std::uint8_t,
              void* user_data)
{
    if (frame->hd.type != NGHTTP2_HEADERS) {
        return 0;
    }
    auto& state = *static_cast<ClientState*>(user_data);
    auto  n     = std::string_view{reinterpret_cast<const char*>(name), namelen};
    auto  v     = std::string_view{reinterpret_cast<const char*>(value), valuelen};
    if (n == "grpc-status") {
        state.reply.status = std::string{v};
        if (frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
            state.reply.status_in_response_headers = true;
        }
        else if (frame->headers.cat == NGHTTP2_HCAT_HEADERS) {
            state.reply.status_in_trailers = true;
        }
    }
    else if (n == "grpc-message") {
        state.reply.message = std::string{v};
    }
    return 0;
}

int on_data(nghttp2_session*, std::uint8_t, std::int32_t, const std::uint8_t* data, std::size_t len,
            void* user_data)
{
    auto& state = *static_cast<ClientState*>(user_data);
    auto  begin = reinterpret_cast<const std::byte*>(data);
    state.reply.data.insert(state.reply.data.end(), begin, begin + len);
    return 0;
}

int on_frame(nghttp2_session*, const nghttp2_frame* frame, void* user_data)
{
    auto& state = *static_cast<ClientState*>(user_data);
    if (frame->hd.stream_id == state.stream_id && (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0U)
    {
        state.done = true;
    }
    return 0;
}

void flush(nghttp2_session* session, asio::ip::tcp::socket& socket)
{
    for (;;) {
        const std::uint8_t* data{};
        const auto          len = nghttp2_session_mem_send(session, &data);
        if (len <= 0) {
            return;
        }
        asio::write(socket, asio::buffer(data, static_cast<std::size_t>(len)));
    }
}

GrpcReply unary_call(std::uint16_t port, std::string_view method, std::vector<std::byte> body,
                     std::string_view http_method = "POST")
{
    asio::io_context      io;
    asio::ip::tcp::socket socket{io};
    socket.connect({asio::ip::make_address("127.0.0.1"), port});

    ClientState state;
    state.socket       = &socket;
    state.request_body = std::move(body);

    nghttp2_session_callbacks* callbacks{};
    nghttp2_session_callbacks_new(&callbacks);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame);

    nghttp2_session* session{};
    nghttp2_session_client_new(&session, callbacks, &state);

    std::array settings{nghttp2_settings_entry{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}};
    nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, settings.data(), settings.size());

    std::array headers{
        nv(":method", http_method),
        nv(":scheme", "http"),
        nv(":authority", "127.0.0.1"),
        nv(":path", method),
        nv("content-type", "application/grpc"),
        nv("te", "trailers"),
    };
    nghttp2_data_provider provider{
        .source        = nghttp2_data_source{.ptr = &state},
        .read_callback = data_read,
    };
    state.stream_id = nghttp2_submit_request(session, nullptr, headers.data(), headers.size(),
                                             &provider, nullptr);
    flush(session, socket);

    std::array<std::uint8_t, 4096> buffer{};
    for (int attempts = 0; attempts < 200 && !state.done; ++attempts) {
        std::error_code ec;
        const auto      n = socket.read_some(asio::buffer(buffer), ec);
        if (ec) {
            break;
        }
        nghttp2_session_mem_recv(session, buffer.data(), n);
        flush(session, socket);
    }

    nghttp2_session_del(session);
    nghttp2_session_callbacks_del(callbacks);
    return state.reply;
}

class RunningGrpcApp
{
public:
    explicit RunningGrpcApp(std::size_t max_message_size = 1024)
        : http_port{free_port()}, grpc_port{free_port()}
    {
        auto grpc = std::make_unique<aevox::grpc::Plugin>(
            aevox::grpc::PluginConfig{.port = grpc_port, .max_message_size = max_message_size});
        auto echo = grpc->add_unary_method(
            "/aevox.test.Echo/Echo",
            [](aevox::grpc::UnaryRequest& req)
                -> aevox::Task<std::expected<aevox::grpc::UnaryResponse, aevox::grpc::GrpcStatus>> {
                aevox::grpc::UnaryResponse response;
                response.payload.assign(req.payload().begin(), req.payload().end());
                co_return response;
            });
        REQUIRE(echo.has_value());
        auto fail = grpc->add_unary_method(
            "/aevox.test.Echo/Fail",
            [](aevox::grpc::UnaryRequest&)
                -> aevox::Task<std::expected<aevox::grpc::UnaryResponse, aevox::grpc::GrpcStatus>> {
                co_return std::unexpected{
                    aevox::grpc::GrpcStatus::error(aevox::grpc::StatusCode::InvalidArgument,
                                                   "bad input")};
            });
        REQUIRE(fail.has_value());
        auto installed = app.install(std::move(grpc));
        REQUIRE(installed.has_value());
        app.get("/health", [](aevox::Request&) -> aevox::Task<aevox::Response> {
            co_return aevox::Response::ok("ok");
        });
        thread = std::jthread{[this] { app.listen(http_port); }};
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    ~RunningGrpcApp()
    {
        app.stop();
    }

    aevox::App    app;
    std::uint16_t http_port{};
    std::uint16_t grpc_port{};
    std::jthread  thread;
};

} // namespace

TEST_CASE("grpc unary loopback - echo returns payload and OK trailer", "[grpc][integration]")
{
    RunningGrpcApp server;
    auto           payload = bytes("hello");
    auto           reply = unary_call(server.grpc_port, "/aevox.test.Echo/Echo", envelope(payload));
    CHECK(reply.status == "0");
    CHECK_FALSE(reply.status_in_response_headers);
    CHECK(reply.status_in_trailers);
    CHECK(reply.data.size() >= 5U);
}

TEST_CASE("grpc unary loopback - unknown method returns Unimplemented", "[grpc][integration]")
{
    RunningGrpcApp server;
    auto           payload = bytes("hello");
    auto reply = unary_call(server.grpc_port, "/aevox.test.Echo/Missing", envelope(payload));
    CHECK(reply.status == "12");
    CHECK(reply.status_in_trailers);
}

TEST_CASE("grpc unary loopback - oversized request returns ResourceExhausted",
          "[grpc][integration]")
{
    RunningGrpcApp server{1};
    auto           payload = bytes("hello");
    auto           reply = unary_call(server.grpc_port, "/aevox.test.Echo/Echo", envelope(payload));
    CHECK(reply.status == "8");
    CHECK(reply.status_in_trailers);
}

TEST_CASE("grpc unary loopback - compressed request returns Unimplemented", "[grpc][integration]")
{
    RunningGrpcApp server;
    auto           payload = bytes("hello");
    auto reply = unary_call(server.grpc_port, "/aevox.test.Echo/Echo", envelope(payload, true));
    CHECK(reply.status == "12");
    CHECK(reply.status_in_trailers);
}

TEST_CASE("grpc unary loopback - handler error maps to grpc-status trailer", "[grpc][integration]")
{
    RunningGrpcApp server;
    auto           payload = bytes("hello");
    auto           reply = unary_call(server.grpc_port, "/aevox.test.Echo/Fail", envelope(payload));
    CHECK(reply.status == "3");
    CHECK(reply.status_in_trailers);
}

TEST_CASE("grpc unary loopback - non-POST request is rejected", "[grpc][integration]")
{
    RunningGrpcApp server;
    auto           payload = bytes("hello");
    auto reply = unary_call(server.grpc_port, "/aevox.test.Echo/Echo", envelope(payload), "GET");
    CHECK(reply.status == "13");
    CHECK(reply.status_in_trailers);
}

TEST_CASE("grpc unary loopback - extra bytes after unary envelope are rejected",
          "[grpc][integration]")
{
    RunningGrpcApp server;
    auto           payload = bytes("hello");
    auto           request = envelope(payload);
    request.push_back(std::byte{0x01});

    auto reply = unary_call(server.grpc_port, "/aevox.test.Echo/Echo", std::move(request));
    CHECK(reply.status == "13");
    CHECK(reply.status_in_trailers);
}
