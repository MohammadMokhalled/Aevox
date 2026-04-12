# Changelog

All notable changes to Aevox are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

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
