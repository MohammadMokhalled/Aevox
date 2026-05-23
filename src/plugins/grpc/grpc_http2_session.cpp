#include "grpc_http2_session.hpp"

#include <aevox/plugins/grpc.hpp>
#include <aevox/task.hpp>
#include <aevox/tcp_stream.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <memory>
#include <nghttp2/nghttp2.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "grpc_envelope.hpp"

namespace aevox::grpc::detail {

namespace {

constexpr std::size_t      kReadSize{16384};
constexpr std::size_t      kGrpcEnvelopeSize{5};
constexpr std::size_t      kInitialResponseHeaderCount{2};
constexpr std::size_t      kStatusTrailerCount{2};
constexpr std::string_view kContentType{"application/grpc"};

[[nodiscard]] std::string to_lower_ascii(std::string_view text)
{
    std::string lowered{text};
    for (auto& ch : lowered) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return lowered;
}

[[nodiscard]] bool starts_with_grpc_content_type(std::string_view value) noexcept
{
    return value.starts_with(kContentType);
}

[[nodiscard]] bool is_reserved_metadata(std::string_view name) noexcept
{
    return name == "content-type" || name == "te" || name == "grpc-timeout" ||
           name == "grpc-encoding" || name == "grpc-accept-encoding";
}

[[nodiscard]] bool is_reserved_trailer(std::string_view name) noexcept
{
    return name == "grpc-status" || name == "grpc-message";
}

[[nodiscard]] std::string status_code_text(StatusCode code)
{
    return std::to_string(static_cast<int>(code));
}

[[nodiscard]] std::string sanitize_grpc_message(std::string_view message)
{
    std::string sanitized;
    sanitized.reserve(message.size());
    for (const auto ch : message) {
        if (ch == '\r' || ch == '\n') {
            sanitized.push_back(' ');
        }
        else {
            sanitized.push_back(ch);
        }
    }
    return sanitized;
}

struct HeaderStorage
{
    std::vector<std::uint8_t> name;
    std::vector<std::uint8_t> value;
};

[[nodiscard]] std::vector<std::uint8_t> to_octets(std::string_view text)
{
    std::vector<std::uint8_t> octets;
    octets.reserve(text.size());
    for (const auto ch : text) {
        octets.push_back(static_cast<std::uint8_t>(ch));
    }
    return octets;
}

[[nodiscard]] HeaderStorage make_header(std::string_view name, std::string_view value)
{
    return HeaderStorage{.name = to_octets(name), .value = to_octets(value)};
}

[[nodiscard]] nghttp2_nv make_nv(HeaderStorage& header) noexcept
{
    return nghttp2_nv{
        .name     = header.name.data(),
        .value    = header.value.data(),
        .namelen  = header.name.size(),
        .valuelen = header.value.size(),
        .flags    = NGHTTP2_NV_FLAG_NONE,
    };
}

} // namespace

struct StreamState
{
    std::int32_t               stream_id{};
    std::string                http_method;
    std::string                method;
    std::string                content_type;
    std::vector<std::byte>     payload;
    std::vector<Metadata>      metadata;
    std::optional<GrpcStatus>  early_status;
    std::vector<std::byte>     response_bytes;
    std::size_t                response_offset{};
    std::vector<HeaderStorage> response_trailers;
    std::vector<nghttp2_nv>    trailer_headers;
    bool                       end_stream{};
    bool                       submitted{};
    bool                       trailers_submitted{};
};

class GrpcHttp2Session::Impl
{
public:
    Impl(TcpStream stream, MethodMap& methods, const PluginConfig& config)
        : stream_{std::move(stream)}, methods_{methods}, config_{config}
    {
        nghttp2_session_callbacks* raw_callbacks{};
        (void)nghttp2_session_callbacks_new(&raw_callbacks);
        callbacks_.reset(raw_callbacks);

        nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks_.get(), on_begin_headers);
        nghttp2_session_callbacks_set_on_header_callback(callbacks_.get(), on_header);
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks_.get(),
                                                                  on_data_chunk_recv);
        nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks_.get(), on_frame_recv);
        nghttp2_session_callbacks_set_send_callback(callbacks_.get(), on_blocked_send);

        nghttp2_session* raw_session{};
        (void)nghttp2_session_server_new(&raw_session, callbacks_.get(), this);
        session_.reset(raw_session);
    }

    [[nodiscard]] Task<void> run()
    {
        std::array<nghttp2_settings_entry, 1> settings{
            nghttp2_settings_entry{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
                                   config_.max_concurrent_streams},
        };
        (void)nghttp2_submit_settings(session_.get(), NGHTTP2_FLAG_NONE, settings.data(),
                                      settings.size());
        co_await flush();

        for (;;) {
            auto read_result = co_await stream_.read(kReadSize);
            if (!read_result) {
                co_return;
            }
            auto bytes = std::move(*read_result);
            if (bytes.empty()) {
                co_return;
            }

            std::vector<std::uint8_t> octets;
            octets.reserve(bytes.size());
            for (const auto byte : bytes) {
                octets.push_back(std::to_integer<std::uint8_t>(byte));
            }

            const auto consumed =
                nghttp2_session_mem_recv(session_.get(), octets.data(), octets.size());
            if (consumed < 0) {
                co_return;
            }

            co_await process_ready_streams();
            co_await flush();
        }
    }

