#pragma once
// include/aevox/plugin.hpp
//
// Minimal public plugin lifecycle API. Plugin implementations are optional
// extension targets; the core owns only this small lifecycle boundary.

#include <aevox/error.hpp>

#include <cstdint>
#include <expected>
#include <string_view>

namespace aevox {

class App;

/**
 * @brief Error code returned by Aevox plugin lifecycle operations.
 *
 * `PluginError` is intentionally generic. Plugin-specific APIs may expose richer
 * error enums, but `App::install()` and app lifecycle management use this common
 * type so the core does not depend on plugin implementation details.
 *
 * @note Thread-safety: enum values are immutable and safe to copy between threads.
 * @note Ownership: no dynamic storage.
 */
enum class PluginError : std::uint8_t
{
    InvalidArgument,       ///< Plugin pointer, config, or registration input was invalid.
    AlreadyInstalled,      ///< The plugin object was installed more than once.
    InstallFailed,         ///< Plugin installation failed for a plugin-specific reason.
    StartFailed,           ///< Plugin failed to bind, listen, or start its worker loop.
    AlreadyRunning,        ///< `start()` was called after the plugin was already running.
    NotRunning,            ///< `stop()` or lifecycle operation observed a non-running plugin.
    DependencyUnavailable, ///< Required optional dependency was not available in this build.
    Unknown,               ///< Opaque fallback for unexpected plugin failures.
};

/**
 * @brief Returns a stable text label for a PluginError.
 *
 * @param error  Error code to describe.
 * @return Static string literal with process lifetime.
 * @note Thread-safety: safe to call concurrently.
 */
[[nodiscard]] std::string_view to_string(PluginError error) noexcept;

/**
 * @brief Maps a PluginError to the shared Aevox error category taxonomy.
 *
 * @param error  Error code to classify.
 * @return Broad error category used by logs, docs, and generic error handling.
 * @note Thread-safety: safe to call concurrently.
 */
[[nodiscard]] ErrorCategory category(PluginError error) noexcept;

/**
 * @brief Base interface for first-party Aevox plugins.
 *
 * Plugins are installed on an `App` before `listen()` starts. `App` owns installed
 * plugin objects, starts them before the HTTP/1.1 executor begins accepting, and
 * stops them during `App::stop()` and destruction.
 *
 * @note Thread-safety: `install()` and `start()` are called by `App` on the main
 *       thread. `stop()` must be thread-safe because `App::stop()` is thread-safe.
 * @note Move semantics: plugin implementations are owned through
 *       `std::unique_ptr<Plugin>` and are not copied. Concrete plugin types may be
 *       movable before installation.
 * @note Ownership: `App` owns the plugin after successful `App::install()`.
 */
class Plugin
{
public:
    /**
     * @brief Destroys a plugin through the base lifecycle interface.
     *
     * Concrete plugin destructors must release owned resources and should call
     * `stop()` or otherwise ensure background work has drained before returning.
     *
     * @note Thread-safety: destruction must not race with lifecycle calls.
     * @note Move semantics: base plugin objects are not movable.
     * @note Ownership: releases resources owned by the concrete plugin.
     */
    virtual ~Plugin() = default;

    /**
     * @brief Copy construction is disabled for plugin lifecycle objects.
     *
     * @param other  Source plugin, intentionally unsupported.
     * @note Thread-safety: not applicable because the operation is deleted.
     */
    Plugin(const Plugin&) = delete;

    /**
     * @brief Copy assignment is disabled for plugin lifecycle objects.
     *
     * @param other  Source plugin, intentionally unsupported.
     * @return This plugin, intentionally unavailable.
     * @note Thread-safety: not applicable because the operation is deleted.
     */
    Plugin& operator=(const Plugin&) = delete;

    /**
     * @brief Move construction is disabled through the base lifecycle interface.
     *
     * @param other  Source plugin, intentionally unsupported.
     * @note Thread-safety: not applicable because the operation is deleted.
     */
    Plugin(Plugin&&) = delete;

    /**
     * @brief Move assignment is disabled through the base lifecycle interface.
     *
     * @param other  Source plugin, intentionally unsupported.
     * @return This plugin, intentionally unavailable.
     * @note Thread-safety: not applicable because the operation is deleted.
     */
    Plugin& operator=(Plugin&&) = delete;

    /**
     * @brief Returns the stable plugin name.
     *
     * @return Static string literal with process lifetime.
     * @note Thread-safety: safe to call concurrently.
     */
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /**
     * @brief Attaches this plugin to an App before the app starts listening.
     *
     * The plugin may validate registrations and capture App-owned services, but
     * must not block and must not start network I/O. Network I/O begins in
     * `start()`.
     *
     * @param app  App that will own and start the plugin after installation.
     * @return `std::expected<void, PluginError>`:
     *         - success when the plugin is installed and ready to start.
     *         - `PluginError::AlreadyInstalled` if the same plugin is installed twice.
     *         - `PluginError::InvalidArgument` for invalid plugin state.
     *         - `PluginError::InstallFailed` for plugin-specific failures.
     * @note Thread-safety: not thread-safe. Called by `App::install()` before `listen()`.
     */
    [[nodiscard]] virtual std::expected<void, PluginError> install(App& app) noexcept = 0;

    /**
     * @brief Starts plugin-owned background work.
     *
     * `App::listen()` calls this before starting the HTTP/1.1 executor. The method
     * must return after the plugin has either bound successfully or reported a
     * startup error; it must not block for the lifetime of the server.
     *
     * @return `std::expected<void, PluginError>`:
     *         - success when startup completed.
     *         - `PluginError::StartFailed` for bind/listen/worker startup failure.
     *         - `PluginError::AlreadyRunning` when called twice.
     * @note Thread-safety: not thread-safe. Called by `App::listen()` on the main thread.
     */
    [[nodiscard]] virtual std::expected<void, PluginError> start() noexcept = 0;

    /**
     * @brief Requests plugin shutdown and drains background work.
     *
     * The function must be idempotent. It must not throw and must not block
     * indefinitely.
     *
     * @note Thread-safety: safe to call concurrently with plugin worker threads.
     */
    virtual void stop() noexcept = 0;

protected:
    /**
     * @brief Constructs the base portion of a concrete plugin.
     *
     * Only derived plugin implementations can construct the base type directly.
     *
     * @note Thread-safety: construction is single-threaded before publication.
     * @note Ownership: establishes no ownership on its own.
     */
    Plugin() = default;
};

} // namespace aevox
