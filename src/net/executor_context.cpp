// src/net/executor_context.cpp
//
// Definitions for the thread_local executor bridge functions declared in
// include/aevox/async.hpp (aevox::detail namespace).
//
// These std::function objects are populated by AsioExecutor when each I/O
// worker thread starts — before io_ctx_.run() is called. They remain valid
// for the lifetime of the thread and are the mechanism by which the Asio-free
// public helpers (pool, sleep, when_all) dispatch work to Asio internals.
//
// Having definitions in a single TU (this file) prevents ODR violations that
// would arise from multiple TUs defining the same thread_local variable.
//
// Design: ADD §4.1 (AEV-006-arch.md)

#include <aevox/async.hpp>

namespace aevox::detail {

thread_local std::function<void(std::move_only_function<void()>)>
    tl_post_to_cpu;

thread_local std::function<void(std::move_only_function<void()>)>
    tl_post_to_io;

thread_local std::function<void(std::chrono::steady_clock::duration, std::move_only_function<void()>)>
    tl_schedule_after;

} // namespace aevox::detail
