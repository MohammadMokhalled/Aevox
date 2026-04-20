#pragma once
// src/config/toml_loader.hpp
//
// INTERNAL — never included outside src/config/ or tests/.
//
// Declares the TOML config loader. The implementation (toml_loader.cpp) uses
// toml++ to parse the file. No toml++ type appears in this header — the
// interface is expressed purely in terms of aevox public types.

#include <aevox/app.hpp>
#include <aevox/config.hpp>

#include <expected>
#include <string_view>

namespace aevox::config {

/**
 * Loads and validates a TOML config file, returning a fully-merged AppConfig.
 *
 * The caller supplies a base config; only keys present in the file override
 * the corresponding fields. Unrecognised keys are silently ignored after
 * writing a warning to std::clog. Each field that is present undergoes a
 * range check; the first failing field causes an immediate invalid_value
 * return — subsequent keys are not validated.
 *
 * @param path  Filesystem path to the TOML file. Must not be empty.
 * @param base  Starting AppConfig. File values override fields in base.
 * @return      Merged AppConfig on success, ConfigErrorDetail on failure.
 *
 * @note This is a synchronous function — it performs blocking file I/O. It is
 *       called only once at App construction time, before any async work starts.
 *       Must not be called from a coroutine or I/O thread.
 */
[[nodiscard]] std::expected<AppConfig, ConfigErrorDetail> load_toml_config(std::string_view path,
                                                                           AppConfig base) noexcept;

} // namespace aevox::config
