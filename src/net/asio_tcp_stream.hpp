#pragma once
// src/net/asio_tcp_stream.hpp
//
// INTERNAL — never included by public headers or application code.
//
// Declares AsioTcpStream, the factory helper that constructs aevox::TcpStream
// from an accepted asio::ip::tcp::socket. The friend declaration in
// include/aevox/tcp_stream.hpp names this class so it can call the private
// TcpStream(unique_ptr<Impl>) constructor.
//
// Design: Tasks/architecture/AEV-003-arch.md §4.1

#include <aevox/tcp_stream.hpp>

#include <asio.hpp>

namespace aevox::net {

/**
 * Factory helper for TcpStream.
 *
 * Constructs TcpStream::Impl (which contains asio::ip::tcp::socket) and
 * passes the result to TcpStream's private constructor via the friend
 * declaration in tcp_stream.hpp.
 *
 * Thread-safety: make() is called only from run_accept_loop() on the I/O
 *   thread — no concurrent calls expected.
 * Move semantics: AsioTcpStream itself is stateless; all state is in the
 *   returned TcpStream.
 * Ownership: returned TcpStream owns the socket via unique_ptr<Impl>.
 */
class AsioTcpStream
{
public:
    /**
     * @brief Constructs a TcpStream that owns the given socket.
     *
     * @param socket  Accepted socket, moved into TcpStream::Impl.
     * @param io_ctx  The executor's io_context (non-owning reference stored
     *                in Impl for use by ReadAwaitable / WriteAwaitable).
     * @return        A fully constructed TcpStream with valid() == true.
     */
    [[nodiscard]] static aevox::TcpStream make(asio::ip::tcp::socket socket,
                                               asio::io_context&     io_ctx);
};

} // namespace aevox::net
