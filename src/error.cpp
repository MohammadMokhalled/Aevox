#include <aevox/error.hpp>

#include <string_view>

namespace aevox {

std::string_view to_string(ErrorCategory category) noexcept
{
    switch (category) {
        case ErrorCategory::Io:
            return "io";
        case ErrorCategory::Protocol:
            return "protocol";
        case ErrorCategory::Parse:
            return "parse";
        case ErrorCategory::Validation:
            return "validation";
        case ErrorCategory::NotFound:
            return "not found";
        case ErrorCategory::Serialization:
            return "serialization";
        case ErrorCategory::State:
            return "state";
        case ErrorCategory::Unknown:
            return "unknown";
    }
    return "unknown";
}

} // namespace aevox
