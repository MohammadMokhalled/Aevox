// src/router/middleware.cpp
//
// Implementation of aevox::Middleware — type-erased middleware wrapper.
//
// Uses std::move_only_function for zero-cost type erasure (no virtual dispatch).
// This keeps middleware invocation in the hot path with no vtable overhead.

#include <aevox/middleware.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/task.hpp>

namespace aevox {

Task<Response> Middleware::operator()(Request&                                          req,
                                      std::move_only_function<Task<Response>(Request&)> next)
{
    if (!fn_) {
        co_return Response::bad_request("Internal middleware error");
    }
    co_return co_await fn_(req, std::move(next));
}

} // namespace aevox
