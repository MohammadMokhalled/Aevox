#include <aevox/error.hpp>
#include <aevox/plugins/grpc.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aevox::grpc {

std::string_view to_string(GrpcError error) noexcept
{
    switch (error) {
        case GrpcError::InvalidConfig:
            return "invalid config";
        case GrpcError::InvalidMethod:
            return "invalid method";
        case GrpcError::DuplicateMethod:
            return "duplicate method";
        case GrpcError::AlreadyInstalled:
            return "already installed";
        case GrpcError::AlreadyRunning:
            return "already running";
        case GrpcError::ListenFailed:
            return "listen failed";
        case GrpcError::StartFailed:
            return "start failed";
        case GrpcError::ProtocolError:
            return "protocol error";
        case GrpcError::MessageTooLarge:
            return "message too large";
        case GrpcError::CompressionUnsupported:
            return "compression unsupported";
        case GrpcError::Unknown:
            return "unknown";
    }
    return "unknown";
}

ErrorCategory category(GrpcError error) noexcept
{
    switch (error) {
        case GrpcError::InvalidConfig:
        case GrpcError::InvalidMethod:
        case GrpcError::DuplicateMethod:
            return ErrorCategory::Validation;
        case GrpcError::AlreadyInstalled:
        case GrpcError::AlreadyRunning:
            return ErrorCategory::State;
        case GrpcError::ListenFailed:
        case GrpcError::StartFailed:
            return ErrorCategory::Io;
        case GrpcError::ProtocolError:
        case GrpcError::CompressionUnsupported:
            return ErrorCategory::Protocol;
        case GrpcError::MessageTooLarge:
            return ErrorCategory::Validation;
        case GrpcError::Unknown:
            return ErrorCategory::Unknown;
    }
    return ErrorCategory::Unknown;
}

GrpcStatus GrpcStatus::ok()
{
    return {};
}

GrpcStatus GrpcStatus::error(StatusCode code, std::string_view message)
{
    return {.code = code, .message = std::string{message}};
}

UnaryRequest::UnaryRequest(std::string method, std::vector<std::byte> payload,
                           std::vector<Metadata> metadata)
    : method_{std::move(method)}, payload_{std::move(payload)}, metadata_{std::move(metadata)}
{}

std::string_view UnaryRequest::method() const noexcept
{
    return method_;
}

std::span<const std::byte> UnaryRequest::payload() const noexcept
{
    return payload_;
}

std::span<const Metadata> UnaryRequest::metadata() const noexcept
{
    return metadata_;
}

} // namespace aevox::grpc
