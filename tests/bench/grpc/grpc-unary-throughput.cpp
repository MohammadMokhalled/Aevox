// Unary gRPC loopback throughput baseline.
#include <aevox/app.hpp>
#include <aevox/plugins/grpc.hpp>
#include <aevox/response.hpp>

#include <asio.hpp>

#include <nanobench.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <iostream>
#include <memory>
#include <nghttp2/nghttp2.h>
#include <span>
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

[[nodiscard]] std::vector<std::byte> make_payload(std::size_t size)
{
    return std::vector<std::byte>(size, std::byte{0x2A});
}

[[nodiscard]] std::vector<std::byte> envelope(std::span<const std::byte> payload)
{
    std::vector<std::byte> out;
    out.reserve(payload.size() + 5U);
    out.push_back(std::byte{0});
    const auto length = static_cast<std::uint32_t>(payload.size());
    out.push_back(static_cast<std::byte>((length >> 24U) & 0xFFU));
    out.push_back(static_cast<std::byte>((length >> 16U) & 0xFFU));
    out.push_back(static_cast<std::byte>((length >> 8U) & 0xFFU));
    out.push_back(static_cast<std::byte>(length & 0xFFU));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

struct ClientState
{
    std::vector<std::byte> request_body;
    std::size_t            request_offset{};
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

bool unary_call(std::uint16_t port, std::span<const std::byte> payload)
{
    asio::io_context      io;
    asio::ip::tcp::socket socket{io};
    socket.connect({asio::ip::make_address("127.0.0.1"), port});

    ClientState state{.request_body = envelope(payload)};

    nghttp2_session_callbacks* callbacks{};
    nghttp2_session_callbacks_new(&callbacks);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame);

    nghttp2_session* session{};
    nghttp2_session_client_new(&session, callbacks, &state);

    std::array settings{nghttp2_settings_entry{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}};
    nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, settings.data(), settings.size());

    std::array headers{
        nv(":method", "POST"),
        nv(":scheme", "http"),
        nv(":authority", "127.0.0.1"),
        nv(":path", "/aevox.bench.Echo/Echo"),
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
    return state.done;
}

class RunningGrpcApp
{
public:
    RunningGrpcApp() : http_port{free_port()}, grpc_port{free_port()}
    {
        auto grpc =
            std::make_unique<aevox::grpc::Plugin>(aevox::grpc::PluginConfig{.port = grpc_port});
        auto registered = grpc->add_unary_method(
            "/aevox.bench.Echo/Echo",
            [](aevox::grpc::UnaryRequest& req)
                -> aevox::Task<std::expected<aevox::grpc::UnaryResponse, aevox::grpc::GrpcStatus>> {
                aevox::grpc::UnaryResponse response;
                response.payload.assign(req.payload().begin(), req.payload().end());
                co_return response;
            });
        if (!registered) {
            std::terminate();
        }
        auto installed = app.install(std::move(grpc));
        if (!installed) {
            std::terminate();
        }
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

int main()
{
    RunningGrpcApp server;
    auto           small  = make_payload(32U);
    auto           medium = make_payload(1024U);

    ankerl::nanobench::Bench bench;
    bench.title("grpc unary throughput").unit("request").warmup(3).minEpochIterations(5);

    bench.run("grpc unary throughput - echo 32B", [&] {
        const auto ok = unary_call(server.grpc_port, small);
        ankerl::nanobench::doNotOptimizeAway(ok);
        if (!ok) {
            std::terminate();
        }
    });

    bench.run("grpc unary throughput - echo 1KiB", [&] {
        const auto ok = unary_call(server.grpc_port, medium);
        ankerl::nanobench::doNotOptimizeAway(ok);
        if (!ok) {
            std::terminate();
        }
    });

    std::cout << "gRPC unary loopback baseline\n";
    return 0;
}
