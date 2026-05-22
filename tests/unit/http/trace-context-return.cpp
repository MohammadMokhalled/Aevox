// trace-context-return.cpp: verify Request::trace_context() contract
// ADD ref: Tasks/architecture/AEV-012-arch.md §8.1

#include <aevox/request.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "http/http_parser.hpp"
#include "http/request_impl.hpp"
#include "http/traceparent.hpp"

namespace {

/// Build a Request with a traceparent header injected into the parsed headers.
[[nodiscard]] aevox::Request make_request_with_traceparent(std::string_view traceparent_val)
{
    // Minimal valid HTTP request raw bytes.
    const std::string raw      = "GET /test HTTP/1.1\r\nHost: localhost\r\ntraceparent: ";
    const std::string raw_full = raw + std::string{traceparent_val} + "\r\n\r\n";

    std::vector<std::byte> buf;
    buf.reserve(raw_full.size());
    for (char c : raw_full) {
        buf.push_back(static_cast<std::byte>(c));
    }

    aevox::detail::ParsedRequest pr;
    pr.method     = "GET";
    pr.target     = "/test";
    pr.keep_alive = false;

    // Add traceparent header. Views must point into the buffer.
    const auto name_pos = raw_full.find("traceparent: ");
    const auto val_pos  = name_pos + 13; // strlen("traceparent: ") == 13
    const auto val_end  = raw_full.find("\r\n", val_pos);

    pr.headers.emplace_back(std::string_view{reinterpret_cast<const char*>(buf.data()) + name_pos,
                                             11},
                            std::string_view{reinterpret_cast<const char*>(buf.data()) + val_pos,
                                             val_end - val_pos});

    return aevox::make_request_from_impl(std::move(buf), std::move(pr));
}

[[nodiscard]] aevox::Request make_request_without_traceparent()
{
    const std::string raw = "GET /test HTTP/1.1\r\nHost: localhost\r\n\r\n";

    std::vector<std::byte> buf;
    buf.reserve(raw.size());
    for (char c : raw) {
        buf.push_back(static_cast<std::byte>(c));
    }

    aevox::detail::ParsedRequest pr;
    pr.method     = "GET";
    pr.target     = "/test";
    pr.keep_alive = false;

    return aevox::make_request_from_impl(std::move(buf), std::move(pr));
}

} // namespace

TEST_CASE("trace_context - returns nullopt when no traceparent header", "[http][tracing]")
{
    auto       req    = make_request_without_traceparent();
    const auto result = req.trace_context();
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("trace_context - returns view of original header when valid", "[http][tracing]")
{
    const std::string tp  = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
    auto              req = make_request_with_traceparent(tp);

    // Populate traceparent fields as app_impl.cpp would do.
    auto       impl   = aevox::get_mutable_request_impl(req);
    const auto parsed = aevox::detail::parse_traceparent(tp);
    if (!parsed) {
        FAIL("parse_traceparent should return a value");
    }
    const auto& pv = *parsed;
    impl->get().set_trace_id(std::string(pv.trace_id.begin(), pv.trace_id.end()));
    impl->get().set_span_id(std::string(pv.parent_id.begin(), pv.parent_id.end()));
    impl->get().set_traceparent(tp);

    const auto result = req.trace_context();
    if (!result) {
        FAIL("trace_context should return a value");
    }
    REQUIRE(*result == tp);
}

TEST_CASE("trace_context - returns nullopt when traceparent header is invalid", "[http][tracing]")
{
    // Invalid traceparent — wrong version.
    const std::string tp  = "01-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
    auto              req = make_request_with_traceparent(tp);

    // app_impl.cpp would parse and reject — traceparent field stays empty.
    const auto result = req.trace_context();
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("trace_context - string_view survives Request move", "[http][tracing]")
{
    const std::string tp  = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
    auto              req = make_request_with_traceparent(tp);

    // Populate traceparent fields as app_impl.cpp would.
    auto       impl   = aevox::get_mutable_request_impl(req);
    const auto parsed = aevox::detail::parse_traceparent(tp);
    if (!parsed) {
        FAIL("parse_traceparent should return a value");
    }
    const auto& pv = *parsed;
    impl->get().set_trace_id(std::string(pv.trace_id.begin(), pv.trace_id.end()));
    impl->get().set_span_id(std::string(pv.parent_id.begin(), pv.parent_id.end()));
    impl->get().set_traceparent(tp);

    // Move the Request — unique_ptr<Impl> transfer preserves the allocation
    // address, so the string_view into traceparent remains valid.
    const aevox::Request moved = std::move(req);
    const auto           view  = moved.trace_context();
    if (!view) {
        FAIL("trace_context should return a value after move");
    }
    REQUIRE(*view == tp);
}
