// examples/middleware-plugin/main.cpp
//
// Demonstrates how to write, register, and compose middleware in Aevox.
//
// Two middlewares are shown:
//
//   1. logger_middleware (lambda, global) — logs every request and response
//      status to stdout using std::format.
//
//   2. AuthMiddleware (named struct, scoped to /api) — checks the
//      Authorization header and short-circuits with 401 when absent or wrong.
//
// Onion execution order for GET /api/data:
//   logger_middleware (pre) -> AuthMiddleware (pre) -> handler -> (post) -> (post)
//
// Onion execution order for GET /health:
//   logger_middleware (pre) -> handler -> (post)
//   (AuthMiddleware does not run — path does not start with /api)
//
// Build:
//   cmake --build --preset default --target middleware-plugin
//
// Run:
//   ./middleware-plugin
//   # then in another terminal:
//   curl http://localhost:8080/health
//   curl http://localhost:8080/api/data                            # -> 401
//   curl -H "Authorization: secret-token-123" http://localhost:8080/api/data  # -> 200

#include <aevox/app.hpp>

#include <format>
#include <iostream>
#include <string_view>

// ---------------------------------------------------------------------------
// Compile-time token used by AuthMiddleware.
// In production: load from environment variable or config file.
// ---------------------------------------------------------------------------
constexpr std::string_view kExpectedToken = "secret-token-123";

// ---------------------------------------------------------------------------
// AuthMiddleware — named struct, stateful, reusable
//
// Holds the expected token string. Satisfies aevox::MiddlewareFn because
// operator() accepts (Request&, auto next) and returns Task<Response>.
//
// Thread-safety: const-invocable; required_token is read-only after
// construction. Safe for concurrent requests.
// ---------------------------------------------------------------------------
struct AuthMiddleware
{
    std::string required_token;

    [[nodiscard]] aevox::Task<aevox::Response> operator()(aevox::Request& req, auto next) const
    {
        auto auth = req.header("Authorization");
        if (!auth || *auth != required_token) {
            co_return aevox::Response::unauthorized("Missing or invalid Authorization token");
        }
        co_return co_await next(req);
    }
};

int main()
{
    aevox::App app;

    // -----------------------------------------------------------------------
    // Middleware 1: logger (global — wraps every request)
    //
    // Logs the method and path before forwarding, then logs the status code
    // after the inner chain returns. Uses std::format for output.
    // In production use aevox::log or a dedicated logging middleware.
    // -----------------------------------------------------------------------
    app.use([](aevox::Request& req, auto next) -> aevox::Task<aevox::Response> {
        std::cout << std::format("[logger] {} {}\n", aevox::to_string(req.method()), req.path());
        auto res = co_await next(req);
        std::cout << std::format("[logger] -> {}\n", res.status_code());
        co_return res;
    });

    // -----------------------------------------------------------------------
    // Middleware 2: auth guard (scoped to /api)
    //
    // Runs only for requests whose path starts with "/api".
    // Short-circuits with 401 when the Authorization header is absent or
    // does not match kExpectedToken. Passes through to the handler otherwise.
    // -----------------------------------------------------------------------
    app.use("/api", AuthMiddleware{.required_token = std::string{kExpectedToken}});

    // -----------------------------------------------------------------------
    // Routes
    // -----------------------------------------------------------------------

    // Public — logger runs, auth guard does not
    app.get("/", [](aevox::Request&) {
        return aevox::Response::ok("Welcome to the middleware-plugin example.");
    });

    // Protected — both logger and auth guard run
    app.get("/api/data", [](aevox::Request&) {
        return aevox::Response::ok("Protected data. Auth check passed.");
    });

    // Health check — logger runs, auth guard does not
    app.get("/health", [](aevox::Request&) { return aevox::Response::ok("ok"); });

    std::cout << "[middleware-plugin] listening on http://localhost:8080\n";
    std::cout << "  Routes:\n";
    std::cout << "    GET /           (public)\n";
    std::cout << "    GET /health     (public)\n";
    std::cout << "    GET /api/data   (requires Authorization: secret-token-123)\n";

    app.listen(8080);
}
