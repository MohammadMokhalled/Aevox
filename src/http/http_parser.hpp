#pragma once
// src/http/http_parser.hpp
//
// INTERNAL — never included outside src/http/ or tests/.
//
// Declares HttpParser, ParsedRequest, ParseError, and ParserConfig.
// llhttp types are confined to http_parser.cpp — the pimpl keeps them
// out of this header.
//
// Buffer ownership (read before using ParsedRequest):
//   ParsedRequest::method, target, and headers are zero-copy views into the
//   buffer passed to feed(). Keep that buffer alive for the lifetime of the
//   ParsedRequest.
//   ParsedRequest::body is a span into an internal buffer (chunk_buf) owned by
//   HttpParser::Impl. ALL body bytes — Content-Length and chunked alike — are
//   accumulated there, giving body a uniform lifetime regardless of transfer
//   encoding. The caller must finish using ParsedRequest before calling feed()
//   or reset() (either invalidates body).
//
// Thread-safety:
//   HttpParser is not thread-safe. Use one instance per connection coroutine.
//
// Design: Tasks/architecture/AEV-003-arch.md §3.3, §4.2

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace aevox::detail {

namespace {
// Default maximum number of request headers for the parser's safety net.
// Not user-configurable at this level — the user-facing limit flows from
// AppConfig::max_header_count via the connection handler.
constexpr std::size_t kParserDefaultMaxHeaderCount{100};
// Default maximum body size for the parser's safety net (1 MiB).
// Overridden per-connection from AppConfig::max_body_size.
constexpr std::size_t kParserDefaultMaxBodyBytes{1u * 1024u * 1024u};
} // namespace

// =============================================================================
// ParseError
// =============================================================================

/**
 * Error codes returned by HttpParser::feed() via std::expected.
 * Never thrown.
 */
enum class ParseError : std::uint8_t
{
    Incomplete,     ///< Buffer ended mid-request; call feed() again with more data.
    BadRequest,     ///< Malformed request (invalid method, bad headers, etc.).
    TooLarge,       ///< Body exceeds ParserConfig::max_body_bytes.
    TooManyHeaders, ///< Header count exceeds ParserConfig::max_header_count.
    Unsupported,    ///< HTTP version or feature not supported in v0.1.
};

// =============================================================================
// ParsedRequest
// =============================================================================

/**
 * Structured view of a parsed HTTP/1.1 request.
 *
 * method, target, and headers are zero-copy views into the buffer passed to
 * HttpParser::feed(). The caller must keep that buffer alive until reset() or
 * the next feed() call invalidates this struct. body is an exception: see below.
 *
 * body: All body bytes — whether delivered via Content-Length or chunked
 *       transfer encoding — are accumulated into an internal buffer owned by
 *       HttpParser::Impl (chunk_buf). ParsedRequest::body is a span into that
 *       internal buffer and is valid until the next call to feed() or reset().
 *       This gives body a uniform, connection-scoped lifetime regardless of
 *       transfer encoding (intentional simplification; see Tasks/progress/AEV-003-devlog.md A-2).
 *
 * Thread-safety: not thread-safe — value type, do not share between threads.
 */
struct ParsedRequest
{
    std::string_view method; // e.g. "GET", "POST"
    std::string_view target; // e.g. "/users/42?sort=asc"
    int              version_major{1};
    int              version_minor{1};

    // Headers as (name, value) pairs. Views into the feed() buffer.
    // Header names are NOT lowercased — comparison must be case-insensitive
    // per RFC 7230 §3.2.
    std::vector<std::pair<std::string_view, std::string_view>> headers;

    // Span into the parser's internal chunk_buf. Valid until reset() or feed().
    std::span<const std::byte> body;

    bool upgrade{false};   // true if an "Upgrade" header is present
    bool keep_alive{true}; // true if connection should persist (HTTP/1.1 default)
};

// =============================================================================
// ParserConfig
// =============================================================================

/**
 * Configuration for one HttpParser instance.
 * All limits are enforced during parsing; violations return ParseError.
 */
struct ParserConfig
{
    /// Maximum number of headers in one request. Safety-net default.
    /// Per-connection value is always supplied from AppConfig::max_header_count.
    std::size_t max_header_count{kParserDefaultMaxHeaderCount};

    /// Maximum entity body size in bytes. Safety-net default.
    /// Per-connection value is always supplied from AppConfig::max_body_size.
    std::size_t max_body_bytes{kParserDefaultMaxBodyBytes};
};

// =============================================================================
// HttpParser
// =============================================================================

/**
 * Incremental HTTP/1.1 request parser.
 *
 * Wraps llhttp via a pimpl (HttpParser::Impl in http_parser.cpp). llhttp types
 * never appear in this header.
 *
 * Usage:
 *   HttpParser parser;
 *   while (true) {
 *       auto buf = co_await stream.read();
 *       if (!buf) break;
 *       auto result = parser.feed(std::span{*buf});
 *       if (result) {
 *           handle(*result);
 *           parser.reset();   // prepare for the next pipelined request
 *       } else if (result.error() == ParseError::Incomplete) {
 *           continue;
 *       } else {
 *           break;            // protocol error — close connection
 *       }
 *   }
 *
 * Thread-safety: NOT thread-safe. One instance per connection coroutine.
 * Move semantics: Moved-from HttpParser has impl_ == nullptr; calling feed()
 *   on a moved-from instance is undefined behaviour.
 * Ownership: Owns llhttp_t and all accumulation buffers via Impl.
 */
class HttpParser
{
public:
    /**
     * Constructs a parser with the given limits.
     * @param config  Header count and body size limits. All fields have defaults.
     */
    explicit HttpParser(ParserConfig config = {}) noexcept;

    // Move-only: llhttp_t has non-trivial internal state that must not be copied.
    HttpParser(HttpParser&&) noexcept;
    HttpParser& operator=(HttpParser&&) noexcept;

    HttpParser(const HttpParser&)            = delete;
    HttpParser& operator=(const HttpParser&) = delete;

    // Destructor declared here, defined in http_parser.cpp (Impl is incomplete here).
    ~HttpParser() noexcept;

    /**
     * Feeds raw bytes into the parser.
     *
     * Returns ParsedRequest when a complete request is parsed (string_view and
     * span fields point into `data` and into the parser's internal chunk buffer).
     * Returns ParseError::Incomplete when more data is needed (call feed() again).
     * Returns any other ParseError on a fatal protocol violation (close the connection;
     * do not call reset() — construct a new HttpParser instead).
     *
     * @param data  Raw bytes from the TCP stream. Must remain alive until the
     *              returned ParsedRequest (if any) is no longer used.
     * @return  std::expected<ParsedRequest, ParseError>.
     *
     * @note noexcept — llhttp errors are translated to ParseError. std::bad_alloc
     *       from internal vector growth is not caught (allocation failure is not a
     *       ParseError — it is unrecoverable and propagates to std::terminate).
     */
    [[nodiscard]] std::expected<ParsedRequest, ParseError> feed(
        std::span<const std::byte> data) noexcept;

    /**
     * Resets the parser for the next pipelined request.
     *
     * Clears all accumulated header, body, and error state. After reset() the
     * parser is ready to accept a new request regardless of whether the previous
     * feed() returned a hard error (e.g. BadRequest, TooLarge). This makes a
     * single HttpParser instance usable across the lifetime of a keep-alive
     * connection, including after malformed requests that are recovered at the
     * connection level (e.g. by sending a 400 response and continuing).
     *
     * chunk_buf capacity is retained across calls to amortize re-allocations on
     * keep-alive connections.
     *
     * Any ParsedRequest returned by the preceding feed() call is invalidated
     * immediately (its string_view and span fields dangle after reset()).
     */
    void reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aevox::detail
