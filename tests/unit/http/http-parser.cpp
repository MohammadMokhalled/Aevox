// HttpParser unit tests — no I/O.
// ADD ref: Tasks/architecture/AEV-003-arch.md §8.1

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "http/http_parser.hpp"

using namespace aevox::detail;

// Helper: convert a string literal into a byte span backed by a vector.
static std::vector<std::byte> to_bytes(std::string_view s)
{
    std::vector<std::byte> buf(s.size());
    std::memcpy(buf.data(), s.data(), s.size());
    return buf;
}

// =============================================================================

TEST_CASE("HTTP/1.1 parser - happy path GET no body", "[http][parser]")
{
    HttpParser parser;
    auto       buf    = to_bytes("GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n");
    auto       result = parser.feed(std::span{buf});

    REQUIRE(result.has_value());
    CHECK(result->method == "GET");
    CHECK(result->target == "/index.html");
    CHECK(result->version_major == 1);
    CHECK(result->version_minor == 1);
    CHECK(result->keep_alive == true);
    CHECK(result->body.empty());
    CHECK(result->upgrade == false);
    REQUIRE(result->headers.size() == 1);
    CHECK(result->headers[0].first == "Host");
    CHECK(result->headers[0].second == "example.com");
}

// =============================================================================

TEST_CASE("HTTP/1.1 parser - happy path POST with Content-Length body", "[http][parser]")
{
    HttpParser parser;
    auto       buf    = to_bytes("POST /submit HTTP/1.1\r\n"
                                          "Host: example.com\r\n"
                                          "Content-Length: 5\r\n"
                                          "\r\n"
                                          "hello");
    auto       result = parser.feed(std::span{buf});

    REQUIRE(result.has_value());
    CHECK(result->method == "POST");
    CHECK(result->target == "/submit");
    REQUIRE(result->body.size() == 5);

    std::string body_str(reinterpret_cast<const char*>(result->body.data()), result->body.size());
    CHECK(body_str == "hello");

    bool found_cl = false;
    for (auto const& [name, value] : result->headers) {
        if (name == "Content-Length") {
            CHECK(value == "5");
            found_cl = true;
        }
    }
    CHECK(found_cl);
}

// =============================================================================

TEST_CASE("HTTP/1.1 parser - headers parsed as string_view pairs", "[http][parser]")
{
    HttpParser parser;
    auto       buf    = to_bytes("GET / HTTP/1.1\r\n"
                                          "Host: localhost\r\n"
                                          "Accept: text/html\r\n"
                                          "X-Custom: value123\r\n"
                                          "\r\n");
    auto       result = parser.feed(std::span{buf});

    REQUIRE(result.has_value());
    REQUIRE(result->headers.size() == 3);

    // Verify names and values are correct.
    CHECK(result->headers[0].first == "Host");
    CHECK(result->headers[0].second == "localhost");
    CHECK(result->headers[1].first == "Accept");
    CHECK(result->headers[1].second == "text/html");
    CHECK(result->headers[2].first == "X-Custom");
    CHECK(result->headers[2].second == "value123");

    // Views must point into the original buffer.
    const char* buf_start = reinterpret_cast<const char*>(buf.data());
    const char* buf_end   = buf_start + buf.size();
    for (auto const& [name, value] : result->headers) {
        CHECK(name.data() >= buf_start);
        CHECK(name.data() < buf_end);
        CHECK(value.data() >= buf_start);
        CHECK(value.data() < buf_end);
    }
}

// =============================================================================

TEST_CASE("HTTP/1.1 parser - truncated request returns Incomplete", "[http][parser]")
{
    HttpParser parser;

    SECTION("first half returns Incomplete")
    {
        auto first = to_bytes("GET / HTTP/1.1\r\nHost: loc");
        auto r1    = parser.feed(std::span{first});
        REQUIRE_FALSE(r1.has_value());
        CHECK(r1.error() == ParseError::Incomplete);
    }

    SECTION("second feed with remainder returns success")
    {
        auto first  = to_bytes("GET / HTTP/1.1\r\nHost: loc");
        auto second = to_bytes("alhost\r\n\r\n");
        (void)parser.feed(std::span{first});
        auto r2 = parser.feed(std::span{second});
        REQUIRE(r2.has_value());
        CHECK(r2->method == "GET");
        CHECK(r2->target == "/");
    }
}

