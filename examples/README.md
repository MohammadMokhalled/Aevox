# Aevox Examples

Runnable applications demonstrating the Aevox public API. Each example is a
self-contained project under its own subdirectory.

Examples are built as part of the default build:

```bash
export VCPKG_ROOT=$HOME/vcpkg
cmake --preset default
cmake --build --preset default
```

---

## Available Examples

| Example | Binary | What it shows |
|---|---|---|
| [hello-world](hello-world/) | `hello-world` | Static routes, named path parameters, clean shutdown |

---

## Adding a New Example

1. Create `examples/<name>/main.cpp` and `examples/<name>/CMakeLists.txt`.
2. Add `add_subdirectory(<name>)` to `examples/CMakeLists.txt`.
3. Follow the pattern in `hello-world/CMakeLists.txt` — link `aevox_core` privately,
   apply `${AEVOX_COMPILE_OPTIONS}`, and add `${CMAKE_SOURCE_DIR}/src` to the
   private include path (required by `router_impl.hpp`).