private:
    struct CallbackDeleter
    {
        void operator()(nghttp2_session_callbacks* callbacks) const noexcept
        {
            nghttp2_session_callbacks_del(callbacks);
        }
    };

    struct SessionDeleter
    {
        void operator()(nghttp2_session* session) const noexcept
        {
            nghttp2_session_del(session);
        }
    };

    static ssize_t on_blocked_send(nghttp2_session*, const std::uint8_t*, std::size_t, int,
                                   void*) noexcept
    {
        return NGHTTP2_ERR_WOULDBLOCK;
    }

    static int on_begin_headers(nghttp2_session*, const nghttp2_frame* frame, void* user_data)
    {
        if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
            return 0;
        }

        auto&       self = *static_cast<Impl*>(user_data);
        StreamState state;
        state.stream_id                    = frame->hd.stream_id;
        self.streams_[frame->hd.stream_id] = std::move(state);
        return 0;
    }

    static int on_header(nghttp2_session*, const nghttp2_frame* frame, const std::uint8_t* name,
                         std::size_t namelen, const std::uint8_t* value, std::size_t valuelen,
                         std::uint8_t, void* user_data)
    {
        if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
            return 0;
        }

        auto& self = *static_cast<Impl*>(user_data);
        auto  it   = self.streams_.find(frame->hd.stream_id);
        if (it == self.streams_.end()) {
            return 0;
        }

        // nghttp2 callback byte spans are valid only for this call.
        std::string raw_header_name;
        raw_header_name.reserve(namelen);
        for (const auto byte : std::span{name, namelen}) {
            raw_header_name.push_back(static_cast<char>(byte));
        }
        std::string header_value;
        header_value.reserve(valuelen);
        for (const auto byte : std::span{value, valuelen}) {
            header_value.push_back(static_cast<char>(byte));
        }
        const auto header_name = to_lower_ascii(raw_header_name);

        if (header_name == ":method") {
            it->second.http_method = header_value;
        }
        else if (header_name == ":path") {
            it->second.method = header_value;
        }
        else if (header_name == "content-type") {
            it->second.content_type = header_value;
        }
        else if (!header_name.starts_with(':') && !is_reserved_metadata(header_name)) {
            it->second.metadata.push_back(Metadata{.name = header_name, .value = header_value});
        }

        return 0;
    }

    static int on_data_chunk_recv(nghttp2_session*, std::uint8_t, std::int32_t stream_id,
                                  const std::uint8_t* data, std::size_t len, void* user_data)
    {
        auto& self = *static_cast<Impl*>(user_data);
        auto  it   = self.streams_.find(stream_id);
        if (it == self.streams_.end()) {
            return 0;
        }

        if (it->second.payload.size() + len > self.config_.max_message_size + kGrpcEnvelopeSize) {
            it->second.early_status =
                GrpcStatus::error(StatusCode::ResourceExhausted, "message too large");
            return 0;
        }

        const auto chunk = std::as_bytes(std::span{data, len});
        it->second.payload.insert(it->second.payload.end(), chunk.begin(), chunk.end());
        return 0;
    }

    static int on_frame_recv(nghttp2_session*, const nghttp2_frame* frame, void* user_data)
    {
        if ((frame->hd.flags & NGHTTP2_FLAG_END_STREAM) == 0U) {
            return 0;
        }

        auto& self = *static_cast<Impl*>(user_data);
        auto  it   = self.streams_.find(frame->hd.stream_id);
        if (it != self.streams_.end()) {
            it->second.end_stream = true;
            self.ready_streams_.push_back(frame->hd.stream_id);
        }
        return 0;
    }

    static ssize_t on_data_read(nghttp2_session* session, std::int32_t, std::uint8_t* buf,
                                std::size_t length, std::uint32_t* data_flags,
                                nghttp2_data_source* source, void*)
    {
        auto* state = static_cast<StreamState*>(source->ptr);
        if (state == nullptr) {
            *data_flags |= NGHTTP2_DATA_FLAG_EOF;
            return 0;
        }

        const auto remaining = state->response_bytes.size() - state->response_offset;
        const auto to_copy   = std::min(length, remaining);
        if (to_copy > 0U) {
            const auto response = std::span{state->response_bytes}.subspan(state->response_offset);
            std::memcpy(buf, response.data(), to_copy);
            state->response_offset += to_copy;
        }
        if (state->response_offset >= state->response_bytes.size()) {
            *data_flags |= NGHTTP2_DATA_FLAG_EOF | NGHTTP2_DATA_FLAG_NO_END_STREAM;
            if (!state->trailers_submitted) {
                state->trailers_submitted = true;
                (void)nghttp2_submit_trailer(session, state->stream_id,
                                             state->trailer_headers.data(),
                                             state->trailer_headers.size());
            }
        }
        return static_cast<ssize_t>(to_copy);
    }

    [[nodiscard]] Task<void> process_ready_streams()
    {
        while (!ready_streams_.empty()) {
            const auto stream_id = ready_streams_.front();
            ready_streams_.erase(ready_streams_.begin());

            auto it = streams_.find(stream_id);
            if (it == streams_.end()) {
                continue;
            }

            auto& state = it->second;
            if (state.submitted) {
                continue;
            }
            if (!state.end_stream) {
                continue;
            }
            state.submitted = true;

            if (state.early_status) {
                submit_status(state, *state.early_status);
                continue;
            }
            if (state.http_method != "POST") {
                submit_status(state, GrpcStatus::error(StatusCode::Internal, "invalid method"));
                continue;
            }
            if (!starts_with_grpc_content_type(state.content_type)) {
                submit_status(state,
                              GrpcStatus::error(StatusCode::Internal, "invalid content-type"));
                continue;
            }

            auto method = methods_.get().find(state.method);
            if (method == methods_.get().end()) {
                submit_status(state,
                              GrpcStatus::error(StatusCode::Unimplemented, "method not found"));
                continue;
            }

            auto decoded = decode_unary_message(std::span{state.payload}, config_.max_message_size);
            if (!decoded) {
                submit_status(state, status_for_decode_error(decoded.error()));
                continue;
            }

            UnaryRequest request{state.method, std::move(decoded->payload), state.metadata};
            try {
                auto response = co_await method->second(request);
                if (!response) {
                    submit_status(state, response.error());
                    continue;
                }

                auto reserved =
                    std::ranges::find_if(response->trailing_metadata, [](const auto& md) {
                        return is_reserved_trailer(to_lower_ascii(md.name));
                    });
                if (reserved != response->trailing_metadata.end()) {
                    submit_status(state, GrpcStatus::error(StatusCode::Internal,
                                                           "reserved trailer metadata"));
                    continue;
                }

                auto encoded =
                    encode_unary_message(std::span{response->payload}, config_.max_message_size);
                if (!encoded) {
                    submit_status(state, status_for_decode_error(encoded.error()));
                    continue;
                }

                state.response_bytes = std::move(*encoded);
                submit_response(state, GrpcStatus::ok(), response->trailing_metadata);
            }
            catch (...) {
                submit_status(state, GrpcStatus::error(StatusCode::Unknown, "handler failed"));
            }
        }
    }

    [[nodiscard]] static GrpcStatus status_for_decode_error(GrpcError error)
    {
        switch (error) {
            case GrpcError::MessageTooLarge:
                return GrpcStatus::error(StatusCode::ResourceExhausted, "message too large");
            case GrpcError::CompressionUnsupported:
                return GrpcStatus::error(StatusCode::Unimplemented, "compression unsupported");
            default:
                return GrpcStatus::error(StatusCode::Internal, "protocol error");
        }
    }

    void submit_status(StreamState& state, const GrpcStatus& status)
    {
        state.response_bytes.clear();
        state.response_offset = 0U;
        submit_response(state, status, {});
    }

    void submit_response(StreamState& state, const GrpcStatus& status,
                         std::span<const Metadata> trailing_metadata)
    {
        const auto status_text = status_code_text(status.code);
        const auto status_msg  = sanitize_grpc_message(status.message);

        std::vector<HeaderStorage> response_storage;
        response_storage.reserve(kInitialResponseHeaderCount);
        response_storage.push_back(make_header(":status", "200"));
        response_storage.push_back(make_header("content-type", kContentType));

        std::vector<nghttp2_nv> response_headers;
        response_headers.reserve(kInitialResponseHeaderCount);
        for (auto& header : response_storage) {
            response_headers.push_back(make_nv(header));
        }

        state.response_trailers.clear();
        state.response_trailers.reserve(trailing_metadata.size() + kStatusTrailerCount);
        for (const auto& metadata : trailing_metadata) {
            state.response_trailers.push_back(
                make_header(to_lower_ascii(metadata.name), metadata.value));
        }
        state.response_trailers.push_back(make_header("grpc-status", status_text));
        if (!status_msg.empty()) {
            state.response_trailers.push_back(make_header("grpc-message", status_msg));
        }

        state.trailer_headers.clear();
        state.trailer_headers.reserve(state.response_trailers.size());
        for (auto& trailer : state.response_trailers) {
            state.trailer_headers.push_back(make_nv(trailer));
        }
        state.trailers_submitted = false;

        const nghttp2_data_provider provider{
            .source        = nghttp2_data_source{.ptr = &state},
            .read_callback = on_data_read,
        };

        (void)nghttp2_submit_response(session_.get(), state.stream_id, response_headers.data(),
                                      response_headers.size(), &provider);
    }

    [[nodiscard]] Task<void> flush()
    {
        for (;;) {
            const std::uint8_t* data{};
            const auto          len = nghttp2_session_mem_send(session_.get(), &data);
            if (len <= 0) {
                co_return;
            }

            const auto bytes = std::as_bytes(std::span{data, static_cast<std::size_t>(len)});
            auto       wr    = co_await stream_.write(bytes);
            if (!wr) {
                co_return;
            }
        }
    }

    TcpStream                         stream_;
    std::reference_wrapper<MethodMap> methods_;
    PluginConfig                      config_;

    std::unique_ptr<nghttp2_session_callbacks, CallbackDeleter> callbacks_;
    std::unique_ptr<nghttp2_session, SessionDeleter>            session_;
    std::unordered_map<std::int32_t, StreamState>               streams_;
    std::vector<std::int32_t>                                   ready_streams_;
};

GrpcHttp2Session::GrpcHttp2Session(TcpStream stream, MethodMap& methods, const PluginConfig& config)
    : impl_{std::make_unique<Impl>(std::move(stream), methods, config)}
{}

GrpcHttp2Session::~GrpcHttp2Session() = default;

Task<void> GrpcHttp2Session::run()
{
    co_await impl_->run();
}

} // namespace aevox::grpc::detail
