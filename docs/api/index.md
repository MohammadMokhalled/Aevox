# API Reference

Aevox's public API lives entirely under `include/aevox/`. No internal headers are part of the public surface.

## Modules

| Module | Header | Description |
|---|---|---|
| [Executor](executor.md) | `<aevox/executor.hpp>` | Async I/O execution layer — thread pool, accept loops, graceful drain |
| [Task](task.md) | `<aevox/task.hpp>` | Coroutine return type for all async operations |
| [Async Helpers](async.md) | `<aevox/async.hpp>` | `pool()`, `sleep()`, `when_all()` — CPU offload, timers, concurrent fan-out |
| App | `<aevox/app.hpp>` | Top-level application object _(coming in v0.1)_ |
| Request | `<aevox/request.hpp>` | Incoming HTTP request _(coming in v0.1)_ |
| Response | `<aevox/response.hpp>` | Outgoing HTTP response _(coming in v0.1)_ |
| Router | `<aevox/router.hpp>` | URL routing _(coming in v0.2)_ |
| Middleware | `<aevox/middleware.hpp>` | Middleware pipeline _(coming in v0.2)_ |

## C++23 Requirement

All Aevox headers require **C++23** or later:

```cmake
target_compile_features(your_target PRIVATE cxx_std_23)
```

## Error Handling Convention

All fallible operations return `std::expected<T, ErrorEnum>` and are marked `[[nodiscard]]`. Always check the result:

```cpp
auto result = executor->listen(8080, handler);
if (!result) {
    std::println(stderr, "listen failed: {}", aevox::to_string(result.error()));
    return 1;
}
```
