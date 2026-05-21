// src/net/asio_tcp_stream.cpp
//
// Defines TcpStream::Impl (containing asio::ip::tcp::socket),
// ReadAwaitable, WriteAwaitable, all TcpStream out-of-line special members,
// TcpStream::read(), TcpStream::write(), to_string(IoError), and
// TcpStreamFactory::make().
//
// Asio types are confined to this translation unit and asio_tcp_stream.hpp.
// Nothing in include/aevox/ ever includes this file or sees these types.
//
// Design: Tasks/architecture/AEV-003-arch.md §4.1

#include "asio_tcp_stream.hpp"

#include <aevox/async.hpp> // for aevox::detail::tl_post_to_io
#include <aevox/error.hpp>
#include <aevox/task.hpp>
#include <aevox/tcp_stream.hpp>

#include <asio/buffer.hpp>
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// =============================================================================
// TcpStream::Impl — contains the Asio socket (defined here, forward-declared in
// include/aevox/tcp_stream.hpp)
// =============================================================================

class aevox::TcpStream::Impl
{
public:
    // Thread-safety: not thread-safe — used by one connection coroutine.
    // Move semantics: not movable after construction (socket is move-only).
    // Ownership: owned by TcpStream via unique_ptr<Impl>.
    Impl(asio::ip::tcp::socket s, asio::io_context& ctx) : socket_{std::move(s)}, io_ctx_{ctx} {}

    [[nodiscard]] asio::ip::tcp::socket& tcp_socket() noexcept
    {
        return socket_;
    }

    [[nodiscard]] asio::io_context& io_context() noexcept
    {
        return io_ctx_.get();
    }

private:
    asio::ip::tcp::socket                    socket_; // owns the accepted TCP connection
    std::reference_wrapper<asio::io_context> io_ctx_; // lifetime guaranteed by AsioExecutor
};

// =============================================================================
// ReadAwaitable — internal awaitable for TcpStream::read()
//
// Lives on the read() coroutine frame (compiler-allocated). All state is
// stored inline — no additional heap allocation.
//
// Thread-safety: used by a single coroutine; no concurrent access.
// Lifetime: valid for the duration of the read() coroutine suspension.
// =============================================================================

namespace {

class ReadAwaitable
{
public:
    ReadAwaitable(asio::ip::tcp::socket& s, std::size_t max_bytes)
        : socket_{s}, max_bytes_{max_bytes}
    {}

    [[nodiscard]] bool await_ready() const noexcept
    {
        return false; // always suspends to free the I/O thread
    }

    void await_suspend(std::coroutine_handle<> caller)
    {
        assert(aevox::detail::tl_post_to_io() &&
               "TcpStream::read() called outside an executor-managed thread");

        buffer_.resize(max_bytes_);
        auto post_to_io = aevox::detail::tl_post_to_io();

        socket_.get().async_read_some(asio::buffer(buffer_.data(), buffer_.size()),
                                      [this, caller, post_to_io](asio::error_code ec,
                                                                 std::size_t      n) mutable {
                                          if (!ec) {
                                              buffer_.resize(n);
                                          }
                                          else if (ec == asio::error::eof) {
                                              error_ = aevox::IoError::Eof;
                                              buffer_.clear();
                                          }
                                          else if (ec == asio::error::operation_aborted) {
                                              error_ = aevox::IoError::Cancelled;
                                          }
                                          else if (ec == asio::error::connection_reset) {
                                              error_ = aevox::IoError::Reset;
                                          }
                                          else {
                                              error_ = aevox::IoError::Unknown;
                                          }
                                          // Resume the read() coroutine on the I/O pool.
                                          // The post establishes happens-before between the writes
                                          // above and the await_resume() read below (Asio
                                          // completion ordering).
                                          post_to_io([caller]() mutable { caller.resume(); });
                                      });
    }

    [[nodiscard]] std::expected<std::vector<std::byte>, aevox::IoError> await_resume()
    {
        if (error_)
            return std::unexpected{*error_};
        return std::move(buffer_);
    }

private:
    std::reference_wrapper<asio::ip::tcp::socket> socket_; // non-owning; lifetime guaranteed
    std::size_t                                   max_bytes_;

    // Inline storage on the coroutine frame.
    std::vector<std::byte>        buffer_;
    std::optional<aevox::IoError> error_;
};

// =============================================================================
// WriteAwaitable — internal awaitable for TcpStream::write()
//
// Uses asio::async_write (write-all), not async_write_some. The span references
// caller-owned memory; the caller guarantees liveness until the Task completes
// (documented in tcp_stream.hpp).
// =============================================================================

class WriteAwaitable
{
public:
    WriteAwaitable(asio::ip::tcp::socket& s, std::span<const std::byte> data)
        : socket_{s}, data_{data}
    {}

