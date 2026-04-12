// AEV-002: build-system smoke test
// ADD ref: Tasks/architecture/AEV-002-arch.md § Test Architecture
//
// Verifies:
//   - C++23 standard library headers are present and compilable
//   - aevox_core links without errors
//   - Compiler flags (-Wall -Wextra -Wpedantic -Werror) are accepted
//
// This file contains no test logic. Compilation and exit-0 is the test.
// If this file fails to compile or link, the build system is misconfigured.

#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string_view>

int main()
{
    // std::expected — C++23 error-as-value type (PRD §6.2)
    std::expected<int, std::string_view> e = 42;
    (void)e;

    // std::optional — C++23 nullable value (PRD §6.2)
    std::optional<std::uint64_t> o = 0ULL;
    (void)o;

    // std::span — C++23 non-owning buffer view (PRD §6.2)
    std::span<const std::byte> s;
    (void)s;

    // std::format — C++23 string formatting (PRD §6.8)
    [[maybe_unused]] auto msg = std::format("AEV-002 smoke: ok\n");

    return 0;
}
