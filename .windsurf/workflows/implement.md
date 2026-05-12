---
description: Implement — structured implementation skill for the Aevox framework
---

# /implement — Implementation

Use this skill when given a task ID or feature description to implement in the Aevox C++23 web framework. Drives code → tests → self-review in one focused cycle.

## Triggers

- "implement AEV-NNN"
- "write the code for"
- "build this"
- "start implementing"
- "code this up"

## What It Does

1. Reads the ADD (`Tasks/architecture/AEV-NNN-arch.md`).
2. Stops if the ADD is missing or Section 10 has unresolved open issues.
3. Reads the TPO task for acceptance criteria and constraints.
4. Ensures a feature branch is checked out and updates the developer log.
5. Implements in dependency order: public headers → internal headers → source → unit tests → integration tests → benchmarks (if required).
6. Updates `CHANGELOG.md` if public API changed.

## Constraints

- Never mix design work with implementation.
- Every public symbol needs a complete Doxygen block before moving to the next file.
- Minimum test coverage per public function: happy path + error branch + edge case.
- No raw `new`/`delete`, no exceptions for control flow, no Asio in public headers.
- Use `std::expected`, `std::optional`, `std::span`, `std::string_view` everywhere.
