# CLAUDE.md — Aevox Framework AI Rules

This file is loaded by Claude Code at the start of every session. It governs all AI behavior in this repository. Follow every rule here without exception. When a rule conflicts with a general instruction, this file wins.

---

## 1. Project Overview

**Aevox** is a high-performance, modern C++23 web framework. Target: 10M+ HTTP/1.1 requests/second on commodity hardware with a 5-line API surface.

**Source of truth:** `ProductRequirement/cpp_web_framework_prd.md` (v1.7). Read it before making any design or product decision. When in doubt, the PRD decides.

**Active language standard:** C++23. Supported compilers: GCC 13+ (Linux), MSVC 2022+ (Windows). macOS is not a supported platform.

---

## 2. Three-Role Skill System

Work in this project flows through three distinct roles. Each has a dedicated skill. **Never mix role responsibilities.** Invoke the correct skill for the task.

| Role | Skill | Trigger |
|---|---|---|
| **TPO** — Technical Product Owner | `/tpo` | Roadmap, task creation, epic breakdown, sprint planning, backlog |
| **Architect** | `/architect` | Module design, ADD creation, architectural review, layer decisions |
| **Developer** | `/developer` | Implementation, tests, documentation, bug fixes, self-review |

### Skill invocation order

A task must exist before it is architected. An ADD must exist before it is implemented.

```
/tpo → creates Tasks/tasks/AEV-NNN-*.md
/architect → creates Tasks/architecture/AEV-NNN-arch.md
/developer → implements following the ADD
```

If asked to implement a task that has no ADD: **stop**. Report the missing ADD and prompt the user to run `/architect` first.

If the ADD has open issues in Section 10: **stop**. Do not implement past an unresolved open issue without architect sign-off recorded in the ADD.

---

## 3. Absolute Architectural Invariants

These rules are **never negotiable**. CI enforces them automatically. Code review rejects any PR that violates them. If a task cannot be implemented within these constraints, flag it — do not silently work around them.

### 3.1 No Asio Types in Public Headers

No file under `include/aevox/` may include any Asio header or reference any Asio type.

```
# BANNED in include/aevox/**/*.hpp
#include <asio.hpp>
#include <asio/io_context.hpp>
asio::io_context
asio::awaitable
asio::thread_pool
```

The `aevox::Executor` interface in `include/aevox/executor.hpp` is the only networking abstraction the public surface exposes. Everything Asio lives in `src/net/`.

**Why:** When `std::net` standardizes (C++29), replacing Asio requires only swapping `src/net/` — zero application-code changes. One leaked Asio type breaks this migration path.

### 3.2 No Third-Party Library Types in Public Headers

No file under `include/aevox/` may expose types from llhttp, glaze, spdlog, fmtlib, or any other dependency. Every external library is an implementation detail of `src/`.

### 3.3 No Raw `new` / `delete` Anywhere

All heap allocation goes through `std::make_unique` or `std::make_shared`. Raw owning pointers (`T*`) are banned throughout the codebase.

### 3.4 No Exceptions for Control Flow

`std::expected<T, E>` is the error model. Exceptions are allowed to propagate from third-party code but must never be used as a primary control-flow mechanism in Aevox code.

### 3.5 Layer Diagram Must Be Respected

```
[ Application Code ]
        ↓  include/aevox/
[ Router + Middleware + Request/Response ]
        ↓  src/http/, src/json/, src/log/
[ HttpParser | JsonBackend | LogBackend ]
        ↓  include/aevox/executor.hpp
[ aevox::Executor (abstract) ]
        ↓  src/net/ — Asio types live only here
[ AsioExecutor ]
        ↓
[ OS I/O: io_uring / kqueue / IOCP ]
```

Nothing flows upward through this diagram. A layer may only depend on layers below it.

---

## 4. C++23 Mandatory Patterns (PRD §6.9)

Required everywhere — internals, public API, tests, examples. Zero tolerance.

