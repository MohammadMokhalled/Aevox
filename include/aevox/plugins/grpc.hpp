#pragma once
// include/aevox/plugins/grpc.hpp
//
// Public API for the optional official gRPC plugin. This header intentionally
// exposes no nghttp2, protobuf, gRPC C++, or Asio types.

#include <aevox/error.hpp>
#include <aevox/plugin.hpp>
#include <aevox/task.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aevox::grpc {

/**
 * @brief gRPC wire status code.
 *
 * Values match the public gRPC status-code registry. They are serialized as
 * `grpc-status` trailer values by the plugin.
 *
 * @note Thread-safety: enum values are immutable and safe to copy between threads.
 * @note Ownership: no dynamic storage.
 */
enum class StatusCode : std::uint8_t
{
    Ok                 = 0,
    Cancelled          = 1,
    Unknown            = 2,
    InvalidArgument    = 3,
    DeadlineExceeded   = 4,
    NotFound           = 5,
    AlreadyExists      = 6,
    PermissionDenied   = 7,
    ResourceExhausted  = 8,
    FailedPrecondition = 9,
    Aborted            = 10,
    OutOfRange         = 11,
    Unimplemented      = 12,
    Internal           = 13,
    Unavailable        = 14,
    DataLoss           = 15,
    Unauthenticated    = 16,
};

/**
 * @brief Error code for gRPC plugin configuration and transport failures.
 *
 * These errors describe plugin setup and server-side transport problems. Handler
 * application failures should usually be returned as `GrpcStatus` instead so
 * they can be serialized to the client as gRPC status trailers.
 *
 * @note Thread-safety: enum values are immutable and safe to copy between threads.
 * @note Ownership: no dynamic storage.
 */
enum class GrpcError : std::uint8_t
{
    InvalidConfig,
    InvalidMethod,
    DuplicateMethod,
    AlreadyInstalled,
    AlreadyRunning,
    ListenFailed,
    StartFailed,
    ProtocolError,
    MessageTooLarge,
    CompressionUnsupported,
    Unknown,
};

/**
 * @brief Returns a stable text label for a GrpcError.
 *
 * @param error  Error code to describe.
 * @return Static string literal with process lifetime.
 * @note Thread-safety: safe to call concurrently.
 */
[[nodiscard]] std::string_view to_string(GrpcError error) noexcept;

/**
 * @brief Maps a GrpcError to the shared Aevox error category taxonomy.
 *
 * @param error  Error code to classify.
 * @return Broad error category used by logs, docs, and generic error handling.
 * @note Thread-safety: safe to call concurrently.
 */
[[nodiscard]] ErrorCategory category(GrpcError error) noexcept;

/**
 * @brief Metadata entry carried on a gRPC request or response.
 *
 * Header names are stored in lowercase ASCII form for incoming metadata. Binary
 * metadata with `-bin` suffix is transported as the base64 text value received
 * on the HTTP/2 wire in v0.3.
 *
 * @note Thread-safety: independent values are safe to move between threads.
 * @note Ownership: owns both name and value strings.
 */
struct Metadata
{
    std::string name{};
    std::string value{};
};

/**
 * @brief Application-level gRPC status returned by unary handlers.
 *
 * A non-OK status is serialized as gRPC trailers. `message` becomes the
 * `grpc-message` trailer after percent-encoding of invalid header characters.
 *
 * @note Thread-safety: independent values are safe to move between threads.
 * @note Ownership: owns the status message string.
 */
struct GrpcStatus
{
    StatusCode  code{StatusCode::Ok};
    std::string message{};

    /**
     * @brief Creates an OK gRPC status.
     *
     * @return Status with `StatusCode::Ok` and an empty message.
     * @note Thread-safety: returns an independent value.
     */
    [[nodiscard]] static GrpcStatus ok();

    /**
     * @brief Creates a non-OK gRPC status.
     *
     * @param code     gRPC status code. `StatusCode::Ok` is accepted but discouraged.
     * @param message  Human-readable status message copied into the returned value.
     * @return Status value that can be returned from a handler as an error.
     * @throws `std::bad_alloc` if message storage allocation fails.
     * @note Thread-safety: returns an independent value.
     */
    [[nodiscard]] static GrpcStatus error(StatusCode code, std::string_view message);
};

/**
 * @brief Default cleartext h2c gRPC listen port.
 *
 * @note Thread-safety: immutable compile-time constant.
 */
inline constexpr std::uint16_t kDefaultGrpcPort{50051};

/**
 * @brief Default maximum number of concurrent HTTP/2 streams per gRPC connection.
 *
 * @note Thread-safety: immutable compile-time constant.
 */
inline constexpr std::uint16_t kDefaultMaxConcurrentStreams{128};

/**
 * @brief Default maximum unary request or response payload size.
 *
 * The value is four MiB, excluding the five-byte gRPC message envelope.
 *
 * @note Thread-safety: immutable compile-time constant.
 */
