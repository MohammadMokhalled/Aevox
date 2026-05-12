---
description: Developer — implementation, tests, documentation, bug fixes
---

# /developer — Developer

Use this skill for implementing tasks in the Aevox C++23 web framework.

## Triggers

- "implement AEV-NNN"
- "write the code for"
- "build this"
- "start implementing"
- "code this up"

## What It Does

1. Reads the ADD (`Tasks/architecture/AEV-NNN-arch.md`) and TPO task.
2. Stops if the ADD is missing or Section 10 has unresolved open issues.
3. Ensures a feature branch is checked out and updates the devlog.
4. Implements in order: public headers → internal headers → source → unit tests → integration tests → benchmarks (if required).
5. Updates `CHANGELOG.md` if public API changed.

## Constraints

- Never mix design work with implementation.
- Every public symbol needs a complete Doxygen block.
- Minimum test coverage: happy path + error branch + edge case per public function.
- No raw `new`/`delete`, no exceptions for control flow, no Asio in public headers.
