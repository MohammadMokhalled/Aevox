---
description: Fix — structured bug fix skill for the Aevox framework
---

# /fix — Bug Fix

Use this skill when fixing a specific bug in the Aevox C++23 web framework.

## Triggers

- "fix this bug"
- "this test is failing"
- "there's a crash in"
- "address this issue"

## What It Does

1. Reads the failing test, error output, or bug report first.
2. Investigates the root cause by tracing symptoms to origin.
3. Writes a minimal, focused fix targeting the root cause — never a workaround.
4. Adds or updates a regression test that would have caught the bug.
5. Verifies the fix compiles and all tests pass.

## Constraints

- Prefer single-line upstream fixes over downstream workarounds.
- Identify root cause before implementing — no speculative patches.
- Every bug fix must include a regression test.
- Keep changes scoped; do not refactor unrelated code.
- Follow AGENTS.md §3–5: no raw `new`/`delete`, `std::expected` for errors, no exceptions for control flow.