inline constexpr std::size_t kDefaultMaxMessageSize{4UZ * 1024UZ * 1024UZ};

/**
 * @brief Default drain timeout used when stopping the gRPC plugin executor.
 *
 * The value is five seconds.
 *
 * @note Thread-safety: immutable compile-time constant.
 */
inline constexpr std::chrono::milliseconds kDefaultDrainTimeout{std::chrono::seconds{5}};

/**
 * @brief Configuration for the official gRPC plugin.
 *
 * The plugin listens on a separate cleartext h2c port from the HTTP/1.1 App
 * listener. `worker_threads == 0` resolves to the App executor default thread
 * count used by `ExecutorConfig`.
 *
 * @note Thread-safety: read during plugin construction/install only.
 * @note Ownership: owns no dynamic storage.
 */
struct PluginConfig
{
    std::uint16_t             port{kDefaultGrpcPort};
    std::uint16_t             max_concurrent_streams{kDefaultMaxConcurrentStreams};
    std::size_t               max_message_size{kDefaultMaxMessageSize};
    std::uint16_t             worker_threads{0};
    std::chrono::milliseconds drain_timeout{kDefaultDrainTimeout};
};

/**
 * @brief Unary gRPC request delivered to an application handler.
 *
 * The request owns the decoded gRPC message payload without the five-byte gRPC
 * message envelope. Applications that use protobuf generated types parse
 * `payload()` themselves in application code.
 *
 * @note Thread-safety: a request is owned by one handler coroutine and must not
 *       be mutated concurrently.
 * @note Move semantics: movable. A moved-from request is valid but unspecified
 *       except that destruction is safe.
 * @note Ownership: owns method, payload, and metadata storage.
 */
class UnaryRequest
{
public:
    /**
     * @brief Constructs a unary request value.
     *
     * @param method    Fully-qualified gRPC method path, e.g. `/pkg.Service/Get`.
     * @param payload   Decoded request message bytes without gRPC envelope.
     * @param metadata  Application metadata excluding pseudo-headers and reserved
     *                  gRPC transport headers.
     * @throws `std::bad_alloc` if storage allocation fails while moving values.
     */
    UnaryRequest(std::string method, std::vector<std::byte> payload,
                 std::vector<Metadata> metadata = {});

    /**
     * @brief Returns the fully-qualified gRPC method path.
     *
     * @return Method path with process-independent storage owned by this request.
     * @note Thread-safety: safe while the request is not being mutated.
     */
    [[nodiscard]] std::string_view method() const noexcept;

    /**
     * @brief Returns the decoded request message bytes.
     *
     * @return Byte span excluding the gRPC compression/length envelope.
     * @note Thread-safety: safe while the request is not being mutated.
     */
    [[nodiscard]] std::span<const std::byte> payload() const noexcept;

    /**
     * @brief Returns application metadata attached to this request.
     *
     * @return Read-only span of metadata entries.
     * @note Thread-safety: safe while the request is not being mutated.
     */
    [[nodiscard]] std::span<const Metadata> metadata() const noexcept;

private:
    std::string            method_;
    std::vector<std::byte> payload_;
    std::vector<Metadata>  metadata_;
};

/**
 * @brief Unary gRPC response returned by an application handler.
 *
 * The response payload is serialized as one uncompressed gRPC message. To return
 * a non-OK status, return `std::unexpected(GrpcStatus::error(...))` from the
 * handler instead of constructing `UnaryResponse`.
 *
 * @note Thread-safety: independent values are safe to move between threads.
 * @note Move semantics: movable. A moved-from response is valid but unspecified
 *       except that destruction is safe.
 * @note Ownership: owns payload and trailing metadata storage.
 */
struct UnaryResponse
{
    std::vector<std::byte> payload{};
    std::vector<Metadata>  trailing_metadata{};
};

/**
 * @brief Handler type for unary gRPC methods.
 *
 * The handler receives one decoded request message and returns either one
 * response message or a gRPC application status. Transport and framing failures
 * are handled by the plugin before the handler is invoked.
 *
 * @note Thread-safety: each handler invocation receives a distinct request.
 *       Handler callables must provide their own synchronization for shared
 *       captured state.
 */
using UnaryHandler =
    std::move_only_function<Task<std::expected<UnaryResponse, GrpcStatus>>(UnaryRequest&)>;

/**
 * @brief Official Aevox gRPC server plugin.
 *
 * `Plugin` owns unary method registrations and a plugin-local executor. It is
 * installed into an `aevox::App` via `App::install(std::unique_ptr<aevox::Plugin>)`.
 * Network I/O starts when the App starts listening and stops with the App.
 *
 * @note Thread-safety: registration is not thread-safe and must happen before
 *       installation. `stop()` is thread-safe through the base `aevox::Plugin`
 *       interface.
 * @note Move semantics: movable before installation. A moved-from plugin is valid
 *       only for destruction.
 * @note Ownership: after successful `App::install()`, the App owns the plugin.
 */
