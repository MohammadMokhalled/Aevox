# Changelog

All notable changes to Aevox are documented here.  
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).  
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Infrastructure

- **GitHub Actions CI pipeline** — Linux (GCC 13), Windows (MSVC 2022), AddressSanitizer + UndefinedBehaviorSanitizer, and clang-format check on every PR (AEV-007)
- **CMake 3.27+ build system** — C++23 baseline, Ninja and MSVC presets, vcpkg manifest-mode dependency management, and ASan/UBSan configurations (AEV-002)

### Added

- **`aevox::TcpStream`** — move-only async TCP stream; `read()` and `write()` are `co_await`-able coroutines returning `std::expected`; Asio socket confined to `src/net/asio_tcp_stream.cpp` (AEV-003)
- **`aevox::IoError`** — `Eof`, `Cancelled`, `Reset`, `Timeout`, `Unknown` error codes for TCP I/O operations (AEV-003)
- **`aevox::detail::HttpParser`** — internal HTTP/1.1 request parser backed by llhttp; incremental feed model via `feed(span<const byte>)` returning `expected<ParsedRequest, ParseError>`; llhttp types confined to `src/http/http_parser.cpp` (AEV-003)
- **`aevox::pool(fn)`** — dispatches a CPU-bound callable to a dedicated CPU thread pool, returns `Task<R>`; suspends the calling coroutine without blocking an I/O thread (AEV-006)
- **`aevox::sleep(duration)`** — non-blocking coroutine timer; suspends for at least `duration` without occupying an I/O thread (AEV-006)
- **`aevox::when_all(tasks...)`** — concurrent fan-out over ≥ 2 non-void `Task<T>` values; returns `Task<std::tuple<Ts...>>` when all tasks complete (AEV-006)
- **`aevox::ExecutorConfig::cpu_pool_threads`** — configures the dedicated CPU thread pool size (default 4; `0` disables the dedicated pool) (AEV-006)
- **`aevox::Executor`** — abstract async I/O execution interface with TCP acceptor loop, thread pool management, and graceful drain on shutdown (AEV-001)
- **`aevox::Task<T>`** — lazy, move-only, `[[nodiscard]]` coroutine return type with symmetric transfer and full `promise_type` using only std types (AEV-001)
- **`aevox::ExecutorConfig`** — thread count and drain timeout configuration for `make_executor()` (AEV-001)
- **`aevox::make_executor()`** — factory that creates an Asio-backed executor; Asio types confined to `src/net/` (AEV-001)
- **`aevox::ExecutorError`** — `bind_failed`, `listen_failed`, `accept_failed`, `already_running`, `not_running` with `to_string()` (AEV-001)
- **`aevox::ConnectionHandler`** — concept constraining TCP connection handler callables (AEV-001)

### Changed

- **`aevox::ConnectionHandler`** concept now requires `Task<void>(std::uint64_t, TcpStream)` — `TcpStream` is passed alongside `conn_id` to every connection handler (AEV-003)
- **`aevox::Executor::listen()`** now passes an owned `TcpStream` alongside `conn_id` to the handler; existing handlers must add the `TcpStream` parameter (AEV-003)
- **`aevox::Task<T>` and `aevox::Task<void>`** are now `[[nodiscard]]` — the compiler warns when a coroutine return value is discarded without being `co_await`-ed (AEV-006)
- **Dropped macOS and Clang** as supported/CI-tested configurations; supported compilers are now GCC 13+ (Linux) and MSVC 2022+ (Windows) only
- **Removed `lint` CMake preset** (Clang-Tidy CI job); clang-format check is retained

---

## [0.0.1] — 2026-04-12

### Added

- Initial project skeleton
- MkDocs documentation scaffold
