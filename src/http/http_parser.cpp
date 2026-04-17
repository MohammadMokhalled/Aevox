// src/http/http_parser.cpp
//
// HttpParser implementation using llhttp.
//
// llhttp types (llhttp_t, llhttp_settings_t) are confined to this translation
// unit. They never appear in http_parser.hpp.
//
// Buffer lifetime invariants:
//   method, target, and header string_views point into the buffer passed to
//   feed(). The caller owns that buffer. llhttp_execute() is synchronous — all
//   callbacks fire and return before feed() returns, so the pointers are valid
//   for the duration of feed().
//   body is a span into chunk_buf (owned by Impl). All body bytes — whether
//   Content-Length or chunked — are copied into chunk_buf by on_body(). This
//   gives body a uniform lifetime: valid until the next feed() or reset().
//
// reinterpret_cast note:
//   llhttp callbacks use const char* for pointer+length pairs. Casting from
//   const char* to const std::byte* (and vice versa) is well-defined per
//   C++23 [basic.types.general]: std::byte aliases any object type.
//   Each cast carries this comment.
//
// Design: AEV-003-arch.md §4.2

#include "http_parser.hpp"

#include <llhttp.h>

#include <algorithm>
#include <cassert>
#include <cstring>

namespace aevox::detail {

// =============================================================================
// HttpParser::Impl
// =============================================================================

struct HttpParser::Impl
{
    llhttp_t          parser{};
    llhttp_settings_t settings{};
    ParserConfig      config;

    // Current feed() buffer — set in feed(), cleared after return.
    // Callbacks use this pointer to compute string_view offsets.
    const char* feed_ptr{nullptr};
    std::size_t feed_len{0};

    // Accumulation state filled by llhttp callbacks:
    const char* field_ptr{nullptr}; // current header field name start
    std::size_t field_len{0};
    const char* value_ptr{nullptr}; // current header value start
    std::size_t value_len{0};
    bool        in_value{false}; // true after first on_header_value for this pair

    ParsedRequest          pending{};
    std::vector<std::byte> chunk_buf{}; // assembled chunked body

    std::size_t header_count{0};
    std::size_t body_byte_count{0};

    bool       complete{false};
    ParseError last_error{ParseError::BadRequest};
    bool       has_error{false};

    // -------------------------------------------------------------------------
    // llhttp callbacks — must return 0 (HPE_OK) or an error code.
    // -------------------------------------------------------------------------

    static int on_url(llhttp_t* p, const char* at, std::size_t length)
    {
        auto& s = *static_cast<Impl*>(p->data);
        // Extend target string_view to cover this segment.
        if (s.pending.target.empty()) {
            // reinterpret_cast: const char* → const std::byte* per C++23 [basic.types.general]
            s.pending.target = std::string_view{at, length};
        }
        else {
            // Extend by re-forming the view (llhttp may call on_url multiple times for long URLs).
            const char* start = s.pending.target.data();
            std::size_t total = static_cast<std::size_t>((at + length) - start);
            s.pending.target  = std::string_view{start, total};
        }
        return HPE_OK;
    }

    static int on_method(llhttp_t* p, const char* at, std::size_t length)
    {
        auto& s = *static_cast<Impl*>(p->data);
        if (s.pending.method.empty()) {
            s.pending.method = std::string_view{at, length};
        }
        else {
            const char* start = s.pending.method.data();
            std::size_t total = static_cast<std::size_t>((at + length) - start);
            s.pending.method  = std::string_view{start, total};
        }
        return HPE_OK;
    }

    static int on_header_field(llhttp_t* p, const char* at, std::size_t length)
    {
        auto& s = *static_cast<Impl*>(p->data);

        if (s.in_value) {
            // Commit the previous field-value pair.
            s.pending.headers.emplace_back(std::string_view{s.field_ptr, s.field_len},
                                           std::string_view{s.value_ptr, s.value_len});
            s.header_count++;
            if (s.header_count > s.config.max_header_count) {
                s.has_error  = true;
                s.last_error = ParseError::TooManyHeaders;
                return HPE_USER;
            }
            s.in_value  = false;
            s.field_ptr = nullptr; // must reset so next field starts fresh
        }

        // Start or extend the current field name.
        if (!s.in_value && s.field_ptr == nullptr) {
            s.field_ptr = at;
            s.field_len = length;
        }
        else if (!s.in_value) {
            s.field_len = static_cast<std::size_t>((at + length) - s.field_ptr);
        }

        return HPE_OK;
    }

    static int on_header_value(llhttp_t* p, const char* at, std::size_t length)
    {
        auto& s = *static_cast<Impl*>(p->data);

        if (!s.in_value) {
            s.value_ptr = at;
            s.value_len = length;
            s.in_value  = true;
        }
        else {
            s.value_len = static_cast<std::size_t>((at + length) - s.value_ptr);
        }

        return HPE_OK;
    }

    static int on_headers_complete(llhttp_t* p)
    {
        auto& s = *static_cast<Impl*>(p->data);

        // Commit the last header pair (if any).
        if (s.field_ptr != nullptr) {
            s.pending.headers.emplace_back(std::string_view{s.field_ptr, s.field_len},
                                           std::string_view{s.value_ptr, s.value_len});
            s.header_count++;
            if (s.header_count > s.config.max_header_count) {
                s.has_error  = true;
                s.last_error = ParseError::TooManyHeaders;
                return HPE_USER;
            }
            s.field_ptr = nullptr;
            s.value_ptr = nullptr;
        }

        s.pending.version_major = llhttp_get_http_major(p);
        s.pending.version_minor = llhttp_get_http_minor(p);
        s.pending.keep_alive    = (llhttp_should_keep_alive(p) != 0);
        s.pending.upgrade       = (llhttp_get_upgrade(p) != 0);

        // Pre-allocate headers capacity now that count is known — no benefit here,
        // but chunk_buf capacity is retained across reset() calls for keep-alive.
        return HPE_OK;
    }

