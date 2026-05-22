// src/http/http_parser.cpp
//
// HttpParser implementation using llhttp.
//
// llhttp types (llhttp_t, llhttp_settings_t) are confined to this translation
// unit. They never appear in http_parser.hpp.
//
// Buffer lifetime invariants:
//   method, target, and header string_views point into parser_-owned strings.
//   They remain valid until the next feed() or reset() call.
//   body is a span into chunk_buf_ (owned by Impl). All body bytes — whether
//   Content-Length or chunked — are copied into chunk_buf_ by on_body(). This
//   gives body a uniform lifetime: valid until the next feed() or reset().
//
// Design: Tasks/architecture/AEV-003-arch.md §4.2

#include "http_parser.hpp"

#include <llhttp.h>

#include <cassert>
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aevox::detail {

namespace {
// Reserve capacity for the headers vector on each new request.
// Covers typical HTTP request sizes without reallocation.
constexpr std::size_t kHeadersReserveSize{16};
} // namespace

// =============================================================================
// HttpParser::Impl
// =============================================================================

class HttpParser::Impl
{
public:
    Impl()  = default;
    ~Impl() = default;

    Impl(const Impl&)            = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&)                 = delete;
    Impl& operator=(Impl&&)      = delete;

private:
    friend class HttpParser;

    llhttp_t          parser_{};
    llhttp_settings_t settings_{};
    ParserConfig      config_;

    // Current feed() buffer — set in feed(), cleared after return.
    std::string feed_text_{};

    // Accumulation state filled by llhttp callbacks:
    std::string method_buf_{};
    std::string target_buf_{};
    std::string field_buf_{};
    std::string value_buf_{};
    bool        in_value_{false}; // true after first on_header_value for this pair

    ParsedRequest                                    pending_{};
    std::vector<std::byte>                           chunk_buf_{}; // assembled chunked body
    std::vector<std::pair<std::string, std::string>> header_storage_{};

    std::size_t header_count_{0};
    std::size_t body_byte_count_{0};

    bool       complete_{false};
    ParseError last_error_{ParseError::BadRequest};
    bool       has_error_{false};

    // -------------------------------------------------------------------------
    // llhttp callbacks — must return 0 (HPE_OK) or an error code.
    // -------------------------------------------------------------------------

    [[nodiscard]] bool commit_header()
    {
        if (field_buf_.empty()) {
            return true;
        }

        header_storage_.emplace_back(std::move(field_buf_), std::move(value_buf_));
        field_buf_.clear();
        value_buf_.clear();
        header_count_++;
        if (header_count_ > config_.max_header_count) {
            has_error_  = true;
            last_error_ = ParseError::TooManyHeaders;
            return false;
        }
        return true;
    }

    void refresh_header_views()
    {
        pending_.headers.clear();
        pending_.headers.reserve(header_storage_.size());
        for (const auto& [name, value] : header_storage_) {
            pending_.headers.emplace_back(std::string_view{name}, std::string_view{value});
        }
    }

    static int on_url(llhttp_t* p, const char* at, std::size_t length)
    {
        auto& s = *static_cast<Impl*>(p->data);
        s.target_buf_.append(at, length);
        s.pending_.target = std::string_view{s.target_buf_};
        return HPE_OK;
    }

    static int on_method(llhttp_t* p, const char* at, std::size_t length)
    {
        auto& s = *static_cast<Impl*>(p->data);
        s.method_buf_.append(at, length);
        s.pending_.method = std::string_view{s.method_buf_};
        return HPE_OK;
    }

    static int on_header_field(llhttp_t* p, const char* at, std::size_t length)
    {
        auto& s = *static_cast<Impl*>(p->data);

        if (s.in_value_) {
            // Commit the previous field-value pair.
            if (!s.commit_header()) {
                return HPE_USER;
            }
            s.in_value_ = false;
        }

        s.field_buf_.append(at, length);

        return HPE_OK;
    }

    static int on_header_value(llhttp_t* p, const char* at, std::size_t length)
    {
        auto& s = *static_cast<Impl*>(p->data);

        s.value_buf_.append(at, length);
        s.in_value_ = true;

        return HPE_OK;
    }

    static int on_headers_complete(llhttp_t* p)
    {
        auto& s = *static_cast<Impl*>(p->data);

        // Commit the last header pair (if any).
        if (!s.field_buf_.empty()) {
            if (!s.commit_header()) {
                return HPE_USER;
            }
        }
        s.refresh_header_views();

        s.pending_.version_major = llhttp_get_http_major(p);
        s.pending_.version_minor = llhttp_get_http_minor(p);
        s.pending_.keep_alive    = (llhttp_should_keep_alive(p) != 0);
        s.pending_.upgrade       = (llhttp_get_upgrade(p) != 0);

        // Pre-allocate headers capacity now that count is known — no benefit here,
        // but chunk_buf_ capacity is retained across reset() calls for keep-alive.
        return HPE_OK;
    }

    static int on_body(llhttp_t* p, const char* at, std::size_t length)
    {
        auto& s = *static_cast<Impl*>(p->data);

        s.body_byte_count_ += length;
        if (s.body_byte_count_ > s.config_.max_body_bytes) {
            s.has_error_  = true;
            s.last_error_ = ParseError::TooLarge;
            return HPE_USER;
        }

        const auto body = std::string_view{at, length};
        for (const char byte : body) {
            s.chunk_buf_.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
        }
        return HPE_OK;
    }

    // on_message_complete — HPE_PAUSED return-code contract
    //
    // Returning HPE_PAUSED is the contract that feed() relies on to distinguish
    // "message complete_" from "need more data". Do NOT change this to
    // HPE_OK + llhttp_pause(): calling llhttp_pause() inside a callback does not
    // halt llhttp_execute() on the *current* call — the pause only takes effect
    // on the next call to llhttp_execute(), which means execute() returns HPE_OK
    // here, and feed()'s `rc == HPE_PAUSED` branch is never reached (Paused state
    // devlog deviation #1).
    static int on_message_complete(llhttp_t* p)
    {
        auto& s     = *static_cast<Impl*>(p->data);
        s.complete_ = true;
        return HPE_PAUSED;
    }
};

