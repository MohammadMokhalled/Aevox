#pragma once
// src/log/spdlog_backend.hpp
//
// INTERNAL — SpdlogBackend declaration. NO spdlog headers here.
// The implementation (spdlog_backend.cpp) is the ONLY file that includes
// <spdlog/spdlog.h>.
//
// Design: Tasks/architecture/AEV-011-arch.md §4.5

#include <aevox/log.hpp>

#include <memory>

namespace aevox {

class LogEntry;

/**
 * @brief spdlog-backed implementation of the LogBackend concept.
 *
 * Owns one spdlog::logger instance with one or more sinks (console, file).
 * Each sink has a custom formatter that renders LogEntry in either JSON or
 * Pretty format.
 */
class SpdlogBackend
{
public:
    explicit SpdlogBackend(const LogConfig& config);
    ~SpdlogBackend();

    SpdlogBackend(const SpdlogBackend&)            = delete;
    SpdlogBackend& operator=(const SpdlogBackend&) = delete;
    SpdlogBackend(SpdlogBackend&&)                 = delete;
    SpdlogBackend& operator=(SpdlogBackend&&)      = delete;

    void write(const LogEntry& entry);
    void flush();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aevox
