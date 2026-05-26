# Coding And Formatting

This page is the detailed style reference for Aevox contributors. The short version: write C++23,
keep public headers dependency-clean, run the formatting script, and let CI enforce the rest.

## Formatting

Run the project formatter before every PR that changes C++ source, tests, or examples:

```bash
bash scripts/format.sh
```

The PR pipeline runs `clang-format-21` and fails if formatting changes are needed. Do not hand-format
large blocks to match personal preference; use the script so local output matches CI.

## C++ Style

| Use | Avoid |
|---|---|
| `std::expected<T, E>` for fallible operations | Exceptions as normal control flow |
| `std::optional<T>` for optional values | Nullable raw pointers |
| `std::span<T>` for non-owning buffers | `T* data, std::size_t size` pairs |
| `std::string_view` for non-owning text parameters | `const std::string&` when ownership is not needed |
| `std::make_unique` / `std::make_shared` | Raw owning `new` / `delete` |
| Coroutines for async public APIs | Callback-based public APIs |
| Concepts for template constraints | SFINAE or `std::enable_if` |
| `std::format` for string formatting | `printf`, `sprintf`, or manual formatting chains |
| `constexpr` constants | Macro constants |

Always write the `std::` prefix explicitly. Do not add `using namespace std;`.

## Public Header Rules

Files under `include/aevox/` are the public API and must remain implementation-agnostic:

- No Asio includes or Asio types.
- No llhttp, glaze, fmtlib, Catch2, nanobench, or other dependency types.
- No implementation-only helper types that force users to include private dependencies.
- Every public symbol needs a Doxygen block with ownership, thread-safety, move semantics, return
  behavior, and error behavior where applicable.

If a change needs a public API signature change, get architecture sign-off before implementing it.

## Error Handling

Return errors as values:

```cpp
auto result = make_value();
if (!result) {
    return std::unexpected(result.error());
}
```

Do not throw exceptions for expected validation, parsing, routing, configuration, or I/O errors.
Third-party exceptions may escape only when there is no practical value-based boundary, and that
case must be documented.

## Tests

Use Catch2 for unit and integration tests. Place tests by behavior area, not task number:

| Test kind | Location |
|---|---|
| Unit tests | `tests/unit/<module>/` |
| Integration tests | `tests/integration/<module>/` |
| Benchmarks | `tests/bench/<module>/` |

Networking integration tests use real loopback I/O. Do not mock Asio internals.

Test names must use an ASCII hyphen-minus (`-`) instead of an em dash. This keeps Catch2 filtering
portable on Windows.

## Documentation Style

Documentation snippets should be C++23 and should show checked `std::expected` handling where a
fallible API is used. Keep snippets focused and runnable. If a feature changes a public workflow,
update the relevant guide page and changelog entry in the same PR.

## Review Checklist

Before requesting review, confirm:

- Formatting script ran.
- Relevant CMake preset builds.
- Relevant CTest preset passes.
- `bash scripts/tidy.sh` passes for C++ changes.
- `mkdocs build --strict` passes for documentation changes.
- Public headers do not expose implementation dependencies.
- Docs and changelog reflect user-visible changes.
