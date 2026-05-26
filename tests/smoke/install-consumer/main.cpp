#include <aevox/response.hpp>

int main()
{
    auto response = aevox::Response::ok("installed");
    return response.status_code() == 200 ? 0 : 1;
}
