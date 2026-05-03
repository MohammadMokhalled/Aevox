// json-glaze-backend.cpp: aevox::internal::GlazeBackend unit tests
// ADD ref: Tasks/architecture/AEV-009-arch.md § Test Architecture

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

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

namespace {
constexpr aevox::internal::GlazeBackend kBackend{};
} // namespace

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("GlazeBackend - deserialize valid JSON into struct", "[json]")
{
    SECTION("happy path - valid JSON into PersonDto")
    {
        const auto result =
            kBackend.deserialize<PersonDto>(R"({"name":"alice","age":30,"active":true})");
        REQUIRE(result.has_value());
        if (result) {
            CHECK(result->name == "alice");
            CHECK(result->age == 30);
            CHECK(result->active == true);
        }
    }

    SECTION("error path - truncated JSON returns unexpected")
    {
        const auto result = kBackend.deserialize<PersonDto>(R"({"name":"alice")");
        REQUIRE(!result.has_value());
        CHECK(!result.error().message().empty());
    }

    SECTION("error path - type mismatch returns unexpected")
    {
        const auto result = kBackend.deserialize<PersonDto>(
            R"({"name":"alice","age":"not-a-number","active":true})");
        REQUIRE(!result.has_value());
        CHECK(!result.error().message().empty());
    }

    SECTION("edge path - empty string input returns unexpected")
    {
        const auto result = kBackend.deserialize<PersonDto>("");
        REQUIRE(!result.has_value());
        CHECK(!result.error().message().empty());
    }

    SECTION("edge path - JSON null as root value returns unexpected")
    {
        const auto result = kBackend.deserialize<PersonDto>("null");
        REQUIRE(!result.has_value());
        CHECK(!result.error().message().empty());
    }
}

TEST_CASE("GlazeBackend - serialize struct to JSON string", "[json]")
{
    SECTION("happy path - serialize PersonDto contains expected fields")
    {
        const PersonDto person{.name = "alice", .age = 30, .active = true};
        const auto      result = kBackend.serialize(person);
        REQUIRE(result.has_value());
        if (result) {
            CHECK(result->find("\"name\"") != std::string::npos);
            CHECK(result->find("\"alice\"") != std::string::npos);
            CHECK(result->find("\"age\"") != std::string::npos);
            CHECK(result->find("30") != std::string::npos);
        }
    }

    SECTION("edge path - struct with empty string fields produces valid JSON")
    {
        const PersonDto empty_person{.name = "", .age = 0, .active = false};
        const auto      result = kBackend.serialize(empty_person);
        REQUIRE(result.has_value());
        if (result) {
            CHECK(result->find("\"name\"") != std::string::npos);
            CHECK(result->find("\"\"") != std::string::npos);
        }
    }
}
