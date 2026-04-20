---
name: fix
description: Developer skill for applying a targeted fix to a known issue in the Aevox C++23 web framework. Use this when a specific problem has been identified — by the user, a review, the Architect, or a failing test — and needs to be resolved. Trigger on: "fix this issue", "apply the fix for", "address finding", "this test is failing", "the review found X", "correct this bug".
---

# Fix — Aevox Developer

You are applying a targeted fix to a known issue in the Aevox C++23 web framework. The problem has already been identified — your job is to resolve it cleanly, re-test, and report.

---

## Before Touching Any File

1. **Read the issue description carefully.** Understand exactly what is wrong before acting.

2. **Read every file involved in the fix.** Never edit code you haven't read. Understand the surrounding context — what the code is trying to do, why it was written this way.

3. **Identify the fix source and priority:**

   | Source | Priority |
   |---|---|
   | User request | Highest — treat as Critical |
   | Architect review — Critical | Fix immediately; nothing else first |
   | Architect review — Major | Fix before any new work |
   | Architect review — Minor | Fix before marking Done |
   | Architect review — Advisory | Use judgment |
   | TPO change | High — check if ADD revision needed first |
   | Failing test | High — root-cause before patching |

4. **Update the Developer Log** at `Tasks/progress/{TASK-ID}-devlog.md`:
   - Add a `Fix:{source}` entry with today's date and a description of the issue.
   - Set status to `Fix:User`, `Fix:Architect`, or `Fix:TPO`.

---

## Applying the Fix

### Minimal scope

Change only what is necessary to resolve the issue. Do not refactor surrounding code, add features, or clean up unrelated style. A fix touches only the bug.

### Code standards still apply

Even in a fix, all CLAUDE.md §3–4 rules are non-negotiable:

- No `new` / `delete`
- No Asio types in `include/aevox/`
- No third-party types in public headers
- All `std::expected` / `std::optional` returns `[[nodiscard]]`
- No prohibited patterns from CLAUDE.md §4

### If the fix requires a public API change

If fixing the issue requires changing a signature in `include/aevox/`:

1. **Stop.** Report this to the user before making the change.
2. Confirm whether the ADD needs a revision — it almost certainly does.
3. Do not change a public API without architect sign-off unless the user explicitly authorizes it.

### If the fix reveals a deeper issue

1. Fix the originally reported issue.
2. Document the deeper issue in the Developer Log.
3. Report it to the user — do not silently fix unreported issues.

---

## Re-Testing

After applying the fix, re-run the relevant tests. If the fix touches shared code (executor, task types, router core), run the full suite.

```bash
cmake --build build --target aevox_tests
ctest --test-dir build --output-on-failure
```

**Do not report the fix as done until tests pass.**

If fixing a failing test: it must now pass. If you cannot make it pass without breaking others, stop and report.

---

## Updating Documentation

If the fix changes documented behaviour, update docs as part of the fix:

- Doxygen: update the affected `@brief`, `@return`, `@note` as needed.
- mkdocs: update `docs/api/{module}.md` if the public-facing description changed.
- `CHANGELOG.md`: add an entry under `[Unreleased]` if a public header was modified.

---

## Completion Report

Update the Developer Log (status back to `Done` or `Implementing` if more work remains), then report:

```
Fix applied: {brief description of what was changed}

Root cause: {one sentence — what was actually wrong}

Files changed:
  {list with brief reason for each}

Tests re-run: {module | full suite}
Tests result: {N passing / N total}

Follow-on risks: {none | describe}
```

---

## TPO Fix Requests — Special Case

When a TPO change modifies acceptance criteria:

1. Read the updated task file.
2. Determine whether the ADD needs a revision:
   - Scope change (new/removed/changed behaviour) → **yes, flag it before implementing**.
   - Wording clarification (same behaviour, clearer description) → usually not.
3. Do not implement a scope change without an updated ADD.
