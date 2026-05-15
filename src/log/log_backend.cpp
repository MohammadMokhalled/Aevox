// src/log/log_backend.cpp
//
// INTERNAL — LogBackend factory implementation.
//
// Design: Tasks/architecture/AEV-011-arch.md §4.4

#include "log_backend.hpp"

#include "spdlog_backend.hpp"

namespace aevox {

std::unique_ptr<SpdlogBackend> make_log_backend(const LogConfig& config)
{
    return std::make_unique<SpdlogBackend>(config);
}

} // namespace aevox
