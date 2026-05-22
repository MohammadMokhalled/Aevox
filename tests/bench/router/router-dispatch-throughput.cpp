// Router dispatch throughput and latency benchmark.
// ADD ref: Tasks/architecture/AEV-015-arch.md §8.2

#define ANKERL_NANOBENCH_IMPLEMENT
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/router.hpp>
#include <aevox/task.hpp>

#include <nanobench.h>

#include <chrono>
#include <coroutine>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "http/request_impl.hpp"
#include "support/bench_stats.hpp"

namespace {

std::vector<std::byte> make_buffer(std::string_view text)
{
    std::vector<std::byte> buffer(text.size());
    std::memcpy(buffer.data(), text.data(), text.size());
    return buffer;
}

[[nodiscard]] aevox::Request make_request(aevox::HttpMethod method, std::string_view path)
{
    const std::string_view method_text = aevox::to_string(method);

    std::string raw;
    raw.reserve(method_text.size() + path.size() + std::string_view{"  HTTP/1.1\r\n\r\n"}.size());
    raw += method_text;
    raw += ' ';
    raw += path;
    raw += " HTTP/1.1\r\n\r\n";

    auto buffer = make_buffer(raw);

    aevox::detail::ParsedRequest parsed;
    // Request internals store string_view fields into the byte buffer owned by Request::Impl.
    parsed.method =
        std::string_view{reinterpret_cast<const char*>(buffer.data()), method_text.size()};
    parsed.target =
        std::string_view{reinterpret_cast<const char*>(buffer.data()) + method_text.size() + 1U,
                         path.size()};
    parsed.keep_alive = true;

    return aevox::make_request_from_impl(std::move(buffer), std::move(parsed));
}

template <typename T> [[nodiscard]] T drive_task(aevox::Task<T> task)
{
    auto inner = task.await_suspend(std::noop_coroutine());
    inner.resume();
    return task.await_resume();
}

[[nodiscard]] aevox::Router make_router()
{
    aevox::Router router;
    router.get("/hello", [](aevox::Request&) { return aevox::Response::ok("Hello, World!"); });
    router.get("/users/{id:int}",
               [](aevox::Request&, int id) { return aevox::Response::ok(std::to_string(id)); });
    router.get("/files/{path...}",
               [](aevox::Request&, const std::string& path) { return aevox::Response::ok(path); });
    return router;
}

template <typename MakeRequest>
[[nodiscard]] std::vector<std::chrono::nanoseconds> collect_latency_samples(
    aevox::Router& router, MakeRequest make_request, std::size_t count)
{
    std::vector<std::chrono::nanoseconds> samples;
    samples.reserve(count);

    for (std::size_t index = 0; index < count; ++index) {
        auto       request  = make_request();
        const auto started  = std::chrono::steady_clock::now();
        auto       response = drive_task(router.dispatch(request));
        const auto finished = std::chrono::steady_clock::now();
        ankerl::nanobench::doNotOptimizeAway(response.status_code());
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started));
    }

    return samples;
}

void print_latency_summary(std::string_view                          label,
                           std::span<const std::chrono::nanoseconds> samples)
{
    const auto summary = aevox::test_support::summarize_latency(samples);
    std::cout << label << " latency p50: " << summary.p50.count() << " ns\n";
    std::cout << label << " latency p99: " << summary.p99.count() << " ns\n";
    std::cout << label << " latency p999: " << summary.p999.count() << " ns\n";
}

} // namespace

int main()
{
    constexpr int         kEpochIterations = 60000;
    constexpr std::size_t kLatencySamples  = 512;

    auto router = make_router();

    ankerl::nanobench::Bench bench;
    bench.title("Router dispatch throughput")
        .unit("dispatch")
        .minEpochIterations(kEpochIterations)
        .warmup(100);

    bench.run("router dispatch - static route", [&] {
        auto request  = make_request(aevox::HttpMethod::GET, "/hello");
        auto response = drive_task(router.dispatch(request));
        ankerl::nanobench::doNotOptimizeAway(response.status_code());
    });

    bench.run("router dispatch - typed parameter", [&] {
        auto request  = make_request(aevox::HttpMethod::GET, "/users/42");
        auto response = drive_task(router.dispatch(request));
        ankerl::nanobench::doNotOptimizeAway(response.status_code());
    });

    bench.run("router dispatch - wildcard route", [&] {
        auto request  = make_request(aevox::HttpMethod::GET, "/files/a/b/c.txt");
        auto response = drive_task(router.dispatch(request));
        ankerl::nanobench::doNotOptimizeAway(response.status_code());
    });

    const auto static_samples = collect_latency_samples(
        router, [] { return make_request(aevox::HttpMethod::GET, "/hello"); }, kLatencySamples);
    print_latency_summary("Router dispatch", static_samples);

    return 0;
}
