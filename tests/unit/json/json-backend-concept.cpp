// json-backend-concept.cpp: aevox::JsonBackend concept unit tests
// ADD ref: Tasks/architecture/AEV-009-arch.md § Test Architecture

#include <aevox/json_backend.hpp>
#include <aevox/json_error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <string>
#include <string_view>

#include "json/glaze_backend.hpp"

// =============================================================================
// Minimal conforming backend — for positive concept check
// =============================================================================

namespace {

struct MinimalBackend
{
    template <typename T>
    [[nodiscard]] std::expected<T, aevox::JsonError> deserialize(std::string_view /*input*/) const
    {
        return T{};
    }

    template <typename T>
    [[nodiscard]] std::expected<std::string, aevox::JsonError> serialize(const T& /*value*/) const
    {
        return std::string{"{}"};
    }
};

// =============================================================================
// Backends missing one required operation — for negative concept checks
// =============================================================================

struct MissingDeserialize
{
    template <typename T>
    [[nodiscard]] std::expected<std::string, aevox::JsonError> serialize(const T& /*value*/) const
    {
        return std::string{"{}"};
    }
};

struct MissingSerialize
{
    template <typename T>
    [[nodiscard]] std::expected<T, aevox::JsonError> deserialize(std::string_view /*input*/) const
    {
        return T{};
    }
};

} // namespace

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("JsonBackend concept - GlazeBackend satisfies the concept", "[json]")
{
    static_assert(aevox::JsonBackend<aevox::internal::GlazeBackend>,
                  "GlazeBackend must satisfy aevox::JsonBackend");
    SUCCEED("GlazeBackend satisfies aevox::JsonBackend");
}

TEST_CASE("JsonBackend concept - minimal custom backend satisfies the concept", "[json]")
{
    static_assert(aevox::JsonBackend<MinimalBackend>,
                  "MinimalBackend must satisfy aevox::JsonBackend");
    SUCCEED("MinimalBackend satisfies aevox::JsonBackend");
}

TEST_CASE("JsonBackend concept - backend missing deserialize does not satisfy the concept",
          "[json]")
{
    static_assert(!aevox::JsonBackend<MissingDeserialize>,
                  "MissingDeserialize must NOT satisfy aevox::JsonBackend");
    SUCCEED("MissingDeserialize correctly does not satisfy aevox::JsonBackend");
}

TEST_CASE("JsonBackend concept - backend missing serialize does not satisfy the concept", "[json]")
{
    static_assert(!aevox::JsonBackend<MissingSerialize>,
                  "MissingSerialize must NOT satisfy aevox::JsonBackend");
    SUCCEED("MissingSerialize correctly does not satisfy aevox::JsonBackend");
}
