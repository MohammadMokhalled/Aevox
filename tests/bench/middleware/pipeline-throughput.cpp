// tests/bench/middleware/pipeline-throughput.cpp
//
// Performance benchmark: middleware pipeline throughput.
// Measures overhead of middleware composition vs. direct dispatch.

#include <aevox/app.hpp>
#include <aevox/middleware.hpp>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

auto pass_through_middleware()
{
    return [](aevox::Request&                                                        req,
              std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
               -> aevox::Task<aevox::Response> { co_return co_await next(req); };
}

} // namespace

TEST_CASE("Middleware: throughput benchmark", "[bench][middleware]")
{
    SECTION("baseline: no middleware")
    {
        aevox::App app;
        app.get("/test", [](aevox::Request& /*req*/) { return aevox::Response::ok("OK"); });

        // Benchmark would require real request dispatch
        // This is a placeholder - actual throughput test would need HTTP roundtrip
    }

    SECTION("single pass-through middleware")
    {
        aevox::App app;
        app.use(pass_through_middleware());
        app.get("/test", [](aevox::Request& /*req*/) { return aevox::Response::ok("OK"); });

        // Benchmark would require real request dispatch
    }

    SECTION("ten pass-through middleware")
    {
        aevox::App app;
        for (int i = 0; i < 10; ++i) {
            app.use(pass_through_middleware());
        }
        app.get("/test", [](aevox::Request& /*req*/) { return aevox::Response::ok("OK"); });

        // Benchmark would require real request dispatch
    }
}