// =============================================================================
// HttpParser — special members (defined here: Impl is complete_)
// =============================================================================

HttpParser::HttpParser(ParserConfig config) noexcept : impl_{std::make_unique<Impl>()}
{
    impl_->config_ = config;
    impl_->pending_.headers.reserve(kHeadersReserveSize);

    llhttp_settings_init(&impl_->settings_);
    impl_->settings_.on_url              = Impl::on_url;
    impl_->settings_.on_method           = Impl::on_method;
    impl_->settings_.on_header_field     = Impl::on_header_field;
    impl_->settings_.on_header_value     = Impl::on_header_value;
    impl_->settings_.on_headers_complete = Impl::on_headers_complete;
    impl_->settings_.on_body             = Impl::on_body;
    impl_->settings_.on_message_complete = Impl::on_message_complete;

    llhttp_init(&impl_->parser_, HTTP_REQUEST, &impl_->settings_);
    impl_->parser_.data = impl_.get();
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

    impl_->feed_text_.clear();
    impl_->feed_text_.reserve(data.size());
    for (const std::byte byte : data) {
        impl_->feed_text_.push_back(std::to_integer<char>(byte));
    }

    const std::string_view feed_view{impl_->feed_text_};
    llhttp_errno_t const   rc = llhttp_execute(&impl_->parser_, feed_view.data(), feed_view.size());

    if (impl_->has_error_) {
        return std::unexpected{impl_->last_error_};
    }

    if (rc == HPE_PAUSED && impl_->complete_) {
        // Complete message parsed; resume parser_ state for next call.
        llhttp_resume(&impl_->parser_);

        // All body bytes (Content-Length and chunked alike) are in chunk_buf_ via
        // on_body(). body is a span into that internal buffer; it remains valid
        // until the next feed() or reset() call.
        impl_->pending_.body = std::span<const std::byte>{impl_->chunk_buf_};

        return std::move(impl_->pending_);
    }

    if (rc == HPE_OK) {
        // Need more data.
        return std::unexpected{ParseError::Incomplete};
    }

    // Any other error code (HPE_USER is handled via has_error_ above).
    return std::unexpected{ParseError::BadRequest};
}

// =============================================================================
// reset()
// =============================================================================

void HttpParser::reset() noexcept
{
    assert(impl_ && "reset() called on moved-from HttpParser");

    llhttp_reset(&impl_->parser_);
    impl_->parser_.data = impl_.get();

    // Clear accumulated state. chunk_buf_ capacity is retained (amortizes
    // re-allocations across keep-alive requests).
    impl_->pending_ = ParsedRequest{};
    impl_->chunk_buf_.clear();
    impl_->method_buf_.clear();
    impl_->target_buf_.clear();
    impl_->field_buf_.clear();
    impl_->value_buf_.clear();
    impl_->header_storage_.clear();
    impl_->in_value_        = false;
    impl_->header_count_    = 0;
    impl_->body_byte_count_ = 0;
    impl_->complete_        = false;
    impl_->has_error_       = false;

    impl_->pending_.headers.clear();
    impl_->pending_.headers.reserve(kHeadersReserveSize);
}

} // namespace aevox::detail
