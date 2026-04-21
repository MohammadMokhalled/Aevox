#pragma once
// include/aevox/config.hpp
//
// Public configuration constants and error types for Aevox.
//
// Defines the named constexpr defaults that back every field in AppConfig and
// ExecutorConfig, and the ConfigError/ConfigErrorDetail types used by App::create().
//
// No Asio, TOML, or other third-party types appear in this file.
//
// Thread-safety: all symbols are constexpr or stateless enum values — inherently
// thread-safe.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace aevox {

// =============================================================================
// AppConfig default constants
// =============================================================================

/** @brief Default TCP port. Value: 8080. */
inline constexpr std::uint16_t kDefaultPort{8080};

/**
 * @brief Default bind address. Value: `"0.0.0.0"` (all interfaces).
 *
 * Pass `"127.0.0.1"` to restrict to loopback. IPv6 is not supported in v0.1.
 */
inline constexpr std::string_view kDefaultHost{"0.0.0.0"};

/** @brief Default TCP listen backlog depth. Value: 1024. */
inline constexpr int kDefaultBacklog{1024};

/**
 * @brief Default maximum allowed request body size in bytes. Value: 10 MiB (10,485,760 bytes).
 *
 * Requests whose body exceeds this limit are rejected with HTTP 413.
 *
 * @note Valid range: 1 byte to 2 GiB (2,147,483,648 bytes). Values outside
 *       this range are rejected by the config loader with ConfigError::invalid_value.
 */
inline constexpr std::size_t kDefaultMaxBodySize{10UZ * 1024UZ * 1024UZ};

/**
 * @brief Default per-request timeout. Value: 30 seconds.
 *
 * If a complete request is not received within this window the connection is
 * closed.
 *
 * @note Valid range: 1 second to 3600 seconds. Values outside this range are
 *       rejected with ConfigError::invalid_value.
 */
inline constexpr std::chrono::seconds kDefaultRequestTimeout{30};

/**
 * @brief Default maximum number of HTTP headers per request. Value: 100.
 *
 * Matches the Node.js HTTP_MAX_HEADERS default. Requests with more headers are
 * rejected with HTTP 431 (Request Header Fields Too Large).
 *
 * @note Valid range: 1 to 1000. Values outside this range are rejected with
 *       ConfigError::invalid_value.
 */
inline constexpr std::size_t kDefaultMaxHeaderCount{100};

/**
 * @brief Default maximum bytes read from the TCP socket in one read() call.
 * Value: 65536 bytes (64 KiB).
 *
 * Matches a typical TCP receive window. Increasing reduces syscall overhead for
 * large requests; decreasing reduces per-connection memory.
 *
 * @note Valid range: 512 bytes to 16 MiB (16,777,216 bytes). Values outside
 *       this range are rejected with ConfigError::invalid_value.
 * @note This is a per-connection buffer size, not a limit on total body size.
 *       Use kDefaultMaxBodySize / AppConfig::max_body_size for the body limit.
 */
inline constexpr std::size_t kDefaultMaxReadBytes{65536};

// =============================================================================
// ExecutorConfig default constants
// =============================================================================

/**
 * @brief Default number of I/O worker threads. Value: 0.
 *
 * 0 resolves to `std::max(1u, std::thread::hardware_concurrency())` at executor
 * construction time. Set an explicit positive value to pin the thread count.
 *
 * @note Valid config-file range: 0 to 1024. 0 preserves hardware_concurrency behaviour.
 */
inline constexpr std::size_t kDefaultIoThreadCount{0};

/**
 * @brief Default number of dedicated CPU thread pool threads. Value: 4.
 *
 * The CPU pool is used exclusively by `aevox::pool(fn)`. Set to 0 to disable
 * the dedicated CPU pool (`pool()` work runs on I/O threads instead).
 *
 * @note Valid range: 0 to 256.
 */
inline constexpr std::size_t kDefaultCpuPoolThreads{4};

/**
 * @brief Default grace period for in-flight coroutines after stop(). Value: 30 seconds.
 *
 * After `stop()` is called the executor stops accepting connections and waits up
 * to this duration for in-flight coroutines to complete. If the timeout expires
 * the I/O context is force-stopped.
 *
 * @note Valid range: 1 second to 3600 seconds.
 */
inline constexpr std::chrono::seconds kDefaultDrainTimeout{30};

// =============================================================================
// ConfigError
// =============================================================================

/**
 * @brief Error codes for configuration loading and parsing.
 *
 * Returned via `std::expected<App, ConfigErrorDetail>` from `App::create()` when
 * a config file path is supplied. Never thrown.
 *
 * @note All values map to non-overlapping, distinct failure modes. Callers may
 *       switch on all three without a default case.
 * @note Thread-safety: stateless enum — inherently thread-safe.
 */
enum class ConfigError : std::uint8_t
{
    file_not_found, ///< The specified config file path does not exist on the filesystem.
    parse_error,    ///< The file exists but is not valid TOML syntax.
    invalid_value,  ///< A field value fails a range or type constraint.
};

/**
 * @brief Supplementary detail for a ConfigError.
 *
 * Pairs a `ConfigError` code with a human-readable message and, when applicable,
 * the name of the offending config key. Use this as the error type in
 * `std::expected` so callers have actionable context without parsing strings.
 *
 * @note Thread-safety: value type — safe to copy or move across threads.
 * @note Move semantics: moved-from `ConfigErrorDetail` has empty `message` and `key`.
 */
struct ConfigErrorDetail
{
    ConfigError code{ConfigError::file_not_found}; ///< Discriminant error code.
    std::string message;                           ///< Human-readable description.
    std::string key; ///< Offending TOML key (populated for invalid_value only).
};

/**
 * @brief Returns a short static description of a ConfigError code.
 *
 * @param e  The error code to describe.
 * @return   A null-terminated string literal. Lifetime is static — never dangles.
 */
[[nodiscard]] std::string_view to_string(ConfigError e) noexcept;

} // namespace aevox
