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
        case ConfigError::FileNotFound:
            return "file not found";
        case ConfigError::ParseError:
            return "TOML parse error";
        case ConfigError::InvalidValue:
            return "invalid field value";
    }
    return "unknown config error";
}

ConfigError ConfigErrorDetail::error_code() const noexcept
{
    return code;
}

std::string_view ConfigErrorDetail::error_message() const noexcept
{
    return message;
}

std::string_view ConfigErrorDetail::error_key() const noexcept
{
    return key;
}

ErrorCategory category(ConfigError e) noexcept
{
    switch (e) {
        case ConfigError::FileNotFound:
            return ErrorCategory::NotFound;
        case ConfigError::ParseError:
            return ErrorCategory::Parse;
        case ConfigError::InvalidValue:
            return ErrorCategory::Validation;
    }
    return ErrorCategory::Unknown;
}

ErrorCategory category(const ConfigErrorDetail& detail) noexcept
{
    return category(detail.error_code());
}

} // namespace aevox
