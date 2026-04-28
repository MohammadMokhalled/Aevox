// src/router/app_impl.cpp
//
// INTERNAL — App constructor, destructor, listen(), stop(), router() accessors,
// and the per-connection HTTP handler loop (parse → dispatch → serialize → write).
//
// Signal handling: uses a file-scope std::atomic<Executor*> so that SIGINT/SIGTERM
// can call executor_->stop() safely. This is the v0.1 approach and is adequate for
// single-App deployments (one Executor per process).
//
// Design: Tasks/architecture/AEV-004-arch.md §8

#include <aevox/app.hpp>
#include <aevox/response.hpp>
#include <aevox/task.hpp>
#include <aevox/tcp_stream.hpp>

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <expected>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "config/toml_loader.hpp"
#include "http/http_parser.hpp"
#include "http/request_impl.hpp"
#include "http/response_impl.hpp"
#include "router/router_impl.hpp"

namespace aevox {

// =============================================================================
// File-scope signal state
// =============================================================================

namespace {

// v0.1 constraint: one App per process. A second App::listen() overwrites this
// global, breaking signal delivery for the first. Upgrade tracked as a future task.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<Executor*> g_signal_executor{nullptr};

// Reserve size for the per-request HTTP response header string builder.
constexpr std::size_t kResponseHeadReserveSize{256};

void handle_signal(int) noexcept
{
    if (auto* ex = g_signal_executor.load(std::memory_order_relaxed))
        ex->stop();
}

// =============================================================================
// HTTP response serializer
// =============================================================================

constexpr std::string_view status_text(int code) noexcept
{
    switch (code) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 413:
            return "Payload Too Large";
        case 500:
            return "Internal Server Error";
        default:
            return "Unknown";
    }
}

std::vector<std::byte> serialize_response(const Response& resp)
{
    const auto*            impl = get_response_impl(resp);
    const std::string_view body = resp.body_view();

    // Build the response header section into a std::string buffer.
    std::string head;
    head.reserve(kResponseHeadReserveSize);
    head += std::format("HTTP/1.1 {} {}\r\n", resp.status_code(), status_text(resp.status_code()));
    head += std::format("Content-Length: {}\r\n", body.size());

    if (impl) {
        for (const auto& [name, value] : impl->headers) {
            head += name;
            head += ": ";
            head += value;
            head += "\r\n";
        }
    }
    head += "\r\n";

    // Concatenate header + body into a single byte vector.
    std::vector<std::byte> bytes;
    bytes.resize(head.size() + body.size());
    std::memcpy(bytes.data(), head.data(), head.size());
    if (!body.empty())
        std::memcpy(bytes.data() + head.size(), body.data(), body.size());
    return bytes;
}

// =============================================================================
// dispatch_with_pipeline — dispatch through middleware pipeline (or directly).
//
// Extracted from App::listen() to avoid an immediately-invoked coroutine lambda
// (IIFE). An IIFE creates a temporary closure that is destroyed before the
// coroutine is resumed from an Asio callback, leaving a dangling reference in
// the coroutine frame. A named function receives its arguments by value/reference
// into the frame without any intermediate closure lifetime hazard.
// =============================================================================

Task<Response> dispatch_with_pipeline(Request& req, Router& router,
                                      std::vector<Middleware>&            global_mw,
                                      std::vector<ScopedMiddlewareEntry>& scoped_mw)
{
    // Fast path: no middleware — direct dispatch, identical to pre-AEV-024 code.
    if (global_mw.empty() && scoped_mw.empty()) {
        co_return co_await router.dispatch(req);
    }

    // Innermost: the router dispatch.
    std::move_only_function<Task<Response>(Request&)> next =
        [&router](Request& r) -> Task<Response> { co_return co_await router.dispatch(r); };

    // Collect scoped middleware whose prefix matches req.path() with boundary check.
    // Non-const references because Middleware::operator() is non-const (std::move_only_function
    // call operator is not const-qualified by the standard).
    std::vector<std::reference_wrapper<Middleware>> matching_scoped;
    for (auto& entry : scoped_mw) {
        const auto& path = req.path();
        if (path.starts_with(entry.prefix) &&
            (entry.prefix.size() == path.size() || path[entry.prefix.size()] == '/'))
        {
            matching_scoped.push_back(std::ref(entry.middleware));
        }
    }

    // Build the chain in two passes. The innermost layers must be wrapped first;
    // the outermost last. Desired execution order: GlobalA → GlobalB → ScopedA → handler.
    //
    // Pass 1 — scoped middleware wraps immediately around the router dispatch (innermost).
    // Iterate in reverse so that the first-registered scoped middleware executes first.
    for (auto& it : std::ranges::reverse_view(matching_scoped)) {
        auto prev = std::move(next);
        next      = [mw   = std::ref(it.get()),
                prev = std::move(prev)](Request& r) mutable -> Task<Response> {
            co_return co_await mw.get()(r, std::move(prev));
        };
    }

    // Pass 2 — global middleware wraps around the scoped chain (outermost).
    // Iterate in reverse so that the first-registered global middleware executes first.
    for (auto& it : std::ranges::reverse_view(global_mw)) {
        auto prev = std::move(next);
        next = [mw = std::ref(it), prev = std::move(prev)](Request& r) mutable -> Task<Response> {
            co_return co_await mw.get()(r, std::move(prev));
        };
    }

    co_return co_await next(req);
}

} // namespace

