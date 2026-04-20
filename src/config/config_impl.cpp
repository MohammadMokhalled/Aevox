// src/config/config_impl.cpp
//
// Defines aevox::to_string(ConfigError).
// Parallel to the existing to_string(ExecutorError) in src/net/ and
// to_string(IoError) in src/net/asio_tcp_stream.cpp.

#include <aevox/config.hpp>

namespace aevox {

std::string_view to_string(ConfigError e) noexcept
{
    switch (e) {
        case ConfigError::file_not_found:
            return "file not found";
        case ConfigError::parse_error:
            return "TOML parse error";
        case ConfigError::invalid_value:
            return "invalid field value";
    }
    return "unknown config error";
}

} // namespace aevox
