#pragma once

#include <aevox/error.hpp>
#include <aevox/middleware.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace aevox::middleware {

/**
 * @brief Configuration for static file serving middleware.
 *
 * `StaticFilesConfig` maps a private filesystem root owned by the consuming
 * application to a public HTTP URL prefix. Aevox's own install location is not
 * used or inferred.
 *
 * **Ownership:**
 * The factory copies or moves all fields into an immutable middleware state.
 * Callers may destroy or mutate the original config after successful creation.
 *
 * **Thread-safety:**
 * The constructed middleware stores canonicalized, read-only configuration and
 * may be invoked concurrently, subject to the filesystem contents being stable.
 *
 * **Move semantics:**
 * Value type. Moved-from strings, paths, and maps follow standard library
 * moved-from rules.
 *
 * @note Directory listings, Range requests, caching, and SPA fallback are not
 *       part of this release scope.
 */
struct StaticFilesConfig
{
    /**
     * @brief Absolute application-owned directory from which files are served.
     *
     * This is a filesystem path, not a URL path. Examples include
     * `/usr/share/example-app/public`, `/app/public`, or an absolute path
     * resolved from a development `public/` directory.
     *
     * @note Must be absolute, must exist, and must name a directory.
     */
    std::filesystem::path root;

    /**
     * @brief Public HTTP path prefix that activates this middleware.
     *
     * Requests outside this prefix delegate immediately to the next middleware.
     * `/static` maps `/static/style.css` to `root / "style.css"`.
     * `/` mounts the root at the site root and requires route precedence care.
     *
     * @note Must start with `/`. A trailing slash is accepted and normalized
     *       away except for the root prefix `/`.
     */
    std::string url_prefix{"/static"};

    /**
     * @brief File to serve when a directory path is requested.
     *
     * If empty, directory requests return 404. If non-empty, the middleware
     * attempts to serve this regular file from the requested directory.
     *
     * @note Directory listings are never generated.
     */
    std::string index_file{};

    /**
     * @brief MIME type overrides keyed by lowercase extension.
     *
     * Keys must include the leading dot, such as `.wasm`. Overrides take
     * precedence over Aevox's built-in MIME map.
     *
     * @note Both keys and values are copied into immutable middleware state.
     */
    std::unordered_map<std::string, std::string> mime_overrides{};
};

/**
 * @brief Configuration validation errors returned by `static_files()`.
 *
 * These errors describe construction-time configuration failures. Request-time
 * errors are represented as HTTP responses instead.
 *
 * @note Thread-safety: stateless enum, inherently thread-safe.
 * @note Ownership: no dynamic storage.
 */
enum class StaticFilesConfigError : std::uint8_t
{
    RootEmpty,           ///< `root` is empty.
    RootRelative,        ///< `root` is not an absolute filesystem path.
    RootNotFound,        ///< `root` does not exist or cannot be resolved.
    RootNotDirectory,    ///< `root` exists but is not a directory.
    InvalidUrlPrefix,    ///< `url_prefix` is empty or not a valid URL path prefix.
    InvalidIndexFile,    ///< `index_file` contains a path separator or traversal segment.
    InvalidMimeOverride, ///< A MIME override key or value is malformed.
};

/**
 * @brief Returns a stable text label for a static-files config error.
 *
 * @param error  Error value to describe.
 * @return Static string literal with process lifetime.
 * @note Thread-safety: safe to call concurrently.
 */
[[nodiscard]] std::string_view to_string(StaticFilesConfigError error) noexcept;

/**
 * @brief Maps a static-files config error to Aevox's shared error taxonomy.
 *
 * @param error  Error value to classify.
 * @return Broad error category for diagnostics, logging, and documentation.
 * @note Thread-safety: safe to call concurrently.
 */
[[nodiscard]] aevox::ErrorCategory category(StaticFilesConfigError error) noexcept;

/**
 * @brief Creates middleware that serves files from a configured directory.
 *
 * The returned middleware serves only `GET` and `HEAD` requests whose path
 * matches `config.url_prefix` with a segment boundary. Non-matching requests
 * delegate to the next middleware without filesystem access.
 *
 * @param config Static file serving configuration. All values are consumed into
 *        immutable middleware state on success.
 * @return `std::expected<aevox::Middleware, StaticFilesConfigError>` containing
 *         the middleware on success, or a validation error on failure.
 *
 * @note Thread-safety: successful middleware creation produces immutable state
 *       that is safe for concurrent request handling. The served directory
 *       should be treated as read-only while requests are in flight.
 * @note Move semantics: the returned `Middleware` is move-only.
 * @note Performance: file reads are synchronous and copy file contents into the
 *       response body for v0.2. Use a reverse proxy or CDN for large static
 *       assets under production load.
 */
[[nodiscard]] std::expected<aevox::Middleware, StaticFilesConfigError> static_files(
    StaticFilesConfig config);

} // namespace aevox::middleware
