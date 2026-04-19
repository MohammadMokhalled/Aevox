# Examples

Runnable applications that demonstrate the Aevox public API. Each example is a
self-contained binary under `examples/` in the repository.

## Available examples

| Example | What it shows |
|---|---|
| [Hello World](hello-world.md) | Static routes, named path parameters, clean shutdown — the complete v0.1 API in one file |

## Building examples

Examples are not built by default. Use `--target <name>` explicitly:

=== "Linux"
    ```bash
    export VCPKG_ROOT=$HOME/vcpkg
    cmake --preset default
    cmake --build --preset default --target hello-world
    ```

=== "Windows"
    ```bash
    cmake --preset windows-msvc
    cmake --build --preset windows-msvc-debug --target hello-world
    ```