// =============================================================================
// App — constructor / destructor / move
// =============================================================================

App::App(AppConfig config) : impl_{std::make_unique<Impl>()}
{
    impl_->config   = std::move(config);
    impl_->executor = make_executor(impl_->config.executor);
}

App::~App() = default;

App::App(App&&) noexcept = default;

std::expected<App, ConfigErrorDetail> App::create(
    AppConfig base_config, std::optional<std::string_view> config_path) noexcept
{
    if (!config_path || config_path->empty())
        return App{std::move(base_config)};

    auto merged = aevox::config::load_toml_config(*config_path, std::move(base_config));
    if (!merged)
        return std::unexpected(std::move(merged.error()));

    return App{std::move(*merged)};
}

// =============================================================================
// App — config accessor
// =============================================================================

const AppConfig& App::config() const noexcept
{
    return impl_->config;
}

// =============================================================================
// App — router accessors
// =============================================================================

Router& App::router() noexcept
{
    return impl_->router;
}

const Router& App::router() const noexcept
{
    return impl_->router;
}

// =============================================================================
// App — group
// =============================================================================

Router App::group(std::string_view prefix)
{
    return impl_->router.group(prefix);
}

// =============================================================================
// App — listen
// =============================================================================

void App::listen(std::uint16_t port)
{
    // Install signal handlers so Ctrl-C stops the executor cleanly.
    g_signal_executor.store(impl_->executor.get(), std::memory_order_relaxed);
    std::signal(SIGINT, handle_signal);  // NOLINT: signal() is appropriate here
    std::signal(SIGTERM, handle_signal); // NOLINT

    const std::size_t max_body       = impl_->config.max_body_size;
    const std::size_t max_header_cnt = impl_->config.max_header_count;
    const std::size_t max_read       = impl_->config.max_read_bytes;
    Router&           router         = impl_->router;
    auto&             global_mw      = impl_->global_middlewares;
    auto&             scoped_mw      = impl_->scoped_middlewares;

    auto connection_handler = [max_body, max_header_cnt, max_read, &router, &global_mw,
                               &scoped_mw](std::uint64_t /*conn_id*/,
                                           TcpStream stream) -> Task<void> {
        detail::HttpParser parser{{.max_header_count = max_header_cnt, .max_body_bytes = max_body}};

        for (;;) {
            auto read_result = co_await stream.read(max_read);
            if (!read_result)
                co_return; // EOF or I/O error — close connection

            auto buf = std::move(*read_result);
            if (buf.empty())
                co_return; // clean EOF

            auto parsed = parser.feed(std::span{buf});

            if (!parsed) {
                if (parsed.error() == detail::ParseError::Incomplete)
                    continue; // need more data

                // Protocol error — send 400 and close; ignore write error (closing anyway)
                auto err_bytes = serialize_response(Response::bad_request("Bad Request"));
                (void)(co_await stream.write(std::span{err_bytes}));
                co_return;
            }

            const bool keep_alive = parsed->keep_alive;

            // Build Request — buf moved into Impl; parsed views remain valid because
            // vector move preserves heap address. parsed->body spans into parser's
            // internal chunk_buf, which is valid until parser.reset().
            auto req = make_request_from_impl(std::move(buf), std::move(*parsed));

            // Dispatch through the pipeline (fast path handled inside the function).
            auto response = co_await dispatch_with_pipeline(req, router, global_mw, scoped_mw);

            // Serialize and write the response.
            auto resp_bytes = serialize_response(response);
            auto write_res  = co_await stream.write(std::span{resp_bytes});
            if (!write_res)
                co_return; // write error — close connection

            if (!keep_alive)
                co_return; // Connection: close

            parser.reset(); // prepare for next pipelined request
        }
    };

    auto lr = impl_->executor->listen(port, std::move(connection_handler));
    if (!lr) {
        // Bind or listen failed — terminate (startup defect, not recoverable).
        std::terminate();
    }

    auto run_result = impl_->executor->run();
    (void)run_result; // stop() → run() returns success; errors are not recoverable here

    // Clear signal handler so a second listen() call (UB per contract, but defensive)
    // does not double-install.
    g_signal_executor.store(nullptr, std::memory_order_relaxed);
}

void App::listen()
{
    listen(impl_->config.port);
}

// =============================================================================
// App — stop
// =============================================================================

void App::stop() noexcept
{
    if (impl_ && impl_->executor)
        impl_->executor->stop();
}

// =============================================================================
// App::use_impl() — middleware registration helpers
// =============================================================================

void App::use_impl(Middleware mw)
{
    if (!impl_)
        return;
    impl_->global_middlewares.push_back(std::move(mw));
}

void App::use_impl(std::string_view prefix, Middleware mw)
{
    if (!impl_)
        return;
    impl_->scoped_middlewares.push_back({std::string(prefix), std::move(mw)});
}

} // namespace aevox