// =============================================================================

TEST_CASE("HTTP/1.1 parser - invalid method returns BadRequest", "[http][parser]")
{
    HttpParser parser;
    auto       buf    = to_bytes("NOTAVERB / HTTP/1.1\r\n\r\n");
    auto       result = parser.feed(std::span{buf});

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == ParseError::BadRequest);
}

// =============================================================================

TEST_CASE("HTTP/1.1 parser - chunked transfer encoding assembled", "[http][parser]")
{
    HttpParser parser;
    auto       buf    = to_bytes("POST /data HTTP/1.1\r\n"
                                          "Host: example.com\r\n"
                                          "Transfer-Encoding: chunked\r\n"
                                          "\r\n"
                                          "5\r\n"
                                          "hello\r\n"
                                          "6\r\n"
                                          " world\r\n"
                                          "0\r\n"
                                          "\r\n");
    auto       result = parser.feed(std::span{buf});

    REQUIRE(result.has_value());
    REQUIRE(result->body.size() == 11);

    std::string body_str(reinterpret_cast<const char*>(result->body.data()), result->body.size());
    CHECK(body_str == "hello world");
}

// =============================================================================

TEST_CASE("HTTP/1.1 parser - HTTP Upgrade header detected", "[http][parser]")
{
    HttpParser parser;
    auto       buf    = to_bytes("GET /ws HTTP/1.1\r\n"
                                          "Host: example.com\r\n"
                                          "Upgrade: websocket\r\n"
                                          "Connection: Upgrade\r\n"
                                          "\r\n");
    auto       result = parser.feed(std::span{buf});

    REQUIRE(result.has_value());
    CHECK(result->upgrade == true);
}

// =============================================================================

TEST_CASE("HTTP/1.1 parser - limits and edge cases", "[http][parser]")
{
    SECTION("max_header_count limit enforced")
    {
        ParserConfig cfg;
        cfg.max_header_count = 3;
        HttpParser parser{cfg};

        // Build a request with 4 headers (exceeds limit of 3).
        std::string req    = "GET / HTTP/1.1\r\n"
                             "A: 1\r\n"
                             "B: 2\r\n"
                             "C: 3\r\n"
                             "D: 4\r\n"
                             "\r\n";
        auto        buf    = to_bytes(req);
        auto        result = parser.feed(std::span{buf});

        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == ParseError::TooManyHeaders);
    }

    SECTION("max_body_bytes limit enforced")
    {
        ParserConfig cfg;
        cfg.max_body_bytes = 5;
        HttpParser parser{cfg};

        auto buf    = to_bytes("POST / HTTP/1.1\r\n"
                                  "Host: x\r\n"
                                  "Content-Length: 6\r\n"
                                  "\r\n"
                                  "toolng");
        auto result = parser.feed(std::span{buf});

        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == ParseError::TooLarge);
    }

    SECTION("HTTP/1.0 sets version_minor=0 and keep_alive=false")
    {
        HttpParser parser;
        auto       buf    = to_bytes("GET / HTTP/1.0\r\nHost: x\r\n\r\n");
        auto       result = parser.feed(std::span{buf});

        REQUIRE(result.has_value());
        CHECK(result->version_major == 1);
        CHECK(result->version_minor == 0);
        CHECK(result->keep_alive == false);
    }

    SECTION("reset() and reuse - two sequential requests")
    {
        HttpParser parser;

        auto buf1 = to_bytes("GET /first HTTP/1.1\r\nHost: a\r\n\r\n");
        auto r1   = parser.feed(std::span{buf1});
        REQUIRE(r1.has_value());
        CHECK(r1->target == "/first");

        parser.reset();

        auto buf2 = to_bytes("GET /second HTTP/1.1\r\nHost: b\r\n\r\n");
        auto r2   = parser.feed(std::span{buf2});
        REQUIRE(r2.has_value());
        CHECK(r2->target == "/second");
    }

    SECTION("Connection: close sets keep_alive=false")
    {
        HttpParser parser;
        auto       buf    = to_bytes("GET / HTTP/1.1\r\n"
                                              "Host: x\r\n"
                                              "Connection: close\r\n"
                                              "\r\n");
        auto       result = parser.feed(std::span{buf});

        REQUIRE(result.has_value());
        CHECK(result->keep_alive == false);
    }
}
