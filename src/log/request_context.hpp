#pragma once
// src/log/request_context.hpp
//
// INTERNAL — per-request logging correlation data.
//
// Extracted from src/http/request_impl.hpp to avoid a layering violation
// where src/log/ code had to include HTTP implementation details.
//
// Design: Tasks/architecture/AEV-011-arch.md §4.6

#include <chrono>
#include <cstddef>
#include <string>

namespace aevox {

struct RequestContext
{
    std::string                           request_id;     // UUID or nanoid, assigned by acceptor.
    std::string                           correlation_id; // Populated by AEV-012 tracing hooks.
    std::size_t                           thread_id{0};   // Hashed thread ID.
    std::chrono::steady_clock::time_point accept_time;
};

} // namespace aevox