    [[nodiscard]] bool await_ready() const noexcept
    {
        return data_.empty(); // empty write is a no-op — skip suspension
    }

    void await_suspend(std::coroutine_handle<> caller)
    {
        assert(aevox::detail::tl_post_to_io() &&
               "TcpStream::write() called outside an executor-managed thread");

        auto post_to_io = aevox::detail::tl_post_to_io();

        asio::async_write(socket_.get(), asio::buffer(data_.data(), data_.size()),
                          [this, caller, post_to_io](asio::error_code ec, std::size_t) mutable {
                              if (!ec) {
                                  // success — error_ stays empty
                              }
                              else if (ec == asio::error::operation_aborted) {
                                  error_ = aevox::IoError::Cancelled;
                              }
                              else if (ec == asio::error::connection_reset) {
                                  error_ = aevox::IoError::Reset;
                              }
                              else {
                                  error_ = aevox::IoError::Unknown;
                              }
                              post_to_io([caller]() mutable { caller.resume(); });
                          });
    }

    [[nodiscard]] std::expected<void, aevox::IoError> await_resume()
    {
        if (error_)
            return std::unexpected{*error_};
        return {};
    }

private:
    std::reference_wrapper<asio::ip::tcp::socket> socket_; // non-owning; lifetime guaranteed
    std::span<const std::byte>                    data_;

    // Inline storage on the coroutine frame.
    std::optional<aevox::IoError> error_;
};

} // anonymous namespace

// =============================================================================
// TcpStream — out-of-line special members
// (Defined here because Impl is an incomplete type in tcp_stream.hpp)
// =============================================================================

namespace aevox {

TcpStream::TcpStream(std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)} {}

TcpStream::TcpStream(TcpStream&&) noexcept            = default;
TcpStream& TcpStream::operator=(TcpStream&&) noexcept = default;
TcpStream::~TcpStream()                               = default;

bool TcpStream::valid() const noexcept
{
    return impl_ != nullptr;
}

// =============================================================================
// TcpStream::read() — coroutine; suspends via ReadAwaitable
// =============================================================================

Task<std::expected<std::vector<std::byte>, IoError>> TcpStream::read(std::size_t max_bytes)
{
    co_return co_await ReadAwaitable{impl_->tcp_socket(), max_bytes};
}

// =============================================================================
// TcpStream::write() — coroutine; suspends via WriteAwaitable
// =============================================================================

Task<std::expected<void, IoError>> TcpStream::write(std::span<const std::byte> data)
{
    co_return co_await WriteAwaitable{impl_->tcp_socket(), data};
}

// =============================================================================
// to_string(IoError)
// =============================================================================

[[nodiscard]] std::string_view to_string(IoError e) noexcept
{
    switch (e) {
        case IoError::Eof:
            return "Eof: remote peer closed the connection";
        case IoError::Cancelled:
            return "Cancelled: executor is shutting down";
        case IoError::Reset:
            return "Reset: connection reset by peer";
        case IoError::Timeout:
            return "Timeout: I/O operation timed out";
        case IoError::Unknown:
            return "Unknown: OS-level I/O error";
    }
    return "IoError: unrecognised value";
}

ErrorCategory category(IoError e) noexcept
{
    switch (e) {
        case IoError::Eof:
        case IoError::Reset:
        case IoError::Timeout:
            return ErrorCategory::Io;
        case IoError::Cancelled:
            return ErrorCategory::State;
        case IoError::Unknown:
            return ErrorCategory::Unknown;
    }
    return ErrorCategory::Unknown;
}

} // namespace aevox

// =============================================================================
// TcpStreamFactory::make() — factory
// =============================================================================

namespace aevox::net {

aevox::TcpStream TcpStreamFactory::make(asio::ip::tcp::socket socket, asio::io_context& io_ctx)
{
    return aevox::TcpStream{std::make_unique<aevox::TcpStream::Impl>(std::move(socket), io_ctx)};
}

} // namespace aevox::net

// =============================================================================
// get_tcp_stream_impl — internal accessor (friend of TcpStream, see tcp_stream.hpp)
// =============================================================================

namespace aevox {

/// Returns mutable Impl reference for internal net/ code (WebSocketSession).
/// Defined here because TcpStream::Impl is complete in this TU.
/// Friend declared in tcp_stream.hpp.
TcpStream::Impl& get_tcp_stream_impl(TcpStream& stream) noexcept
{
    return *stream.impl_;
}

} // namespace aevox

namespace aevox::net {

/// Returns the io_context executor from a TcpStream.
/// Defined here where TcpStream::Impl is complete.
/// Declared in asio_tcp_stream.hpp.
asio::io_context::executor_type get_tcp_stream_executor(aevox::TcpStream& stream) noexcept
{
    return get_tcp_stream_impl(stream).io_context().get_executor();
}

} // namespace aevox::net
