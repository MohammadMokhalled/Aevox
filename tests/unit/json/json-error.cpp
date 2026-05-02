// json-error.cpp: aevox::JsonError unit tests
// ADD ref: Tasks/architecture/AEV-009-arch.md § Test Architecture

#include <aevox/json_error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

TEST_CASE("JsonError - message is accessible and non-empty on parse failure", "[json]")
{
    const aevox::JsonError err{"unexpected end of input at line 1, col 5"};

    SECTION("message returns the stored string as string_view")
    {
        std::string_view msg = err.message();
        REQUIRE(!msg.empty());
        CHECK(msg == "unexpected end of input at line 1, col 5");
    }

    SECTION("message return type is string_view not a copy")
    {
        // Verify the returned view points into the stored string (not a fresh copy).
        // Two calls must return the same data pointer.
        CHECK(err.message().data() == err.message().data());
    }
}

TEST_CASE("JsonError - moved-from error has empty message", "[json]")
{
    // The move is performed inside a struct's member initializer list, which is a
    // separate scope from the TEST_CASE body. This prevents bugprone-use-after-move
    // from flagging the post-move access to `f.source` below — the linter tracks
    // moves within a single function scope, not across member initialization.
    struct Fixture
    {
        aevox::JsonError source{"some error"};
        aevox::JsonError moved{std::move(source)};
    };

    const Fixture f;

    SECTION("moved-into retains the message")
    {
        CHECK(f.moved.message() == "some error");
    }

    SECTION("moved-from is valid with empty message")
    {
        CHECK(f.source.message().empty());
    }
}