| Use | Never use |
|---|---|
| `std::expected<T, E>` for errors | Exceptions for control flow |
| `std::optional<T>` for nullable values | Nullable raw pointers `T*` |
| `std::span<T>` for non-owning buffers | `T* data, size_t len` pairs |
| `std::string_view` for string params | `const std::string&` |
| `std::make_unique` / `std::make_shared` | `new` / `delete`, raw owning `T*` |
| Coroutines (`co_await`) for async | Callbacks in public API |
| Concepts (`requires`) for constraints | SFINAE / `enable_if` |
| `std::format` | `printf` / `sprintf` / `+` concatenation |
| `std::ranges` / range-for | Raw index loops, `std::endl` |
| `[[nodiscard]]` on all `expected`/`optional` returns | Unannotated fallible returns |
| `static_cast<T>` | C-style casts `(T)x`, `reinterpret_cast` (rare: add comment) |
| `constexpr` | `#define` constants |
| `std::any` or typed templates | `void*` |
| Explicit `std::` prefix always | `using namespace std;` |
| Dependency injection | Mutable globals |
| ASCII hyphen-minus ` - ` in `TEST_CASE` names | Em dash `—` (U+2014) — garbles in MSVC Windows-1252, breaks Catch2 name matching |

---

## 5. Compiler Flags (CI Baseline)

All code must compile and pass under these flags. Zero warnings are tolerated.

```cmake
target_compile_options(aevox_core PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)
target_compile_features(aevox_core PUBLIC cxx_std_23)
set(CMAKE_CXX_EXTENSIONS OFF)  # strict standard only — no GNU extensions
```

CI additionally runs AddressSanitizer and UndefinedBehaviorSanitizer on the full test suite.

---

## 6. Directory Layout

```
aevox/
├── include/aevox/          ← PUBLIC API ONLY — no implementation, no Asio
│   ├── app.hpp
│   ├── request.hpp
│   ├── response.hpp
│   ├── router.hpp
│   ├── middleware.hpp
│   ├── executor.hpp        ← ONLY networking abstraction exposed publicly
│   ├── task.hpp            ← aevox::Task<T> coroutine return type
│   └── {module}/
├── src/                    ← IMPLEMENTATION — never included by users
│   ├── net/                ← ALL Asio code lives here and only here
│   ├── http/               ← llhttp integration
│   ├── router/
│   ├── json/               ← glaze (or alt) backend
│   └── log/                ← spdlog (or alt) backend
├── tests/
│   ├── unit/{module}/
│   ├── integration/{module}/
│   └── bench/{module}/
├── examples/
├── docs/                   ← MkDocs documentation site
├── Tasks/                  ← TPO + Architect + Developer artifacts
│   ├── tasks/              ← AEV-NNN task files (TPO output)
│   ├── architecture/       ← ADD files (Architect output)
│   ├── progress/           ← Developer logs (Developer output)
│   ├── epics/
│   ├── ROADMAP.md
│   └── BACKLOG.md
├── ProductRequirement/
│   └── cpp_web_framework_prd.md   ← SOURCE OF TRUTH — read before deciding anything
├── CMakeLists.txt
├── vcpkg.json
└── CLAUDE.md               ← this file
```

---

## 7. File Naming Conventions

| Artifact | Convention | Example |
|---|---|---|
| Task files | `AEV-NNN-{slug}.md` | `AEV-001-asio-async-io-core.md` |
| ADD files | `AEV-NNN-arch.md` | `AEV-001-arch.md` |
| Developer logs | `AEV-NNN-devlog.md` | `AEV-001-devlog.md` |
| Public headers | `{class-or-module}.hpp` | `executor.hpp`, `request.hpp` |
| Source files | `{class-or-module}.cpp` | `asio_executor.cpp` |
| Unit tests | `{module}-{what}.cpp` (no task ID prefix) | `executor-contract.cpp`, `http-parser.cpp` |
| Integration tests | `{module}-{what}.cpp` (no task ID prefix) | `tcp-accept.cpp`, `http-parse-roundtrip.cpp` |
| Benchmarks | `{subject}-{metric}.cpp` (no task ID prefix) | `accept-throughput.cpp`, `executor-throughput.cpp` |