class Plugin final : public aevox::Plugin
{
public:
    /**
     * @brief Constructs a gRPC plugin with the supplied configuration.
     *
     * Construction stores the config only. Port binding and worker creation happen
     * in `start()`.
     *
     * @param config  Plugin configuration.
     * @throws `std::bad_alloc` if internal state allocation fails.
     */
    explicit Plugin(PluginConfig config = {});

    /**
     * @brief Destructs the plugin and requests shutdown if still running.
     *
     * @note Does not throw.
     */
    ~Plugin() override;

    /**
     * @brief Move-constructs a gRPC plugin before installation.
     *
     * @param other  Plugin whose registrations and configuration are transferred.
     * @note Thread-safety: not thread-safe. Move only before publishing to `App`.
     * @note Move semantics: `other` remains valid only for destruction.
     * @note Ownership: transfers all plugin-owned state to the new object.
     */
    Plugin(Plugin&& other) noexcept;

    /**
     * @brief Move-assigns a gRPC plugin before installation.
     *
     * Stops this plugin if needed, then transfers registrations and configuration
     * from `other`.
     *
     * @param other  Plugin whose state is transferred.
     * @return Reference to this plugin.
     * @note Thread-safety: not thread-safe. Move only before publishing to `App`.
     * @note Move semantics: `other` remains valid only for destruction.
     * @note Ownership: replaces this plugin's owned state.
     */
    Plugin& operator=(Plugin&& other) noexcept;

    /**
     * @brief Copy construction is disabled.
     *
     * @param other  Source plugin, intentionally unsupported.
     * @note Thread-safety: not applicable because the operation is deleted.
     */
    Plugin(const Plugin&) = delete;

    /**
     * @brief Copy assignment is disabled.
     *
     * @param other  Source plugin, intentionally unsupported.
     * @return This plugin, intentionally unavailable.
     * @note Thread-safety: not applicable because the operation is deleted.
     */
    Plugin& operator=(const Plugin&) = delete;

    /**
     * @brief Registers a unary gRPC method.
     *
     * `method` must be an absolute gRPC method path of the form
     * `/package.Service/Method`. Registration must happen before the plugin is
     * installed on an App.
     *
     * @param method   Fully-qualified gRPC method path.
     * @param handler  Handler callable stored by move.
     * @return `std::expected<void, GrpcError>`:
     *         - success when the method was registered.
     *         - `GrpcError::InvalidMethod` if the path is empty or malformed.
     *         - `GrpcError::DuplicateMethod` if already registered.
     *         - `GrpcError::AlreadyInstalled` if called after installation.
     * @note Thread-safety: not thread-safe. Call from the main thread before
     *       `App::install()`.
     */
    [[nodiscard]] std::expected<void, GrpcError> add_unary_method(std::string_view method,
                                                                  UnaryHandler handler) noexcept;

    /**
     * @brief Returns the stable plugin name.
     *
     * @return Static string literal `"aevox.grpc"`.
     * @note Thread-safety: safe to call concurrently.
     */
    [[nodiscard]] std::string_view name() const noexcept override;

    /**
     * @brief Validates configuration and attaches the plugin to an App.
     *
     * @param app  App that will own and start this plugin.
     * @return `std::expected<void, PluginError>`:
     *         - success when installation state is recorded.
     *         - `PluginError::InvalidArgument` for invalid config or moved-from plugin state.
     *         - `PluginError::AlreadyInstalled` if called more than once.
     * @note Thread-safety: not thread-safe. Called by `App::install()` before `listen()`.
     */
    [[nodiscard]] std::expected<void, PluginError> install(aevox::App& app) noexcept override;

    /**
     * @brief Starts the gRPC h2c listener and worker loop.
     *
     * @return `std::expected<void, PluginError>`:
     *         - success after bind/listen succeeds and the worker is launched.
     *         - `PluginError::InvalidArgument` for moved-from plugin state.
     *         - `PluginError::AlreadyRunning` if already started.
     *         - `PluginError::StartFailed` if bind/listen fails.
     * @note Thread-safety: not thread-safe. Called by `App::listen()`.
     */
    [[nodiscard]] std::expected<void, PluginError> start() noexcept override;

    /**
     * @brief Requests gRPC listener shutdown and joins the worker thread.
     *
     * The operation is idempotent and safe after a partial startup failure.
     *
     * @note Thread-safety: safe to call concurrently with the plugin worker.
     */
    void stop() noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Creates an owning official gRPC plugin pointer for `App::install()`.
 *
 * @param config  Plugin configuration.
 * @return Owning pointer to an `aevox::Plugin` implementation.
 * @throws `std::bad_alloc` if allocation fails.
 * @note Thread-safety: returns an independent plugin object.
 */
[[nodiscard]] std::unique_ptr<aevox::Plugin> make_plugin(PluginConfig config = {});

} // namespace aevox::grpc
