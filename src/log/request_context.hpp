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
    std::string request_id;   // 16 hex chars, random.
    std::string trace_id;     // 32 hex chars, from traceparent. Empty if absent.
    std::string span_id;      // 16 hex chars, from traceparent. Empty if absent.
    std::string traceparent;  // Full "00-<trace_id>-<parent_id>-<flags>" string.
    std::size_t thread_id{0}; // Hashed thread ID.
    std::chrono::steady_clock::time_point accept_time;
};

} // namespace aevox
