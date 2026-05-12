---
name: fix
description: >-
  Developer skill for applying a targeted fix to a known issue in the Aevox C++23 web framework. Use this when a specific problem has been identified — by the user, a review, the Architect, or a failing test — and needs to be resolved. Trigger on: "fix this issue", "apply the fix for", "address finding", "this test is failing", "the review found X", "correct this bug".
---

# Fix — Aevox Developer

You are applying a targeted fix to a known issue in the Aevox C++23 web framework. The problem has already been identified — your job is to resolve it cleanly, re-test, and report.

---

## Before Touching Any File

1. Read the issue description and every file involved.
2. Determine priority from the source (see Developer agent).
3. Update the Developer Log (`Tasks/progress/{TASK-ID}-devlog.md`) with a `Fix:{source}` entry.

---

## Applying the Fix

### Minimal scope

Change only what is necessary to resolve the issue. Do not refactor surrounding code, add features, or clean up unrelated style. A fix touches only the bug.

### Code standards

> See AGENTS.md §3–5.

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
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

**Do not report the fix as done until tests pass.**

If fixing a failing test: it must now pass. If you cannot make it pass without breaking others, stop and report.

---

## Updating Documentation

Update docs and `CHANGELOG.md` as needed per AGENTS.md §8 and §11.

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

## TPO or architect Fix Requests — Special Case

When a TPO or architect change modifies acceptance criteria:

1. Read the updated task file.
2. Determine whether the ADD needs a revision:
   - Scope change (new/removed/changed behaviour) → **yes, flag it before implementing**.
   - Wording clarification (same behaviour, clearer description) → usually not.
3. Do not implement a scope change without an updated ADD.
