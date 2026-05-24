#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/executor.hpp>
#include <aevox/plugin.hpp>
#include <aevox/plugins/grpc.hpp>
#include <aevox/task.hpp>
#include <aevox/tcp_stream.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "grpc_http2_session.hpp"

namespace aevox::grpc {

namespace {

[[nodiscard]] bool is_valid_method(std::string_view method) noexcept
{
    if (!method.starts_with('/')) {
        return false;
    }

    const auto second_slash = method.find('/', 1U);
    if (second_slash == std::string_view::npos || second_slash == 1U ||
        second_slash + 1U == method.size())
    {
        return false;
    }

    return method.find('/', second_slash + 1U) == std::string_view::npos;
}

[[nodiscard]] std::expected<void, GrpcError> validate_config(const PluginConfig& config) noexcept
{
    if (config.port == 0U || config.max_concurrent_streams == 0U || config.max_message_size == 0U) {
        return std::unexpected{GrpcError::InvalidConfig};
    }
    return {};
}

[[nodiscard]] PluginError to_plugin_error(GrpcError error) noexcept
{
    switch (error) {
        case GrpcError::InvalidConfig:
        case GrpcError::InvalidMethod:
        case GrpcError::DuplicateMethod:
            return PluginError::InvalidArgument;
        case GrpcError::AlreadyInstalled:
            return PluginError::AlreadyInstalled;
        case GrpcError::AlreadyRunning:
            return PluginError::AlreadyRunning;
        case GrpcError::ListenFailed:
        case GrpcError::StartFailed:
            return PluginError::StartFailed;
        case GrpcError::ProtocolError:
        case GrpcError::MessageTooLarge:
        case GrpcError::CompressionUnsupported:
            return PluginError::InstallFailed;
        case GrpcError::Unknown:
            return PluginError::Unknown;
    }
    return PluginError::Unknown;
}

} // namespace

class Plugin::Impl
{
public:
    explicit Impl(PluginConfig cfg) : config_{cfg} {}

private:
    friend class Plugin;

    PluginConfig                        config_;
    detail::GrpcHttp2Session::MethodMap unary_methods_;
    std::unique_ptr<aevox::Executor>    executor_;
    std::jthread                        runner_;
    std::atomic_bool                    installed_{false};
    std::atomic_bool                    running_{false};
};

Plugin::Plugin(PluginConfig config) : impl_{std::make_unique<Impl>(config)} {}

Plugin::~Plugin()
{
    stop();
}

Plugin::Plugin(Plugin&& other) noexcept : aevox::Plugin{}, impl_{std::move(other.impl_)} {}

Plugin& Plugin::operator=(Plugin&& other) noexcept
{
    if (this != &other) {
        stop();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

std::expected<void, GrpcError> Plugin::add_unary_method(std::string_view method,
                                                        UnaryHandler     handler) noexcept
{
    if (!impl_) {
        return std::unexpected{GrpcError::Unknown};
    }
    if (impl_->installed_.load(std::memory_order_acquire)) {
        return std::unexpected{GrpcError::AlreadyInstalled};
    }
    if (!handler || !is_valid_method(method)) {
        return std::unexpected{GrpcError::InvalidMethod};
    }

    auto [_, inserted] = impl_->unary_methods_.emplace(std::string{method}, std::move(handler));
    if (!inserted) {
        return std::unexpected{GrpcError::DuplicateMethod};
    }

    return {};
}

std::string_view Plugin::name() const noexcept
{
    return "aevox.grpc";
}

std::expected<void, PluginError> Plugin::install(aevox::App&) noexcept
{
    if (!impl_) {
        return std::unexpected{PluginError::InvalidArgument};
    }
    if (impl_->installed_.exchange(true, std::memory_order_acq_rel)) {
        return std::unexpected{PluginError::AlreadyInstalled};
    }

    auto valid = validate_config(impl_->config_);
    if (!valid) {
        impl_->installed_.store(false, std::memory_order_release);
        return std::unexpected{to_plugin_error(valid.error())};
    }

    return {};
}

std::expected<void, PluginError> Plugin::start() noexcept
{
    if (!impl_) {
        return std::unexpected{PluginError::InvalidArgument};
    }
    if (impl_->running_.exchange(true, std::memory_order_acq_rel)) {
        return std::unexpected{PluginError::AlreadyRunning};
    }

    auto valid = validate_config(impl_->config_);
    if (!valid) {
        impl_->running_.store(false, std::memory_order_release);
        return std::unexpected{to_plugin_error(valid.error())};
    }

    ExecutorConfig executor_config;
    executor_config.thread_count =
        impl_->config_.worker_threads == 0U ? kDefaultIoThreadCount : impl_->config_.worker_threads;
    executor_config.drain_timeout =
        std::chrono::ceil<std::chrono::seconds>(impl_->config_.drain_timeout);
    impl_->executor_ = make_executor(executor_config);

    auto listen_result = impl_->executor_->listen(
        impl_->config_.port,
        [methods = std::ref(impl_->unary_methods_),
         config  = impl_->config_](std::uint64_t, TcpStream stream) mutable -> Task<void> {
            detail::GrpcHttp2Session session{std::move(stream), methods.get(), config};
            co_await session.run();
        });

    if (!listen_result) {
        impl_->executor_.reset();
        impl_->running_.store(false, std::memory_order_release);
        return std::unexpected{PluginError::StartFailed};
    }

    impl_->runner_ = std::jthread{[executor = impl_->executor_.get()] {
        auto run_result = executor->run();
        (void)run_result;
    }};

    return {};
}

void Plugin::stop() noexcept
{
    if (!impl_) {
        return;
    }

    if (impl_->executor_) {
        impl_->executor_->stop();
    }

    if (impl_->runner_.joinable()) {
        impl_->runner_.join();
    }

    impl_->running_.store(false, std::memory_order_release);
}

std::unique_ptr<aevox::Plugin> make_plugin(PluginConfig config)
{
    return std::make_unique<Plugin>(config);
}

} // namespace aevox::grpc
