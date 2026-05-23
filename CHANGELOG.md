# Changelog

All notable changes to Aevox are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Added
- Optional official gRPC plugin foundation: minimal `aevox::Plugin` lifecycle,
  `App::install(std::unique_ptr<Plugin>)`, raw-byte unary h2c gRPC API, optional
  `AEVOX_ENABLE_GRPC`/vcpkg feature wiring, docs, tests, and example (AEV-016)
- Nanobench performance suite workflow with HTTP keep-alive and router dispatch benchmarks,
  benchmark-stat helper tests, CTest `bench` registration, and architecture documentation (AEV-015)
- `examples/static-files` — runnable static asset serving example with bundled application-owned assets (AEV-014)
- `aevox::middleware::static_files()` — secure static file serving middleware with MIME type inference and directory traversal protection (AEV-014)
- `Request::trace_context()` — returns the W3C Trace Context `traceparent` header value for propagation to downstream services (AEV-012)
- W3C `traceparent` header auto-extraction: valid headers populate `trace_id` and `span_id` in request-correlated log entries automatically (AEV-012)
- `aevox::detail::parse_traceparent()` — internal W3C Trace Context Level 1 parser (AEV-012)
- `aevox::ErrorCategory` and `category(...)` helpers for module-specific error types, preserving precise errors while enabling broad logging and response classification (AEV-013)
- `aevox::JsonErrorCode`, `JsonError::code()`, and `to_string(JsonErrorCode)` for stable JSON error branching (AEV-013)
- Planned and architected AEV-013 `std::expected`-based error model task; implementation is approved by `Tasks/architecture/AEV-013-arch.md`.
- Simplified asynchronous logging subsystem (AEV-029): `LogConfig`, `LogDestination`, `LogStats`, global `aevox::log::*` functions, request-correlated `aevox::log::*` overloads, bounded queue, JSON/pretty formatting, and automatic request/response logging via `aevox::middleware::logger()`
- `Request::id()` — stable per-request correlation id for handlers and log output (AEV-029)
- TOML config parsing for simplified `[logging]` keys: `enabled`, `level`, `format`, `destination`, `file_path`, and `queue_capacity` (AEV-029)
- Planned human-gated DevEx work for Claude, Codex, and Windsurf assistant support, documentation refresh, and pipeline stabilization; validation baseline is required as the first implementation step.
- `aevox::WebSocket` — async WebSocket connection handle; `send()`, `send_nowait()`, `close()`, `subscribe()`, `publish()`, `topic()`, `remote_address()` (AEV-010)
- `WebSocket::send_nowait(std::string_view)` — synchronous fire-and-forget text send for use from non-coroutine `WebSocketHandler` callbacks (`on_message`, `on_close`); use `co_await ws.send(msg)` from coroutine contexts for error feedback (AEV-010)
- `aevox::WebSocketHandler` — callback aggregate: `on_open`, `on_message`, `on_close` (AEV-010)
- `aevox::WebSocketError` and `aevox::WebSocketErrorCode` — structured error type for WebSocket failures (AEV-010)
- `App::ws(path, handler)` — register a WebSocket route with automatic RFC 6455 handshake (AEV-010)
- `Request::is_websocket_upgrade()` — fast synchronous predicate for upgrade detection (AEV-010)
- `Request::upgrade_websocket()` — perform HTTP/1.1 to WebSocket upgrade; returns `WebSocket` handle (AEV-010)
- In-process topic bus supporting room-based broadcast via `WebSocket::subscribe()` / `publish()` (AEV-010)
- `Response::switching_protocols()` — internal 101 sentinel used by the WebSocket upgrade path (AEV-010)
- `aevox::JsonBackend` concept — compile-time contract for JSON backend implementations (AEV-009)
- `aevox::JsonError` — structured error type for JSON parse and serialization failures (AEV-009)
- `Request::json<T>()` — async JSON body deserialization returning `std::expected<T, JsonError>` (AEV-009)
- `Response::json(T)` — JSON serialization factory with automatic `Content-Type: application/json` (AEV-009)
- Default JSON backend: glaze, selectable via `AEVOX_JSON_BACKEND` CMake option (AEV-009)