Task IDs are sequential and zero-padded: `AEV-001`, `AEV-002`, etc. Always scan existing tasks to find the next available ID before creating one.

> **AEV-NNN prefixes apply exclusively to files inside `Tasks/`** (task files, ADD files, developer logs, epics). Source files, headers, test files, benchmarks, examples, and documentation pages must never include a task ID in their name. Use the lint script `scripts/check_task_ids.sh` to verify compliance.

---

## 8. In-Code Documentation Rules

Every public symbol in `include/aevox/` requires a full Doxygen block. No exceptions, including for "obvious" functions.

```cpp
/**
 * @brief One-sentence description of purpose.
 *
 * Extended description if needed — 2–4 sentences.
 *
 * @tparam T  Template parameter description.
 * @param  x  Parameter description.
 * @return    What is returned and under what conditions (include error cases).
 * @note      Thread-safety contract. Move semantics. Performance characteristics.
 * @throws    Only if exceptions can escape (rare — note if noexcept).
 */
[[nodiscard]] std::expected<Result, Error>
my_function(std::string_view x) noexcept;
```

Internal code in `src/`: block comments for non-obvious logic. Single-line `//` for anything that isn't self-explanatory. Do not over-comment trivially readable code.

Every class must document:
- Thread-safety contract
- Move semantics (moved-from state)
- Ownership model

---

## 9. Test Requirements (Non-Negotiable)

A task is not Done until tests exist, pass, and CI is green.

| Test type | When required | Framework | Location |
|---|---|---|---|
| Unit tests | Every task | Catch2 | `tests/unit/{module}/` |
| Integration tests | Any task touching I/O, network, middleware | Catch2 (real Asio — no mocks) | `tests/integration/{module}/` |
| Benchmarks | Any task tagged `#networking` or `#performance` | nanobench | `tests/bench/{module}/` |

Minimum coverage per public function:
- Happy path (correct inputs → correct output)
- At least one error path (`std::expected` error branch exercised)
- Edge cases (boundary values, empty inputs)

For networking code additionally:
- Thread-safety test (multiple `std::jthread` workers + `std::barrier`)
- Cancellation test (clean shutdown mid-operation)

Mocking Asio internals is prohibited in integration tests. Use a real `asio::io_context` with loopback sockets.

---

## 10. Documentation Output (MkDocs)

Every task that adds or modifies public API must produce a mkdocs page in `docs/api/{module}.md`. The `mkdocs.yml` nav section must be updated to include the new page.

Never delete existing entries from `mkdocs.yml`. Never delete existing `docs/` pages without replacing them.

---

## 11. CHANGELOG Rules

Every task that modifies a public header (`include/aevox/`) must add an entry to `CHANGELOG.md` under `[Unreleased]`. Format:

```markdown
## [Unreleased]
### Added
- `aevox::Executor::listen()` — bind and accept connections on a TCP port

### Changed
- `aevox::Task<T>` now requires `T` to satisfy `std::movable`
```

### Internal directory changes must be reflected in CHANGELOG

`Tasks/` and `ProductRequirement/` are gitignored and never committed. Any significant change originating in those directories — a new task, a new ADD, a PRD update, a roadmap change — must still produce a `CHANGELOG.md` entry so the public repo retains a traceable record of what changed and why.

**Rule:** When work in `Tasks/` or `ProductRequirement/` produces an observable effect on the public codebase (new API, changed behaviour, new module, dependency update), add a CHANGELOG entry describing that effect. Do not reference internal file paths (e.g. `Tasks/tasks/AEV-003-*.md`) in the entry — describe the public-facing result only.

**Examples:**

```markdown
### Added
- `aevox::Router` path-parameter extraction (AEV-003)

### Changed
- PRD §4.2 revised throughput target to ≥ 2M req/s; architecture updated accordingly
```

---

## 12. Dependencies — Permitted and Forbidden

All external libraries are vendored via vcpkg and version-locked in `vcpkg.json`.