    static int on_body(llhttp_t* p, const char* at, std::size_t length)
    {
        auto& s = *static_cast<Impl*>(p->data);

        s.body_byte_count += length;
        if (s.body_byte_count > s.config.max_body_bytes) {
            s.has_error  = true;
            s.last_error = ParseError::TooLarge;
            return HPE_USER;
        }

        // reinterpret_cast: const char* → const std::byte* per C++23 [basic.types.general]
        const auto* bytes = reinterpret_cast<const std::byte*>(at);
        s.chunk_buf.insert(s.chunk_buf.end(), bytes, bytes + length);
        return HPE_OK;
    }

    // on_message_complete — HPE_PAUSED return-code contract
    //
    // Returning HPE_PAUSED is the contract that feed() relies on to distinguish
    // "message complete" from "need more data". Do NOT change this to
    // HPE_OK + llhttp_pause(): calling llhttp_pause() inside a callback does not
    // halt llhttp_execute() on the *current* call — the pause only takes effect
    // on the next call to llhttp_execute(), which means execute() returns HPE_OK
    // here, and feed()'s `rc == HPE_PAUSED` branch is never reached (see AEV-003
    // devlog deviation #1).
    static int on_message_complete(llhttp_t* p)
    {
        auto& s    = *static_cast<Impl*>(p->data);
        s.complete = true;
        return HPE_PAUSED;
    }
};

// =============================================================================
// HttpParser — special members (defined here: Impl is complete)
// =============================================================================

HttpParser::HttpParser(ParserConfig config) noexcept : impl_{std::make_unique<Impl>()}
{
    impl_->config = std::move(config);
    impl_->pending.headers.reserve(16);

    llhttp_settings_init(&impl_->settings);
    impl_->settings.on_url              = Impl::on_url;
    impl_->settings.on_method           = Impl::on_method;
    impl_->settings.on_header_field     = Impl::on_header_field;
    impl_->settings.on_header_value     = Impl::on_header_value;
    impl_->settings.on_headers_complete = Impl::on_headers_complete;
    impl_->settings.on_body             = Impl::on_body;
    impl_->settings.on_message_complete = Impl::on_message_complete;

    llhttp_init(&impl_->parser, HTTP_REQUEST, &impl_->settings);
    impl_->parser.data = impl_.get();
}

HttpParser::HttpParser(HttpParser&&) noexcept            = default;
HttpParser& HttpParser::operator=(HttpParser&&) noexcept = default;
HttpParser::~HttpParser() noexcept                       = default;

// =============================================================================
// feed()
// =============================================================================

[[nodiscard]] std::expected<ParsedRequest, ParseError> HttpParser::feed(
    std::span<const std::byte> data) noexcept
{
    assert(impl_ && "feed() called on moved-from HttpParser");

    // reinterpret_cast: const std::byte* → const char* for llhttp.
    // Well-defined per C++23 [basic.types.general].
    const auto* ptr = reinterpret_cast<const char*>(data.data());
    std::size_t len = data.size();

    impl_->feed_ptr = ptr;
    impl_->feed_len = len;

    llhttp_errno_t rc = llhttp_execute(&impl_->parser, ptr, len);

    impl_->feed_ptr = nullptr;
    impl_->feed_len = 0;

    if (impl_->has_error) {
        return std::unexpected{impl_->last_error};
    }

    if (rc == HPE_PAUSED && impl_->complete) {
        // Complete message parsed; resume parser state for next call.
        llhttp_resume(&impl_->parser);

        // All body bytes (Content-Length and chunked alike) are in chunk_buf via
        // on_body(). body is a span into that internal buffer; it remains valid
        // until the next feed() or reset() call.
        impl_->pending.body = std::span<const std::byte>{impl_->chunk_buf};

        return std::move(impl_->pending);
    }

    if (rc == HPE_OK) {
        // Need more data.
        return std::unexpected{ParseError::Incomplete};
    }

    // Any other error code (HPE_USER is handled via has_error above).
    return std::unexpected{ParseError::BadRequest};
}

// =============================================================================
// reset()
// =============================================================================

void HttpParser::reset() noexcept
{
    assert(impl_ && "reset() called on moved-from HttpParser");

    llhttp_reset(&impl_->parser);
    impl_->parser.data = impl_.get();

    // Clear accumulated state. chunk_buf capacity is retained (amortizes
    // re-allocations across keep-alive requests).
    impl_->pending = ParsedRequest{};
    impl_->chunk_buf.clear();
    impl_->field_ptr       = nullptr;
    impl_->field_len       = 0;
    impl_->value_ptr       = nullptr;
    impl_->value_len       = 0;
    impl_->in_value        = false;
    impl_->header_count    = 0;
    impl_->body_byte_count = 0;
    impl_->complete        = false;
    impl_->has_error       = false;

    impl_->pending.headers.clear();
    impl_->pending.headers.reserve(16);
}

} // namespace aevox::detail
