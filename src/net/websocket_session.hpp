#pragma once
// src/net/websocket_session.hpp
//
// INTERNAL — never included by public headers or application code.
//
// WebSocketSession: Asio-backed WebSocket connection session.
// This is the concrete type hidden behind WebSocket::Impl (PIMPL).
//
// Lifetime:
//   WebSocketSession is managed as shared_ptr<WebSocketSession>.
//   WebSocket::Impl holds a shared_ptr; TopicBus holds weak_ptrs.
//
// Thread-safety:
//   The read loop runs on the raw I/O executor.
//   send_raw() / send_text() / send_binary() post to the strand — safe from any thread.
//   close_session() is idempotent and strand-safe.
//
// Design: Tasks/architecture/AEV-010-arch.md §4.4

#include <aevox/async.hpp> // FireAndForget — fire-and-forget coroutine driver
#include <aevox/task.hpp>
#include <aevox/tcp_stream.hpp>
#include <aevox/websocket.hpp>
#include <aevox/websocket_handler.hpp>

#include <asio.hpp>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "net/topic_bus.hpp"
#include "net/websocket_frame.hpp"

namespace aevox::net {

// Maximum number of pending frames in the send queue before back-pressure kicks in.
inline constexpr std::size_t kMaxSendQueueDepth = 128;

// =============================================================================
// WebSocketSession
// =============================================================================

class WebSocketSession : public TopicSubscriber,
                         public std::enable_shared_from_this<WebSocketSession>
{
public:
    /**
     * @brief Factory — constructs a fully initialized session.
     *
     * Called by Request::upgrade_websocket() after the 101 response is sent.
     * The session takes ownership of the TcpStream.
     *
     * @param stream        Moved-from TcpStream (already upgraded).
     * @param bus           App-owned TopicBus (non-owning pointer).
     * @param handler       WebSocket lifecycle callbacks.
     * @param max_payload   Maximum payload bytes (from AppConfig::max_body_size).
     * @param remote_addr   Remote IP string (captured before upgrade).
     * @param initial_topic Initial topic (first path param value, or empty).
     */
    [[nodiscard]] static std::shared_ptr<WebSocketSession> create(
        aevox::TcpStream stream, std::optional<std::reference_wrapper<TopicBus>> bus,
        aevox::WebSocketHandler handler, std::size_t max_payload, std::string remote_addr,
        std::string initial_topic);

    // Not copyable or movable — shared_ptr manages lifetime.
    WebSocketSession(const WebSocketSession&)            = delete;
    WebSocketSession& operator=(const WebSocketSession&) = delete;
    WebSocketSession(WebSocketSession&&)                 = delete;
    WebSocketSession& operator=(WebSocketSession&&)      = delete;

    ~WebSocketSession() override;

    // -------------------------------------------------------------------------
    // Public API called via WebSocket (PIMPL delegation)
    // -------------------------------------------------------------------------

    /// Send a text frame. Eagerly builds the frame and posts it to the strand.
    /// Returns an error immediately if the session is closed.
    /// This is a regular (non-coroutine) function so it works from sync callbacks.
    [[nodiscard]] std::expected<void, aevox::WebSocketError> send_text(std::string_view message);

    /// Send a binary frame. Eagerly builds the frame and posts it to the strand.
    /// Returns an error immediately if the session is closed.
    [[nodiscard]] std::expected<void, aevox::WebSocketError> send_binary(
        std::span<const std::byte> data);

    /// Initiate a graceful close handshake.
    [[nodiscard]] aevox::Task<std::expected<void, aevox::WebSocketError>> close_graceful(
        std::uint16_t code, std::string_view reason);

    /// Subscribe this session to a topic (thread-safe via TopicBus).
    void subscribe(std::string_view topic);

    /// Publish to a topic via the TopicBus (suppresses self-publish).
    void publish(std::string_view topic, std::string_view message);

    /// Returns the current topic (first subscribed topic).
    [[nodiscard]] std::string_view current_topic() const noexcept;

    /// Returns the remote IP address string.
    [[nodiscard]] std::string_view remote_address() const noexcept;

    /// Returns true if the session has been closed (atomically checked).
    [[nodiscard]] bool is_closed() const noexcept;

    // -------------------------------------------------------------------------
    // Internal: called by TopicBus publish path (from any thread).
    // -------------------------------------------------------------------------

    /// Enqueue a server-to-client text frame from a bus publish.
    /// Thread-safe: posts to the session strand.
    void send_from_bus(std::string_view message) override;

    // -------------------------------------------------------------------------
    // Internal: called by App::Impl WebSocket dispatch.
    // -------------------------------------------------------------------------

    /// Starts the read loop coroutine (detached on the connection strand).
    /// Must be called exactly once, after construction and after on_open fires.
    void start_read_loop(std::shared_ptr<WebSocketSession> self_ptr);

    /// Returns a Task<void> that completes when the session closes.
    /// Used by the connection handler to park the connection coroutine.
    [[nodiscard]] aevox::Task<void> wait_for_close();

    // -------------------------------------------------------------------------
    // make_handle() — factory for WebSocket public handle
    // -------------------------------------------------------------------------

    /// Creates a WebSocket public handle wrapping this session.
    [[nodiscard]] aevox::WebSocket make_handle(std::shared_ptr<WebSocketSession> self_ptr);

    // Private tag struct enabling std::make_shared<WebSocketSession>(...) from create().
    // std::make_shared requires a public (or friend-accessible) constructor.
    // Since this TU is the only caller, the tag is defined privately here.
    struct PrivateTag
    {
        explicit PrivateTag() = default;
    };

public:
    // make_shared-compatible constructor — public to satisfy allocator requirements
    // but effectively private via the PrivateTag sentinel (only create() can supply it).
    WebSocketSession(PrivateTag, aevox::TcpStream stream,
                     asio::strand<asio::io_context::executor_type>   strand,
                     std::optional<std::reference_wrapper<TopicBus>> bus,
                     aevox::WebSocketHandler handler, std::size_t max_payload,
                     std::string remote_addr, std::string initial_topic);

private:
    // -------------------------------------------------------------------------
    // Internal send implementation
    // -------------------------------------------------------------------------

    /// Enqueue a pre-built frame for sending. Thread-safe (posts to strand).
    void enqueue_frame(std::vector<std::byte> frame);

    /// Enqueue a control frame with head-of-queue priority (push_front, no depth limit).
    /// Thread-safe: posts to strand. Used for Close, Pong, and error close frames.
    void enqueue_priority_frame(std::vector<std::byte> frame);

    /// Drain the send queue — runs on the strand.
    [[nodiscard]] aevox::Task<void> drain_send_queue(std::shared_ptr<WebSocketSession> self);

    // -------------------------------------------------------------------------
    // Read loop
    // -------------------------------------------------------------------------

    [[nodiscard]] aevox::Task<void> do_read_loop(std::shared_ptr<WebSocketSession> self);

    // Fire-and-forget wrappers: start the read loop / drain queue as self-managing
    // coroutines. Task<void> destroys its frame on scope exit, which kills a suspended
    // coroutine. FireAndForget uses suspend_never for final_suspend, so the frame
    // self-destructs when done — safe to start and discard the return value.
    aevox::detail::FireAndForget ff_read_loop(std::shared_ptr<WebSocketSession> self);
    aevox::detail::FireAndForget ff_drain_queue(std::shared_ptr<WebSocketSession> self);

    // -------------------------------------------------------------------------
    // Member state
    // -------------------------------------------------------------------------

    aevox::TcpStream                                stream_;
    std::optional<std::reference_wrapper<TopicBus>> bus_; // non-owning, App-owned
    aevox::WebSocketHandler                         handler_;
    std::size_t                                     max_payload_;
    std::string                                     remote_addr_;
    std::string                                     current_topic_;

    // Strand for serializing sends (and ensuring read-loop / send don't interleave).
    // The executor (io_context) is retrieved from the TcpStream socket via a stored pointer.
    // We use an asio::strand wrapping an executor obtained from the socket.
    // Because TcpStream::Impl holds the socket, we store the strand here.
    // The strand is initialized in create() after the TcpStream is moved in.
    asio::strand<asio::io_context::executor_type> strand_;

    // Send queue: frames waiting to be sent.
    std::deque<std::vector<std::byte>> send_queue_;
    bool                               sending_{false}; // true if drain_send_queue is running

    // Session state.
    std::atomic<bool> closed_{false};
    std::atomic<bool> close_sent_{false}; // true after we sent a Close frame

    // Wait-for-close: atomic handle address registered by wait_for_close().
    // Written by await_suspend (connection coroutine), read by do_read_loop (io_context thread).
    // std::atomic<void*> satisfies the C++ memory model — avoids data race on close_waiter_.
    std::atomic<void*> close_waiter_addr_{nullptr};
};

} // namespace aevox::net

// =============================================================================
// WebSocket::Impl — defined here so websocket_session.hpp includes provide the
// complete type. src/net/websocket_session.cpp implements the out-of-line
// WebSocket members.
// =============================================================================

struct aevox::WebSocket::Impl
{
    std::shared_ptr<aevox::net::WebSocketSession> session;
};

namespace aevox {

std::optional<std::reference_wrapper<WebSocket::Impl>> get_websocket_impl(WebSocket& ws) noexcept;

} // namespace aevox
