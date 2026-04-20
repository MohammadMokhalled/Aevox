---
name: implement
description: Senior C++23 Developer skill for the Aevox web framework focused purely on the implementation phase. Use this skill when given a task ID or feature to build. It drives code → tests → self-review in one focused cycle without re-planning or re-designing. Trigger on: "implement AEV-NNN", "write the code for", "build this", "start implementing", "code this up".
---

# Implement — Aevox Developer

You are executing the **implementation phase** of a task for the Aevox C++23 web framework. You have already been given a task ID or a description of what to build. Your job is to produce working, tested, documented code — nothing else.

---

## Pre-Flight Checks (Do These Before Writing a Single Line)

1. **Locate the ADD.** Read `Tasks/architecture/{TASK-ID}-arch.md`.
   - If it does not exist: **stop**. Report the missing ADD and tell the user to run `/architect` first.
   - If Section 10 (Open Issues) contains unresolved items: **stop**. List them and ask for architect sign-off before continuing.

2. **Locate the TPO task.** Read `Tasks/tasks/{TASK-ID}-*.md`.
   - Extract all acceptance criteria and the Definition of Done checklist.

3. **Check the current branch.**
   - If on `main` or a shared branch: create `feature/<feature-name-in-kebab-case>` and switch to it.
   - If already on the correct feature branch: proceed.

4. **Update or create the Developer Log** at `Tasks/progress/{TASK-ID}-devlog.md`.
   - Set status to `Implementing`.
   - Record which inputs were read.

---

## Code Standards (Zero Tolerance)

All CLAUDE.md §3–4 rules apply. If you write a prohibited pattern, fix it immediately.

---

## Implementation Order

Follow the ADD File Map (§5) exactly. Create in dependency order:

1. Public headers (`include/aevox/`) — declarations with complete Doxygen blocks
2. Internal headers (`src/`) — if any
3. Implementation files (`src/`) — definitions
4. Unit tests (`tests/unit/`)
5. Integration tests (`tests/integration/`) — only if ADD §8 requires them
6. Benchmarks (`tests/bench/`) — only if the task is tagged `#networking` or `#performance`

Every public symbol needs a complete Doxygen block (CLAUDE.md §8) before moving to the next file. Use the ADD §3 comment as your starting point.

---

## Tests

Use test case names **exactly** as specified in ADD §8. No renaming.

```cpp
// tests/unit/{module}/{TASK-ID}-{slug}.cpp
// {TASK-ID}: {what this file tests}
// ADD ref: Tasks/architecture/{TASK-ID}-arch.md § Test Architecture
#include <catch2/catch_test_macros.hpp>

TEST_CASE("{TASK-ID}: <description>", "[{module}]") {
    SECTION("happy path") { /* ... */ }
    SECTION("error path — <condition>") { /* ... */ }
}
```

Minimum per public function:
- Happy path
- At least one `std::expected` error branch exercised
- Edge cases (boundary values, empty inputs)

For networking code additionally:
- Thread-safety test using `std::jthread` + `std::barrier`
- Cancellation / clean-shutdown test

Run tests after writing them:
```bash
cmake --build build --target aevox_tests
ctest --test-dir build --output-on-failure
```

Do not declare the task done until all tests pass.

---

## Deviation Protocol

If the ADD is wrong, incomplete, or impossible to implement as written:

1. Record the deviation in the Developer Log under `## Deviations from ADD`.
2. If it affects public API or file structure: **stop**, report to the user, request an ADD revision from the Architect.
3. If it is a minor internal detail: document it and make the pragmatic choice.

---

## After Implementation

1. Update Developer Log — status `Done` (or `Self-Review` if doing a full review pass).
2. Update `CHANGELOG.md` under `[Unreleased]` if any public header was modified.
3. Report back in this format:

```
Implemented: {TASK-ID}

Files created/modified:
  {list}

Tests: {N} unit | {N} integration | bench: {result or "not required"}

Blockers / deviations: {none | describe}
Next: {docs | self-review | done}
```
