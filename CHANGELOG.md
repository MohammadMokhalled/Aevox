# Changelog

All notable changes to Aevox are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Infrastructure
- CMake 3.27+ build system with C++23 baseline, Ninja and MSVC presets, vcpkg manifest
  mode dependency management, and ASan/UBSan/Clang-Tidy configurations (AEV-002)

### Added
- `aevox::Executor` abstract interface — async I/O execution layer (AEV-001)
- `aevox::Task<T>` coroutine return type — public async primitive (AEV-001)
- `aevox::make_executor()` factory — creates Asio-backed executor (AEV-001)
- `aevox::ExecutorError` error enum with `to_string()` (AEV-001)

---

## [0.0.1] — 2026-04-12

### Added
- Initial project skeleton
- mkdocs documentation scaffold
