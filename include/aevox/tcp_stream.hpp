#pragma once
// include/aevox/tcp_stream.hpp
//
// Public TCP stream type for Aevox.
// Provides co_await read() and co_await write() over an accepted TCP connection.
//
// No Asio types appear in this file. The concrete implementation (AsioTcpStream)
// lives in src/net/ and is held via a pimpl pointer.
//
// Buffer ownership contract:
//   read() returns an owned std::vector<std::byte>. The caller must keep that
//   vector alive for as long as any std::string_view or std::span derived from
//   its contents remains in scope (e.g. the lifetime of a ParsedRequest).
//
// Design: Tasks/architecture/AEV-003-arch.md §3.1

#include <aevox/config.hpp>
#include <aevox/task.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

// Forward declaration — allows friend class declaration below without
// including any Asio header. aevox::net::AsioTcpStream is defined in src/net/.
namespace aevox::net {
class AsioTcpStream;
} // namespace aevox::net

namespace aevox {

// =============================================================================
// IoError
// =============================================================================

/**
 * @brief Error codes for TcpStream I/O operations.
 *
 * Returned via `std::expected`. Never thrown. All values map to OS-level
 * error conditions that can occur on any TCP socket.
 */
enum class IoError : std::uint8_t
{
    Eof,       ///< The remote peer closed the connection cleanly (read returned 0).
    Cancelled, ///< The executor is shutting down; I/O was cancelled (operation_aborted).
    Reset,     ///< The connection was reset by the peer (RST / connection_reset).
    Timeout,   ///< A read or write did not complete within its deadline.
    Unknown,   ///< Any other OS-level I/O error not covered by the above.
};

/**
 * @brief Returns a human-readable description of an IoError value.
 *
 * @param e  The error to describe.
 * @return   A null-terminated static string literal describing the error.
 *           The returned view's lifetime is static — it never dangles.
 */
[[nodiscard]] std::string_view to_string(IoError e) noexcept;

// =============================================================================
// TcpStream
// =============================================================================

/**
 * @brief Owns one accepted TCP connection and exposes coroutine I/O operations.
 *
 * `TcpStream` is the public bridge between the `Executor` accept loop and the
 * HTTP parser layer. Application handlers and the HTTP parser interact
 * only with this type — they never see `asio::ip::tcp::socket` or any OS socket
 * primitive.
 *
 * The underlying socket is hidden via a pimpl pointer. Replacing the Asio backend
 * (ADR-1: `std::net` in C++29) requires only changing `src/net/asio_tcp_stream.cpp`.
 *
 * **Read ownership contract:**
 * `read()` returns an owned `std::vector<std::byte>`. Any `std::string_view` or
 * `std::span` derived from the returned buffer is valid only while that
 * `std::vector<std::byte>` is alive. The `ParsedRequest` struct stores
 * views into the caller-owned buffer — the caller is responsible for keeping the
 * buffer alive for the duration of any `ParsedRequest` derived from it.
 *
 * **Write ownership contract:**
 * `write()` accepts a non-owning `std::span<const std::byte>`. The caller must
 * keep the underlying data alive until the returned `Task` completes.
 *
 * **Thread-safety:**
 * A `TcpStream` must not be used concurrently from multiple coroutines. Concurrent
 * reads or concurrent writes produce undefined behaviour. For v0.1 the HTTP
 * connection loop is strictly sequential: read one request, then write one response.
 *
 * **Move semantics:**
 * `TcpStream` is move-only. A moved-from `TcpStream` has `valid() == false`.
 * Calling any I/O method on a moved-from stream is undefined behaviour.
 *
 * **Ownership:**
 * `TcpStream` owns the underlying socket resource via its pimpl pointer. When
 * `TcpStream` is destroyed, the socket is closed immediately (the connection
 * coroutine must send any response before destroying the stream).
 *
 * @note Instances are created exclusively by `AsioExecutor::run_accept_loop()`
 *       and passed to connection handlers. The constructor is private.
 */
class TcpStream
{
public:
    // Move-only — underlying socket is a unique resource.
    TcpStream(TcpStream&&) noexcept;
    TcpStream& operator=(TcpStream&&) noexcept;

    TcpStream(const TcpStream&)            = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    // Destructor declared here, defined in asio_tcp_stream.cpp (Impl is incomplete here).
    ~TcpStream();

    /**
     * @brief Returns true if this stream holds a live socket.
     *
     * Returns false after the stream has been moved from. Calling `read()` or
     * `write()` on a stream where `valid() == false` is undefined behaviour.
     *
     * @return `true` if the underlying pimpl is non-null.
     */
    [[nodiscard]] bool valid() const noexcept;

    /**
     * @brief Reads up to `max_bytes` bytes from the socket into an owned buffer.
     *
     * Suspends the calling coroutine until at least 1 byte is available or an
     * error occurs. The socket is not blocked — the I/O thread processes other
     * work while waiting.
     *
     * The returned vector holds between 1 and `max_bytes` bytes on success.
     * There is no guarantee that exactly `max_bytes` are returned in one call.
     *
     * The caller must keep the returned vector alive for as long as any
     * `std::string_view` or `std::span` derived from its contents is in scope.
     *
     * @param max_bytes  Maximum number of bytes to read in one call. Must be > 0.
     *                   Default: kDefaultMaxReadBytes (65536 bytes = 64 KiB), which
     *                   matches a typical TCP receive window. Override via
     *                   AppConfig::max_read_bytes to tune per-connection memory vs.
     *                   syscall frequency.
     * @return  On success: `std::vector<std::byte>` with 1..max_bytes bytes.
     *          On `IoError::Eof`: peer closed the connection; vector is empty.
     *          On other `IoError`: an OS-level error occurred.
     *
     * @note Valid only on executor-managed I/O threads.
     * @note `[[nodiscard]]` — discarding the result silently drops received data.
     */
    [[nodiscard]] Task<std::expected<std::vector<std::byte>, IoError>> read(
        std::size_t max_bytes = kDefaultMaxReadBytes);

    /**
     * @brief Writes all bytes in `data` to the socket.
     *
     * Suspends the calling coroutine until all bytes in `data` are sent or an
     * error occurs. This is a write-all operation — the implementation loops
     * until the full span is written or a fatal error is encountered.
     *
     * The caller must keep the data referenced by `data` alive until this
     * `Task` completes (i.e. until `co_await stream.write(data)` returns).
     *
     * @param data  Non-owning view of the bytes to send. Empty span is a no-op
     *              (returns success immediately without suspension).
     * @return  `std::expected<void, IoError>`:
     *          - Empty (success) when all bytes are sent.
     *          - `IoError::Reset` if the peer reset the connection mid-write.
     *          - `IoError::Cancelled` if the executor is draining.
     *          - `IoError::Unknown` for other OS errors.
     *
     * @note Valid only on executor-managed I/O threads.
     * @note `[[nodiscard]]` — unchecked write errors leave the connection in an
     *       unknown state.
     */
    [[nodiscard]] Task<std::expected<void, IoError>> write(std::span<const std::byte> data);

private:
    // Pimpl: hides asio::ip::tcp::socket and io_context reference.
    // Defined only in src/net/asio_tcp_stream.cpp — Asio types never leak here.
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Only AsioTcpStream (in src/net/) may call the private constructor.
    friend class aevox::net::AsioTcpStream;

    // Private constructor: called exclusively by AsioTcpStream::make().
    explicit TcpStream(std::unique_ptr<Impl> impl) noexcept;
};

} // namespace aevox
