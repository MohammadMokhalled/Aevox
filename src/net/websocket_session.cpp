// src/net/websocket_session.cpp
//
// INTERNAL — WebSocketSession: Asio-backed WebSocket connection session.
// Implements the read loop, send queue, strand-serialized send, and close handshake.
// Also implements WebSocket public handle out-of-line members (PIMPL delegation).
//
// All Asio types are confined to this file and websocket_session.hpp.
//
// Design: Tasks/architecture/AEV-010-arch.md §4.4

#include "net/websocket_session.hpp"

#include <aevox/async.hpp> // tl_post_to_io

#include "net/asio_tcp_stream.hpp" // get_tcp_stream_executor()
#include "net/topic_bus.hpp"

namespace aevox::net {

// =============================================================================
// WebSocketSession — construction
// =============================================================================

WebSocketSession::WebSocketSession(PrivateTag, aevox::TcpStream stream,
                                   asio::strand<asio::io_context::executor_type> strand,
                                   TopicBus* bus, aevox::WebSocketHandler handler,
                                   std::size_t max_payload, std::string remote_addr,
                                   std::string initial_topic)
    : stream_{std::move(stream)}, bus_{bus}, handler_{std::move(handler)},
      max_payload_{max_payload}, remote_addr_{std::move(remote_addr)},
      current_topic_{std::move(initial_topic)}, strand_{std::move(strand)}
{}

WebSocketSession::~WebSocketSession()
{
    // Unsubscribe from all topics when the session is destroyed.
    if (bus_) {
        bus_->unsubscribe(static_cast<const aevox::net::TopicSubscriber*>(this));
    }
}

// =============================================================================
// WebSocketSession::create — factory
// =============================================================================

std::shared_ptr<WebSocketSession> WebSocketSession::create(aevox::TcpStream stream, TopicBus* bus,
                                                           aevox::WebSocketHandler handler,
                                                           std::size_t             max_payload,
                                                           std::string             remote_addr,
                                                           std::string             initial_topic)
{
    // Obtain the io_context executor from the TcpStream before moving it.
    // get_tcp_stream_executor() is defined in asio_tcp_stream.cpp where TcpStream::Impl
    // is complete — avoids dereferencing the opaque Impl pointer here.
    auto executor = aevox::net::get_tcp_stream_executor(stream);
    // make_strand() preferred over brace-init: strand(executor) ctor is explicit.
    auto strand = asio::make_strand(executor);

    // Use std::make_shared via the PrivateTag pattern — avoids raw new/delete.
    return std::make_shared<WebSocketSession>(PrivateTag{}, std::move(stream), std::move(strand),
                                              bus, std::move(handler), max_payload,
                                              std::move(remote_addr), std::move(initial_topic));
}

// =============================================================================
// WebSocketSession::send_text
// =============================================================================

std::expected<void, aevox::WebSocketError> WebSocketSession::send_text(std::string_view message)
{
    if (closed_.load(std::memory_order_acquire)) {
        return std::unexpected(
            aevox::WebSocketError{aevox::WebSocketErrorCode::Closed, "WebSocket is closed"});
    }

    // Build the text frame.
    // std::as_bytes(span<const char>) → span<const std::byte>: zero-cost reinterpretation
    // of char data as bytes per C++23 [span.objectrep]. No explicit cast required.
    std::vector<std::byte> frame =
        emit_frame(Opcode::Text, true,
                   std::as_bytes(std::span<const char>{message.data(), message.size()}));

    // enqueue_frame() posts to the strand — safe to call from any thread.
    enqueue_frame(std::move(frame));
    return {};
}

// =============================================================================
// WebSocketSession::send_binary
// =============================================================================

std::expected<void, aevox::WebSocketError> WebSocketSession::send_binary(
    std::span<const std::byte> data)
{
    if (closed_.load(std::memory_order_acquire)) {
        return std::unexpected(
            aevox::WebSocketError{aevox::WebSocketErrorCode::Closed, "WebSocket is closed"});
    }

    std::vector<std::byte> frame = emit_frame(Opcode::Binary, true, data);
    // enqueue_frame() posts to the strand — safe to call from any thread.
    enqueue_frame(std::move(frame));
    return {};
}

// =============================================================================
// WebSocketSession::close_graceful
// =============================================================================

aevox::Task<std::expected<void, aevox::WebSocketError>> WebSocketSession::close_graceful(
    std::uint16_t code, std::string_view reason)
{
    if (closed_.load(std::memory_order_acquire)) {
        co_return std::unexpected(aevox::WebSocketError{aevox::WebSocketErrorCode::Closed,
                                                        "WebSocket is already closed"});
    }

    bool expected = false;
    if (close_sent_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
    {
        enqueue_priority_frame(emit_close_frame(code, reason));
    }
    co_return {};
}

// =============================================================================
// WebSocketSession::subscribe
// =============================================================================

void WebSocketSession::subscribe(std::string_view topic)
{
    current_topic_ = std::string{topic};
    if (bus_) {
        // shared_from_this() returns shared_ptr<WebSocketSession>; TopicBus needs
        // shared_ptr<TopicSubscriber>. Since WebSocketSession : TopicSubscriber,
        // the implicit conversion applies.
        const auto sub = std::shared_ptr<aevox::net::TopicSubscriber>{shared_from_this()};
        bus_->subscribe(topic, sub);
    }
}

// =============================================================================
// WebSocketSession::publish
// =============================================================================

void WebSocketSession::publish(std::string_view topic, std::string_view message)
{
    if (bus_) {
        bus_->publish(topic, message, static_cast<const aevox::net::TopicSubscriber*>(this));
    }
}

// =============================================================================
// WebSocketSession — accessors
// =============================================================================

std::string_view WebSocketSession::current_topic() const noexcept
{
    return current_topic_;
}

std::string_view WebSocketSession::remote_address() const noexcept
{
    return remote_addr_;
}

bool WebSocketSession::is_closed() const noexcept
{
    return closed_.load(std::memory_order_acquire);
}

// =============================================================================
// WebSocketSession::send_from_bus — called by TopicBus from any thread
// =============================================================================

void WebSocketSession::send_from_bus(std::string_view message)
{
    if (closed_.load(std::memory_order_acquire))
        return;

    // Copy the message before posting (the view may be transient).
    std::string msg_copy{message};

    asio::post(strand_, [self = shared_from_this(), msg = std::move(msg_copy)]() mutable {
        if (self->closed_.load(std::memory_order_acquire))
            return;
        // M-4: enforce the same depth limit as enqueue_frame() — bus publishes are
        // not exempt; an unchecked bus with a slow consumer would exhaust memory.
        if (self->send_queue_.size() >= kMaxSendQueueDepth)
            return;
        auto frame = emit_frame(Opcode::Text, true,
                                std::as_bytes(std::span<const char>{msg.data(), msg.size()}));
        self->send_queue_.push_back(std::move(frame));
        if (!self->sending_) {
            self->sending_ = true;
            // ff_drain_queue is a FireAndForget wrapper — frame self-destructs
            // when drain_send_queue finishes. Return value is void (discarded).
            self->ff_drain_queue(self);
        }
    });
}

// =============================================================================
// WebSocketSession::enqueue_frame
// =============================================================================

void WebSocketSession::enqueue_frame(std::vector<std::byte> frame)
{
    asio::post(strand_, [self = shared_from_this(), f = std::move(frame)]() mutable {
        if (self->send_queue_.size() >= kMaxSendQueueDepth) {
            // Queue full — drop the frame (back-pressure signal).
            // The send() coroutine has already returned success to the caller;
            // in a future refinement we'd propagate this back.
            // For v0.2, queue overflow drops the frame.
            return;
        }
        self->send_queue_.push_back(std::move(f));
        if (!self->sending_) {
            self->sending_ = true;
            // ff_drain_queue is a FireAndForget wrapper — frame self-destructs
            // when drain_send_queue finishes. Return value is void (discarded).
            self->ff_drain_queue(self);
        }
    });
}

// =============================================================================
// WebSocketSession::enqueue_priority_frame
// =============================================================================

void WebSocketSession::enqueue_priority_frame(std::vector<std::byte> frame)
{
    // Control frames (Close, Pong) go to the front of the queue bypassing the depth limit.
    // RFC 6455 §5.5 requires control frames to be responded to promptly; dropping them
    // would violate the protocol. push_front ensures they are sent before pending data frames.
    asio::post(strand_, [self = shared_from_this(), f = std::move(frame)]() mutable {
        self->send_queue_.push_front(std::move(f));
        if (!self->sending_) {
            self->sending_ = true;
            self->ff_drain_queue(self);
        }
    });
}

// =============================================================================
// WebSocketSession::drain_send_queue — runs on the I/O thread (strand-posted)
// =============================================================================

aevox::Task<void> WebSocketSession::drain_send_queue(std::shared_ptr<WebSocketSession> self)
{
    while (!self->send_queue_.empty()) {
        auto frame = std::move(self->send_queue_.front());
        self->send_queue_.pop_front();
        auto res = co_await self->stream_.write(std::span{frame});
        if (!res) {
            // Write error — mark closed and stop draining.
            self->closed_.store(true, std::memory_order_release);
            self->send_queue_.clear();
            break;
        }
    }
    self->sending_ = false;
}

// =============================================================================
// WebSocketSession::start_read_loop
// =============================================================================

void WebSocketSession::start_read_loop(std::shared_ptr<WebSocketSession> self_ptr)
{
    // Post ff_read_loop as a fire-and-forget onto the I/O executor thread.
    // FireAndForget uses suspend_never for final_suspend so the frame self-destructs
    // when do_read_loop finishes — no Task owner needed, no dangling frame.
    auto post_to_io = aevox::detail::tl_post_to_io();
    if (post_to_io) {
        post_to_io([self = std::move(self_ptr)]() mutable {
            self->ff_read_loop(self); // return value is void — self-managing frame
        });
    }
}

// =============================================================================
// WebSocketSession::wait_for_close
// =============================================================================

aevox::Task<void> WebSocketSession::wait_for_close()
{
    if (closed_.load(std::memory_order_seq_cst)) {
        co_return;
    }
    // Park this coroutine until do_read_loop signals via close_waiter_addr_.
    //
    // M-5 fix: close_waiter_addr_ is std::atomic<void*>. The TOCTOU window between
    // await_ready() returning false and await_suspend() storing the handle is closed by
    // a double-check in await_suspend: if closed_ became true in the window, we CAS
    // the handle back out and return false (resume immediately) rather than staying
    // suspended and never waking up.
    struct WaitAwaitable
    {
        WebSocketSession* session;

        [[nodiscard]] bool await_ready() const noexcept
        {
            return session->closed_.load(std::memory_order_seq_cst);
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept
        {
            // Store atomically so do_read_loop can see it from any thread.
            session->close_waiter_addr_.store(h.address(), std::memory_order_seq_cst);

            if (!session->closed_.load(std::memory_order_seq_cst)) {
                return true; // genuinely suspended — read loop will resume us
            }

            // closed_ became true in the window between await_ready and here.
            // Attempt to reclaim our handle via CAS before the read loop's exchange
            // takes it. If we win: resume immediately (return false). If the read loop
            // already claimed it (address already null): stay suspended and let the
            // loop resume us via h.resume().
            const auto handle_addr = h.address();
            // NOLINTNEXTLINE(misc-const-correctness) compare_exchange_strong may update expected.
            void* expected = handle_addr;
            return !session->close_waiter_addr_.compare_exchange_strong(expected, nullptr,
                                                                        std::memory_order_seq_cst,
                                                                        std::memory_order_seq_cst);
        }

        void await_resume() noexcept {}
    };

    co_await WaitAwaitable{this};
}

// =============================================================================
// WebSocketSession::make_handle
// =============================================================================

aevox::WebSocket WebSocketSession::make_handle(std::shared_ptr<WebSocketSession> self_ptr)
{
    auto impl     = std::make_unique<aevox::WebSocket::Impl>();
    impl->session = std::move(self_ptr);
    // Unqualified call — make_websocket_handle is a friend of WebSocket, injected
    // into namespace aevox; ADL on unique_ptr<WebSocket::Impl> finds it there.
    return make_websocket_handle(std::move(impl));
}

// =============================================================================
// WebSocketSession::do_read_loop — the main read coroutine
// =============================================================================

aevox::Task<void> WebSocketSession::do_read_loop(std::shared_ptr<WebSocketSession> self)
{
    // Read buffer accumulates partial frames.
    std::vector<std::byte> accumulator;
    constexpr std::size_t  kReadChunkSize = 4096;

    // M-1 + M-2 fix: do_read_loop runs on the raw io_context executor (not the strand_).
    // drain_send_queue() runs on strand_ and also calls stream_.write(). Two concurrent
    // writes to the same socket = UB. Fix: all writes in this loop go through
    // enqueue_priority_frame() which posts to strand_ — never calling stream_.write() here.
    //
    // close_session() only marks the session as closed. The caller is responsible for
    // enqueuing the appropriate close frame via enqueue_priority_frame() BEFORE calling
    // close_session() so that close_sent_ is set before closed_ becomes true.
    auto close_session = [&]() { closed_.store(true, std::memory_order_seq_cst); };

    for (;;) {
        // Read more data.
        auto read_result = co_await stream_.read(kReadChunkSize);
        if (!read_result) {
            // I/O error or EOF — connection already broken, no close frame to send.
            close_session();
            break;
        }

        auto& chunk = *read_result;
        if (chunk.empty()) {
            // EOF — connection already broken.
            close_session();
            break;
        }

        // Append to accumulator.
        accumulator.insert(accumulator.end(), chunk.begin(), chunk.end());

        // Parse all complete frames from the accumulator.
        bool session_ended = false;
        while (!accumulator.empty()) {
            auto parse_result =
                parse_frame_detailed(std::span<const std::byte>{accumulator}, max_payload_,
                                     true /* expect_masked — client frames must be masked */);

            if (!parse_result) {
                if (parse_result.error().kind == ParseFrameErrorKind::Incomplete) {
                    break;
                }

                // M-2 fix: set close_sent_ before enqueuing so the Close opcode handler
                // (if somehow reached after a parse error on the same data) won't send a
                // second frame. Then enqueue the correct close code — no direct write.
                bool expected = false;
                if (close_sent_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                        std::memory_order_acquire))
                {
                    const auto& err = parse_result.error().error;
                    if (err.code() == aevox::WebSocketErrorCode::FrameTooLarge) {
                        enqueue_priority_frame(emit_close_frame(1009, "Message too large"));
                    }
                    else {
                        enqueue_priority_frame(emit_close_frame(1002, "Protocol error"));
                    }
                }
                close_session();
                session_ended = true;
                break;
            }

            const auto&       result   = *parse_result;
            const std::size_t consumed = result.bytes_consumed;

            switch (result.frame.opcode) {
                case Opcode::Text:
                case Opcode::Binary: {
                    // Deliver to on_message callback.
                    const auto&            payload = result.frame.payload;
                    const std::string_view payload_sv{
                        // reinterpret_cast: byte* → char* for string_view construction.
                        // Well-defined: char aliasing is permitted.
                        reinterpret_cast<const char*>(payload.data()), payload.size()};

                    // Create a WebSocket handle for the callback.
                    // We create a temporary handle pointing to the same session.
                    auto ws_handle = self->make_handle(self);
                    handler_.on_message(ws_handle, payload_sv);
                    break;
                }
                case Opcode::Ping:
                    // RFC 6455 §5.5.2: respond with Pong carrying the same payload.
                    // M-1 fix: route through strand via enqueue_priority_frame, not direct write.
                    enqueue_priority_frame(
                        emit_frame(Opcode::Pong, true,
                                   std::span<const std::byte>{result.frame.payload}));
                    break;

                case Opcode::Pong:
                    // Silently discard unsolicited Pong (RFC 6455 §5.5.3).
                    break;

                case Opcode::Close: {
                    // Extract close code from payload (if present).
                    std::uint16_t close_code = 1000;
                    if (result.frame.payload.size() >= 2) {
                        close_code = (static_cast<std::uint16_t>(
                                          static_cast<std::uint8_t>(result.frame.payload[0]))
                                      << 8U) |
                                     static_cast<std::uint16_t>(
                                         static_cast<std::uint8_t>(result.frame.payload[1]));
                    }

                    // Echo the Close frame (RFC 6455 §5.5.1).
                    // M-1 fix: route through strand, not direct write.
                    bool expected = false;
                    if (close_sent_.compare_exchange_strong(expected, true,
                                                            std::memory_order_acq_rel,
                                                            std::memory_order_acquire))
                    {
                        enqueue_priority_frame(emit_close_frame(close_code));
                    }

                    // Fire on_close callback.
                    auto ws_handle = self->make_handle(self);
                    close_session();
                    handler_.on_close(ws_handle, close_code);
                    session_ended = true;
                    break;
                }

                case Opcode::Continuation:
                    // Already rejected by parse_frame — should not reach here.
                    bool expected = false;
                    if (close_sent_.compare_exchange_strong(expected, true,
                                                            std::memory_order_acq_rel,
                                                            std::memory_order_acquire))
                    {
                        enqueue_priority_frame(
                            emit_close_frame(1003, "Continuation frames not supported"));
                    }
                    close_session();
                    session_ended = true;
                    break;
            }

            // Remove consumed bytes from the accumulator.
            accumulator.erase(accumulator.begin(),
                              accumulator.begin() + static_cast<std::ptrdiff_t>(consumed));

            if (session_ended)
                break;
        }

        if (session_ended)
            break;
    }

    // Mark closed (may already be set by close_session() above).
    closed_.store(true, std::memory_order_seq_cst);

    // Resume any coroutine waiting in wait_for_close().
    // M-5 fix: close_waiter_addr_ is atomic. Atomically claim the handle address;
    // if non-null, the connection coroutine is suspended and we resume it.
    // Note: any close frames enqueued via enqueue_priority_frame() were posted to the
    // strand_ BEFORE this point. ff_drain_queue() holds shared_from_this() so the
    // session (and socket) remains alive until the drain completes — even after the
    // connection handler exits following this resume.
    void* addr = close_waiter_addr_.exchange(nullptr, std::memory_order_seq_cst);
    if (addr) {
        std::coroutine_handle<>::from_address(addr).resume();
    }
}

// =============================================================================
// WebSocketSession::ff_read_loop / ff_drain_queue
//
// FireAndForget wrappers that co_await the corresponding Task<void> methods.
// Unlike Task<void>, FireAndForget uses suspend_never for final_suspend, so the
// coroutine frame self-destructs when the body finishes — no owner needed.
// =============================================================================

aevox::detail::FireAndForget WebSocketSession::ff_read_loop(std::shared_ptr<WebSocketSession> self)
{
    co_await do_read_loop(std::move(self));
}

aevox::detail::FireAndForget WebSocketSession::ff_drain_queue(
    std::shared_ptr<WebSocketSession> self)
{
    co_await drain_send_queue(std::move(self));
}

} // namespace aevox::net
