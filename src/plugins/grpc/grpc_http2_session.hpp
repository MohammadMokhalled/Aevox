#pragma once
// Internal nghttp2-backed cleartext h2c gRPC session.

#include <aevox/plugins/grpc.hpp>
#include <aevox/task.hpp>
#include <aevox/tcp_stream.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace aevox::grpc::detail {

class GrpcHttp2Session
{
public:
    using MethodMap = std::unordered_map<std::string, UnaryHandler>;

    GrpcHttp2Session(TcpStream stream, MethodMap& methods, const PluginConfig& config);
    ~GrpcHttp2Session();

    GrpcHttp2Session(const GrpcHttp2Session&)            = delete;
    GrpcHttp2Session& operator=(const GrpcHttp2Session&) = delete;
    GrpcHttp2Session(GrpcHttp2Session&&)                 = delete;
    GrpcHttp2Session& operator=(GrpcHttp2Session&&)      = delete;

    [[nodiscard]] Task<void> run();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aevox::grpc::detail
