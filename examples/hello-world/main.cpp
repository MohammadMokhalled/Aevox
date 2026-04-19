#include <aevox/app.hpp>

#include <format>
#include <string_view>

int main()
{
    aevox::App app;

    app.get("/", [](aevox::Request&) { return aevox::Response::ok("Hello, World!"); });

    app.get("/hello/{name}", [](aevox::Request& req) {
        auto name = req.param<std::string_view>("name").value_or("stranger");
        return aevox::Response::ok(std::format("Hello, {}!", name));
    });

    app.get("/health", [](aevox::Request&) { return aevox::Response::ok("ok"); });

    app.listen(8080);
}
