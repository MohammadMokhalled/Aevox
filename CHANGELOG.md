# Changelog

All notable changes to Aevox are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Infrastructure
- GitHub Actions CI pipeline: Linux (GCC 13), macOS (Clang 16), Windows (MSVC 2022),
  ASan + UBSan, clang-format, and clang-tidy checks on every push and PR (AEV-007)
- CMake 3.27+ build system with C++23 baseline, Ninja and MSVC presets, vcpkg manifest
  mode dependency management, and ASan/UBSan/Clang-Tidy configurations (AEV-002)

### Added
- `aevox::Executor` abstract interface — async I/O execution layer with TCP acceptor loop,
  thread pool management, and graceful drain on shutdown (AEV-001)
- `aevox::Task<T>` coroutine return type — lazy, move-only, symmetric-transfer task with
  full `promise_type` defined in the public header using only std types (AEV-001)
- `aevox::ExecutorConfig` — thread count and drain timeout configuration for `make_executor()` (AEV-001)
- `aevox::make_executor()` factory — creates Asio-backed executor; Asio types confined to `src/net/` (AEV-001)
- `aevox::ExecutorError` error enum with `to_string()` (AEV-001)
- `aevox::ConnectionHandler` concept — constrains TCP connection handler callables (AEV-001)

---

## [0.0.1] — 2026-04-12

### Added
- Initial project skeleton
- mkdocs documentation scaffold