### Changed
- Logger API simplified from request-owned `Logger` handles to free functions: use `aevox::log::info(req, "...")` for request-correlated messages and `aevox::log::info("...")` for global messages (AEV-029)
- `aevox::ConfigErrorDetail` now stores its detail fields privately; use `error_code()`, `error_message()`, and `error_key()` for access (AEV-028)
- `scripts/tidy.sh` now runs clang-tidy only against product code under `include/aevox/` and `src/`, matching the stricter product-code gate (AEV-028)
- `Executor::stop()` now honors calls made after `listen()` but before `run()`, causing the subsequent `run()` to return without starting worker threads; this fixes a Windows CI hang in the config file roundtrip integration test.
- `request_id` format changed from random per-thread ids to a monotonic 16-character lowercase hex id for predictable, low-cost log correlation (AEV-029)
- Logger middleware now emits one access entry per request through the simplified request-correlated log path, including `request_id`, `trace_id`, and `span_id` when present (AEV-029)
- `aevox::ConfigErrorDetail` now provides `error_code()`, `error_message()`, and `error_key()` accessors (AEV-013)
- WebSocket integration test architecture now requires Aevox-owned deadline-bounded test transport wrappers instead of raw Asio usage in test cases, preserving the no-public-Asio project goal and covering split-frame, close, upgrade, and broadcast scenarios without exposing backend networking details.
- CI upgraded to GCC 15 on Ubuntu 26.04; CMake and vcpkg baseline updated to latest stable
- CI split into PR pipeline (`pr.yml`) with clang-format-21 check and clang-tidy-21 check-only gate, and main pipeline (`main.yml`) with build and test only; old `ci.yml` deleted
- CI build matrix reduced to four entries (macOS removed): GCC 15 × {debug, release} on Linux, clang 21 × {debug, release} on Linux; codebase now verified against two independent compilers on every push
- macOS dropped from CI and supported platforms; macOS runners and Homebrew LLVM steps removed from both `pr.yml` and `main.yml`
- `asan-test-clang` job added to both `pr.yml` and `main.yml`: runs ASan + UBSan under clang 21 on Linux as an independent sanitizer pass complementing the existing GCC 15 ASan job
- vcpkg binary cache keys now include compiler ID (`gcc`, `clang-linux`, `msvc`) to prevent ABI-incompatible binary reuse across compilers

### Added
- `aevox::JsonError` — value-type error returned by JSON operations; carries a `std::string_view message()` accessor
- `aevox::JsonBackend<B>` — C++23 concept constraining pluggable JSON backends; requires `deserialize<T>()` and `serialize()` returning `std::expected<T, JsonError>` / `std::expected<std::string, JsonError>`
- `aevox::internal::GlazeBackend` — production JSON backend backed by glaze 3.6.1; satisfies `JsonBackend`; hidden in `src/json/` (no glaze types exposed in public headers)
- `aevox::Request::json<T>()` — async coroutine that deserializes the request body to `T`; returns `Task<std::expected<T, JsonError>>`
- `aevox::Response::json(const T&)` — factory that serializes `T` to JSON and returns a 200 response with `Content-Type: application/json`; falls back to 500 on serialization failure
- `aevox::AppConfig` — runtime-configurable fields: `port`, `host`, `backlog`, `max_body_size`, `request_timeout`, `max_header_count`, `max_read_bytes`; all have named `constexpr` defaults in `include/aevox/config.hpp`
- `aevox::App::create()` — factory that accepts an optional TOML config file path and returns `std::expected<App, ConfigErrorDetail>`; base defaults always apply when no file is provided
- `aevox::ConfigError` enum and `aevox::ConfigErrorDetail` struct — structured error type for config loading failures (`FileNotFound`, `ParseError`, `InvalidValue`)
- `aevox::to_string(ConfigError)` — human-readable error code string
- `aevox::ExecutorConfig` — `thread_count`, `cpu_pool_threads`, `drain_timeout` now exposed in public header with named `constexpr` defaults
- TOML config support via toml++ (confined to `src/config/`; no toml++ types in public headers)
- `aevox::Middleware` — composable async middleware pipeline with global and path-scoped registration; supports request/response interception and short-circuiting
- `aevox::App::use(F&&)` — registers global middleware invoked before every route handler
- `aevox::App::use(std::string_view, F&&)` — registers path-scoped middleware for requests matching a prefix
- `aevox::Request::set<T>()` and `aevox::Request::get<T>()` — per-request middleware context bag for passing typed values between middleware and handlers
- middleware-plugin example: demonstrates middleware authoring using lambda and struct styles with scoped path-prefix guards (AEV-024)

### Removed
- `aevox::Logger`, `aevox::log::global()`, public sink config variants, `LogField`, custom log backend concepts, logger backend internals, and `Request::logger()`; these were replaced by the smaller AEV-029 logging API
- `aevox::BodyParseError` — stub enum superseded by `aevox::JsonError`; the single value `NotImplemented` is no longer needed now that `Request::json<T>()` is fully implemented
- `aevox::SerializeError` — stub enum superseded by `aevox::JsonError`; serialization failures now surface as a 500 response with a structured JSON body

### Fixed
- Logger startup and shutdown logging errors no longer terminate `App::listen()`; startup failures
  fall back to disabled logging with a diagnostic, and flush failures are reported without aborting
  shutdown (AEV-029)
- Middleware pipeline dispatch: extracted immediately-invoked coroutine lambda (IIFE) into a named free function `dispatch_with_pipeline`; the IIFE pattern caused a dangling-reference hang when the connection handler was resumed from an Asio I/O callback on a different call-stack depth, leaving router-e2e tests blocked indefinitely

### Changed
- `aevox::AppConfig` field initialisers now reference named `constexpr` defaults (`kDefaultPort`, `kDefaultHost`, etc.) instead of bare literals
- `aevox::ExecutorConfig` field initialisers updated to use `kDefaultIoThreadCount`, `kDefaultCpuPoolThreads`, `kDefaultDrainTimeout`
- `aevox::TcpStream::read()` default argument changed from bare `65536` to `kDefaultMaxReadBytes`

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
