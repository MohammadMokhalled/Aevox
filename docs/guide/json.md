# JSON

Aevox provides first-class support for JSON through two public operations: deserializing a request body into a C++ struct and serializing a C++ struct into a JSON response. No annotations or schema files are required — any aggregate struct with public fields works automatically.

## Parsing a Request Body

Use `co_await req.json<T>()` inside a coroutine handler to deserialize the request body. The method returns `std::expected<T, aevox::JsonError>`. Always check the result before using it.

```cpp
#include <aevox/app.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>

// Any aggregate struct with public fields is automatically reflectable.
struct CreateUserRequest {
    std::string name;
    std::string email;
    int         age{};
};

int main() {
    aevox::App app;

    app.post("/users", [](aevox::Request& req) -> aevox::Task<aevox::Response> {
        auto result = co_await req.json<CreateUserRequest>();
        if (!result) {
            // result.error() is aevox::JsonError
            co_return aevox::Response::bad_request(
                std::string{result.error().message()});
        }
        // *result is a fully populated CreateUserRequest
        co_return aevox::Response::ok(
            std::format("Created user: {}", result->name));
    });

    app.listen(8080);
}
```

The error branch fires for malformed JSON, type mismatches (e.g. a string where an `int` is expected), and missing required fields. The `aevox::JsonError::message()` string contains a human-readable description suitable for logging or a 400 response body.

## Sending a JSON Response

Use `aevox::Response::json(value)` to serialize any aggregate struct to a JSON response with `Content-Type: application/json` and status 200.

```cpp
struct UserResponse {
    std::string id;
    std::string name;
    int         age{};
    bool        active{};
};

app.get("/users/:id", [](aevox::Request& req) -> aevox::Task<aevox::Response> {
    auto id = req.param<std::string>("id");
    if (!id) {
        co_return aevox::Response::bad_request("missing id");
    }
    UserResponse user{.id = *id, .name = "Alice", .age = 30, .active = true};
    co_return aevox::Response::json(user);
});
```

If serialization fails (which is rare for well-formed structs), `Response::json` returns a 500 response with a JSON error body rather than throwing. You do not need to handle this case explicitly in typical usage.

## Combining Parse and Serialize

A common pattern: parse the request body, process it, and return a JSON response.

```cpp
struct EchoRequest {
    std::string message;
};

struct EchoResponse {
    std::string original;
    std::string reversed;
    int         length{};
};

app.post("/echo", [](aevox::Request& req) -> aevox::Task<aevox::Response> {
    auto body = co_await req.json<EchoRequest>();
    if (!body) {
        co_return aevox::Response::bad_request(
            std::string{body.error().message()});
    }

    auto reversed = body->message;
    std::ranges::reverse(reversed);

    co_return aevox::Response::json(EchoResponse{
        .original = body->message,
        .reversed = std::move(reversed),
        .length   = static_cast<int>(body->message.size())});
});
```

## Calling json() Multiple Times

`json<T>()` re-parses the body on each call — the body bytes remain stable for the full request lifetime. Calling with two different types in the same handler is supported. If you need the same result more than once, cache it:

```cpp
app.post("/multi", [](aevox::Request& req) -> aevox::Task<aevox::Response> {
    auto parsed = co_await req.json<CreateUserRequest>();
    if (!parsed) {
        co_return aevox::Response::bad_request(
            std::string{parsed.error().message()});
    }
    // Use *parsed multiple times — no re-parse cost.
    validate(*parsed);
    persist(*parsed);
    co_return aevox::Response::json(*parsed);
});
```

## Custom JSON Backends

The default backend is `aevox::internal::GlazeBackend`, powered by glaze. If you need a different backend, implement the `aevox::JsonBackend<B>` concept:

```cpp
#include <aevox/json_backend.hpp>

struct MyBackend {
    template <typename T>
    [[nodiscard]] std::expected<T, aevox::JsonError>
    deserialize(std::string_view input) const {
        // your deserialization logic
    }

    template <typename T>
    [[nodiscard]] std::expected<std::string, aevox::JsonError>
    serialize(const T& value) const {
        // your serialization logic
    }
};

static_assert(aevox::JsonBackend<MyBackend>);
```

Both methods must be callable on a `const` backend instance — backends must carry no mutable state. The `static_assert` confirms your backend satisfies the contract at compile time.

## See Also

- [API Reference — JSON](../api/json.md) — `JsonError`, `JsonBackend`, and related types
- [Request and Response](request-response.md) — full request and response API
- [Error Handling](error-handling.md) — working with `std::expected` error branches
