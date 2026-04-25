// src/router/middleware.cpp
//
// Implementation of aevox::Middleware — type-erased middleware wrapper.
//
// The Middleware class uses the pimpl pattern with a virtual interface
// (MiddlewareImpl) to enable type erasure of arbitrary middleware callables.
// This keeps the public API simple while supporting any callable signature
// that matches the MiddlewareFn concept.

#include <aevox/middleware.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/task.hpp>

namespace aevox {

Task<Response> Middleware::operator()(
    Request& req,
    std::move_only_function<Task<Response>(Request&)> next) const
{
    if (!impl_) {
        // Should not happen in normal use — middleware must be constructed
        // via App::use(). If it does, return an internal server error.
        co_return Response::internal_server_error("Middleware not initialized");
    }

    // Delegate to the concrete middleware implementation.
    co_return co_await impl_->invoke(req, std::move(next));
}

} // namespace aevox
