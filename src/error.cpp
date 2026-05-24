#include <aevox/error.hpp>
#include <aevox/plugin.hpp>

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

std::string_view to_string(PluginError error) noexcept
{
    switch (error) {
        case PluginError::InvalidArgument:
            return "invalid argument";
        case PluginError::AlreadyInstalled:
            return "already installed";
        case PluginError::InstallFailed:
            return "install failed";
        case PluginError::StartFailed:
            return "start failed";
        case PluginError::AlreadyRunning:
            return "already running";
        case PluginError::NotRunning:
            return "not running";
        case PluginError::DependencyUnavailable:
            return "dependency unavailable";
        case PluginError::Unknown:
            return "unknown";
    }
    return "unknown";
}

ErrorCategory category(PluginError error) noexcept
{
    switch (error) {
        case PluginError::InvalidArgument:
            return ErrorCategory::Validation;
        case PluginError::AlreadyInstalled:
        case PluginError::AlreadyRunning:
        case PluginError::NotRunning:
            return ErrorCategory::State;
        case PluginError::InstallFailed:
        case PluginError::StartFailed:
            return ErrorCategory::Io;
        case PluginError::DependencyUnavailable:
            return ErrorCategory::NotFound;
        case PluginError::Unknown:
            return ErrorCategory::Unknown;
    }
    return ErrorCategory::Unknown;
}

} // namespace aevox
