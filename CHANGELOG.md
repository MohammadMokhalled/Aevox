# Changelog

All notable changes to Aevox are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Added
- `/document` skill definition in `CLAUDE.md` §17 covering User Guide creation, Architecture and Concepts pages, and consistency refactor pass; added to the skill table and invocation chain in §2
- `docs/guide/` section with 7 pages: installation, first HTTP server, routing, request and response, async patterns, error handling, and guide index
- `docs/architecture/` expanded with 5 new pages: executor concepts, router path-matching design, coroutines and `Task<T>`, error model, and layer diagram
- All new architecture pages include at least one Mermaid diagram

### Changed
- `docs/architecture/index.md` updated with links to new architecture sub-pages
- `docs/api/index.md` corrected: App, Router, Request, Response now listed as available (v0.1 is complete)
- `docs/api/executor.md` and other pages cleaned of internal `Tasks/` path references
- Consistency refactor across all `docs/` pages: normalized heading hierarchy, language-tagged code blocks, "See Also" sections added where missing
- Removed AEV-xxx task ID references from all file names, CMake targets, in-code comments, and documentation outside the `Tasks/` folder; lint script added at `scripts/check_task_ids.sh`
- `CLAUDE.md` Section 7 File Naming Conventions updated: test, integration test, and benchmark files now use slug-only naming (no task ID prefix); AEV-NNN prefixes apply exclusively to files inside `Tasks/`

### Added
- `examples/hello-world` — end-to-end Hello World demonstrating the v0.1 public API: static routes, named path parameters, and clean SIGINT shutdown (AEV-020)
- `aevox::Router` — radix-trie HTTP router with static, named-parameter, and
  wildcard segment matching; O(depth) dispatch; thread-safe after registration (AEV-004)
- `aevox::App` and `aevox::AppConfig` — top-level server entry point; owns Router
  and Executor; registers routes via `get/post/put/patch/del/options`; starts
  listening with `listen(port)` which blocks until `stop()` or SIGINT/SIGTERM (AEV-004)
- `aevox::RouteError` enum (`NotFound`, `MethodNotAllowed`, `BadParam`) for dispatch
  error introspection in tests (AEV-004)
- `aevox::Response::method_not_allowed()` factory — returns 405 response; caller
  adds the `Allow` header via `.header("Allow", value)` (AEV-004)
- `Router::group(prefix)` — returns a child Router scoped to a shared path prefix (AEV-004)
- `aevox::Request` and `aevox::Response` public API types with typed path-parameter
  extraction (`param<T>`), case-insensitive header lookup, raw body access (`body()`),
  middleware context store (`set<T>`/`get<T>`), fluent response builder
  (`content_type()`/`header()`), factory methods (`ok`/`created`/`not_found`/
  `bad_request`/`unauthorized`/`forbidden`/`json`/`stream`), and JSON stub
  (real implementation wired in AEV-009) (AEV-005)
- `aevox::HttpMethod` enum (`GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `HEAD`,
  `OPTIONS`, `Unknown`) and `to_string(HttpMethod)` (AEV-005)
- `aevox::ParamError`, `aevox::BodyParseError`, `aevox::SerializeError` error
  enums for typed extraction and JSON parse/serialize paths (AEV-005)
- `include/aevox/concepts.hpp` — `ParamConvertible`, `Serializable`,
  `Deserializable` concept stubs; real glaze-backed constraints wired in AEV-009 (AEV-005)

### Infrastructure
- GitHub Actions CI pipeline: Linux (GCC 13), Windows (MSVC 2022), ASan + UBSan,
  and clang-format on every PR (AEV-007)
- CMake 3.27+ build system with C++23 baseline, Ninja and MSVC presets, vcpkg manifest
  mode dependency management, and ASan/UBSan configurations (AEV-002)

### Changed
- Dropped macOS and Clang as supported/CI-tested configurations; supported compilers are
  now GCC 13+ (Linux) and MSVC 2022+ (Windows) only
- Removed `lint` CMake preset (Clang-Tidy CI job); clang-format check is retained

### Changed
- `aevox::Task<T>` and `aevox::Task<void>` are now `[[nodiscard]]` — the compiler will
  warn when a coroutine return value is discarded without being `co_await`-ed (AEV-006)

### Changed
- `aevox::ConnectionHandler` concept now requires `Task<void>(std::uint64_t, TcpStream)`;
  `TcpStream` is passed alongside `conn_id` to every connection handler (AEV-003)
- `aevox::Executor::listen()` now passes an owned `TcpStream` alongside `conn_id` to the
  handler; existing handlers must add the `TcpStream` parameter (AEV-003)

### Added
- `aevox::TcpStream` — move-only async TCP stream; `read()` and `write()` are
  `co_await`-able coroutines; Asio socket confined to `src/net/asio_tcp_stream.cpp` (AEV-003)
- `aevox::IoError` — `Eof`, `Cancelled`, `Reset`, `Timeout`, `Unknown` error codes for
  TCP I/O operations (AEV-003)
- `aevox::detail::HttpParser` — internal HTTP/1.1 request parser backed by llhttp;
  incremental feed model via `feed(span<const byte>)` returning `expected<ParsedRequest, ParseError>`;
  llhttp types confined to `src/http/http_parser.cpp` (AEV-003)
- `aevox::pool(fn)` — dispatches CPU-bound callable to dedicated CPU thread pool, returns
  `Task<R>`; suspends calling coroutine without blocking I/O thread (AEV-006)
- `aevox::sleep(duration)` — non-blocking coroutine timer; suspends for at least `duration`
  without occupying an I/O thread (AEV-006)
- `aevox::when_all(tasks...)` — concurrent fan-out over ≥ 2 non-void `Task<T>` values;
  returns `Task<std::tuple<Ts...>>` when all tasks complete (AEV-006)
- `aevox::ExecutorConfig::cpu_pool_threads` — configures the dedicated CPU thread pool size
  (default 4; 0 disables dedicated pool) (AEV-006)
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
