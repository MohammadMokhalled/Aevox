// src/json/json_error.cpp
// Out-of-line definition for aevox::JsonError::message().
//
// Kept out of the header so that the clang static analyser cannot trace
// into the implementation when analysing translation units that move a
// JsonError and then call message() — a valid operation per the class
// contract (moved-from JsonError returns an empty view) that the analyser's
// cplusplus.Move check would otherwise flag as a false positive.

#include <aevox/json_error.hpp>

#include <utility>

namespace aevox {

std::string_view to_string(JsonErrorCode code) noexcept
{
    switch (code) {
        case JsonErrorCode::ParseError:
            return "JSON parse error";
        case JsonErrorCode::TypeMismatch:
            return "JSON type mismatch";
        case JsonErrorCode::MissingField:
            return "JSON missing field";
        case JsonErrorCode::SerializationFailed:
            return "JSON serialization failed";
        case JsonErrorCode::InvalidUtf8:
            return "JSON invalid UTF-8";
        case JsonErrorCode::Unknown:
            return "unknown JSON error";
    }
    return "unknown JSON error";
}

ErrorCategory category(JsonErrorCode code) noexcept
{
    switch (code) {
        case JsonErrorCode::ParseError:
            return ErrorCategory::Parse;
        case JsonErrorCode::TypeMismatch:
        case JsonErrorCode::MissingField:
        case JsonErrorCode::InvalidUtf8:
            return ErrorCategory::Validation;
        case JsonErrorCode::SerializationFailed:
            return ErrorCategory::Serialization;
        case JsonErrorCode::Unknown:
            return ErrorCategory::Unknown;
    }
    return ErrorCategory::Unknown;
}

JsonError::JsonError(JsonErrorCode code, std::string message) noexcept
    : code_{code}, message_{std::move(message)}
{}

JsonError::JsonError(std::string message) noexcept : message_{std::move(message)} {}

JsonErrorCode JsonError::code() const noexcept
{
    return code_;
}

std::string_view JsonError::message() const noexcept
{
    return message_;
}

ErrorCategory category(const JsonError& error) noexcept
{
    return category(error.code());
}

} // namespace aevox
