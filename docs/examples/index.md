# Examples

Runnable applications that demonstrate the Aevox public API. Each example is a
self-contained binary under `examples/` in the repository.

## Available examples

| Example | What it shows |
|---|---|
| [Hello World](hello-world.md) | Static routes, named path parameters, clean shutdown — the complete v0.1 API in one file |

## Building examples

Examples are built as part of the default build:

=== "Linux"
    ```bash
    export VCPKG_ROOT=$HOME/vcpkg
    cmake --preset default
    cmake --build --preset default
    ```

=== "Windows"
    ```bash
    cmake --preset windows-msvc
    cmake --build --preset windows-msvc-debug
    ```
