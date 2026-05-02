// json-serialization-throughput.cpp: JSON backend throughput baselines
// ADD ref: Tasks/architecture/AEV-009-arch.md § Test Architecture
//
// Establishes the glaze backend throughput baseline for future regression
// tracking (AEV-015). No regression assertion for v0.2 — this is the first run.

#define ANKERL_NANOBENCH_IMPLEMENT

#include <nanobench.h>

#include <iostream>
#include <string>

#include "json/glaze_backend.hpp"

// =============================================================================
// Test fixtures — external linkage required by glaze extern template variables.
// =============================================================================

struct PersonDto
{
    std::string name;
    int         age{};
    bool        active{};
};

struct OrderDto
{
    std::string id;
    std::string customer_name;
    std::string status;
    int         quantity{};
    double      price{};
    bool        paid{};
    std::string payment_method;
    std::string shipping_address;
    std::string billing_address;
    std::string created_at;
    std::string updated_at;
    int         priority{};
    std::string notes;
    bool        shipped{};
    double      tax{};
};

// =============================================================================
// main — nanobench measurements
// =============================================================================

int main()
{
    constexpr aevox::internal::GlazeBackend kBackend{};

    const PersonDto   person{.name = "alice", .age = 30, .active = true};
    const std::string person_json = R"({"name":"alice","age":30,"active":true})";

    const OrderDto order{.id               = "ORD-001",
                         .customer_name    = "alice",
                         .status           = "pending",
                         .quantity         = 5,
                         .price            = 99.99,
                         .paid             = false,
                         .payment_method   = "credit_card",
                         .shipping_address = "123 Main St",
                         .billing_address  = "123 Main St",
                         .created_at       = "2026-01-01T00:00:00Z",
                         .updated_at       = "2026-01-02T00:00:00Z",
                         .priority         = 1,
                         .notes            = "rush order",
                         .shipped          = false,
                         .tax              = 8.99};
    const auto     order_json = kBackend.serialize(order);

    ankerl::nanobench::Bench bench;
    bench.title("GlazeBackend throughput baselines")
        .unit("op")
        .minEpochIterations(100000)
        .warmup(1000);

    bench.run("GlazeBackend deserialize - small struct (3 fields)", [&] {
        auto result = kBackend.deserialize<PersonDto>(person_json);
        ankerl::nanobench::doNotOptimizeAway(result);
    });

    bench.run("GlazeBackend serialize - small struct (3 fields)", [&] {
        auto result = kBackend.serialize(person);
        ankerl::nanobench::doNotOptimizeAway(result);
    });

    if (order_json) {
        bench.run("GlazeBackend deserialize - medium struct (15 fields)", [&] {
            auto result = kBackend.deserialize<OrderDto>(*order_json);
            ankerl::nanobench::doNotOptimizeAway(result);
        });
    }
    else {
        std::cout << "Skipped medium struct benchmark: OrderDto serialization failed\n";
    }

    return 0;
}