| Library | Permitted in | Forbidden in |
|---|---|---|
| Asio (standalone) | `src/net/` only | `include/aevox/` — absolute ban |
| llhttp | `src/http/` only | `include/aevox/` — absolute ban |
| glaze | `src/json/` only | `include/aevox/` — absolute ban |
| spdlog | `src/log/` only | `include/aevox/` — absolute ban |
| fmtlib | `src/` only | `include/aevox/` (use `std::format` there) |
| Catch2 / nanobench | `tests/` only | `src/`, `include/aevox/` |

Application code (user's handlers) must never import any of the above directly. Violations are code review rejections.

---

## 13. Architectural Decision Records

All ADRs live in the PRD (§19). Before making any design choice that touches layer boundaries, coroutine scheduling, or third-party library usage, check whether an ADR already covers it.

Key ADRs:

| ADR | Rule |
|---|---|
| ADR-1 | Asio is hidden behind `aevox::Executor`. Enables `std::net` swap in C++29. |
| ADR-2 | `aevox::pool()` uses a separate CPU thread pool — never the I/O pool. |
| ADR-3 | Coroutines are pinned to their originating thread by default (v0.1). |
| ADR-4 | Regex routing is opt-in, not available in v0.1. |
| ADR-5 | C++20 modules are opt-in in v0.4. |
| ADR-6 | HTTP/2 is permanently out of scope — delegated to Nginx/Caddy. |

---

## 14. Performance Targets

The architecture must support these without design changes. Flag any implementation that risks these targets.

| Metric | Target |
|---|---|
| HTTP/1.1 throughput | ≥ 1,000,000 req/s (single machine, keep-alive) |
| WebSocket messages/sec | ≥ 5,000,000 |
| Concurrent coroutines | ≥ 1,000,000 at ≤ 2KB each |
| p99 latency | ≤ 1ms under load (no DB) |
| p999 latency | ≤ 5ms |
| Startup time | ≤ 50ms |
| Compile time (hello world) | ≤ 2 seconds |
| Binary size (hello world, static) | ≤ 2MB |

---

## 15. What Is Explicitly Out of Scope

Do not implement, design, or suggest adding:
- HTTP/2 or HTTP/3 in the Aevox core
- TLS/SSL termination
- ORM or database access
- Certificate management
- Reverse proxy configuration
- gRPC in the core (plugin only, planned for v0.3)

---

## 16. AI Behavioral Rules

These apply to every response in this repository.

### Search before reading
Before opening files speculatively, use Grep to find relevant symbols/patterns and Glob to find relevant files. Read only files that directly answer the question. Never read an entire file when a targeted search would suffice.

### Read before modifying
Never propose changes to code you have not read. Use the Read tool before editing. Understand existing patterns before suggesting modifications.

### Skill boundaries
Do not mix role work in a single response. A response that designs an architecture (Architect) must not also implement it (Developer). Use the correct skill.

### No speculative abstractions
Do not add helpers, utilities, or abstractions for hypothetical future requirements. Three similar lines of code is better than a premature abstraction. Build exactly what the task requires — no more.

### No unrequested improvements
Do not add docstrings, type annotations, error handling, or refactoring to code that was not part of the requested change. A bug fix touches only the bug. A feature touches only the feature.

### Deviations from ADD require reporting
If you discover the ADD is wrong, incomplete, or impossible to implement:
1. Record it in the Developer Log
2. If it affects public API or file structure: stop and report to the user before continuing
3. If it is a minor internal detail: document and continue

### Confirm before irreversible actions
Ask before: deleting files, pushing commits, modifying `CMakeLists.txt` in ways that change the build graph, or any action affecting CI configuration.

### The PRD is the final arbiter
When a user request conflicts with the PRD, implement the request and explicitly flag the conflict so the user can decide whether to update the PRD. Never silently abandon a PRD constraint.

### Run clang-format after every code change

Run `bash scripts/format.sh` after editing any `.cpp`/`.hpp` file and before any build.
