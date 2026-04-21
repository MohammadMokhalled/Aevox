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

} // namespace

// =============================================================================
// App — constructor / destructor / move
// =============================================================================

App::App(AppConfig config) : impl_{std::make_unique<Impl>()}
{
    impl_->config_   = std::move(config);
    impl_->executor_ = make_executor(impl_->config_.executor);
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
    return impl_->config_;
}

// =============================================================================
// App — router accessors
// =============================================================================

Router& App::router() noexcept
{
    return impl_->router_;
}

const Router& App::router() const noexcept
{
    return impl_->router_;
}

// =============================================================================
// App — group
// =============================================================================

Router App::group(std::string_view prefix)
{
    return impl_->router_.group(prefix);
}

// =============================================================================
// App — listen
// =============================================================================

void App::listen(std::uint16_t port)
{
    // Install signal handlers so Ctrl-C stops the executor cleanly.
    g_signal_executor.store(impl_->executor_.get(), std::memory_order_relaxed);
    std::signal(SIGINT, handle_signal);  // NOLINT: signal() is appropriate here
    std::signal(SIGTERM, handle_signal); // NOLINT

    const std::size_t max_body       = impl_->config_.max_body_size;
    const std::size_t max_header_cnt = impl_->config_.max_header_count;
    const std::size_t max_read       = impl_->config_.max_read_bytes;
    Router&           router         = impl_->router_;

    auto connection_handler = [max_body, max_header_cnt, max_read,
                               &router](std::uint64_t /*conn_id*/, TcpStream stream) -> Task<void> {
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

            // Dispatch through the Router.
            auto response = co_await router.dispatch(req);

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

    auto lr = impl_->executor_->listen(port, std::move(connection_handler));
    if (!lr) {
        // Bind or listen failed — terminate (startup defect, not recoverable).
        std::terminate();
    }

    auto run_result = impl_->executor_->run();
    (void)run_result; // stop() → run() returns success; errors are not recoverable here

    // Clear signal handler so a second listen() call (UB per contract, but defensive)
    // does not double-install.
    g_signal_executor.store(nullptr, std::memory_order_relaxed);
}

void App::listen()
{
    listen(impl_->config_.port);
}

// =============================================================================
// App — stop
// =============================================================================

void App::stop() noexcept
{
    if (impl_ && impl_->executor_)
        impl_->executor_->stop();
}

} // namespace aevox
