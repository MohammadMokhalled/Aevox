// src/net/executor_context.cpp
//
// The thread-local executor bridge functions (tl_post_to_cpu, tl_post_to_io,
// tl_schedule_after) are now defined as inline functions with function-local
// static thread_locals directly in include/aevox/async.hpp.
//
// This file is retained as a compilation unit to ensure the header is compiled
// into the library and to avoid ODR issues in older toolchains.
//
// Design: Tasks/architecture/AEV-006-arch.md §4.1
