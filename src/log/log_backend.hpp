#pragma once
// src/log/log_backend.hpp
//
// INTERNAL — LogBackend concept and factory.
//
// Design: Tasks/architecture/AEV-011-arch.md §4.4

#include <concepts>
#include <memory>

#include "log_entry.hpp"

namespace aevox {

/**
 * @brief Compile-time concept for pluggable log backends.
 *
 * Satisfied by any type that provides `write(const LogEntry&)` and `flush()`.
 * The default backend for v0.2 is SpdlogBackend.
 */
template <typename T>
concept LogBackend = requires(T& t, const LogEntry& entry) {
    { t.write(entry) } -> std::same_as<void>;
    { t.flush() } -> std::same_as<void>;
};

/**
 * @brief Factory that creates the active LogBackend from a LogConfig.
 *
 * The concrete type is selected at compile time via the AEVOX_LOG_BACKEND
 * CMake option. Only SpdlogBackend is implemented for v0.2.
 */
[[nodiscard]] std::unique_ptr<class SpdlogBackend> make_log_backend(const LogConfig& config);

} // namespace aevox
