---
name: review
description: Structured review skill for the Aevox C++23 web framework. Used by all agents — TPO (reviewing tasks and requirements), Architect (reviewing designs and ADD compliance), Developer (reviewing code and tests). Produces a severity-tiered findings report. Trigger on: "review this", "review AEV-NNN", "check this code", "audit this file", "is this correct", "does this follow the rules", "look over this".
---

# Review — Aevox

You are conducting a structured review for the Aevox C++23 web framework. The review type is determined by what you are given. Apply all applicable rule sets from CLAUDE.md and the PRD.

---

## Determine Review Type

| You are given | Review type | Primary rule set |
|---|---|---|
| A task file (`Tasks/tasks/AEV-NNN-*.md`) | **Task Review** — requirements, DoD, test coverage spec | CLAUDE.md §9, TPO task template |
| An ADD (`Tasks/architecture/AEV-NNN-arch.md`) | **Design Review** — API correctness, layer compliance, completeness | CLAUDE.md §3–4, PRD §5–6 |
| Source files (`.hpp`, `.cpp`, test files) | **Code Review** — implementation correctness, style, compliance | CLAUDE.md §3–4, ADD §3/§5/§8 |
| A mix | Run all applicable review types, clearly separated |

---

## Pre-Review: Gather Context

Before writing a single finding:

1. Read every file you are reviewing end-to-end.
2. If reviewing code: also read the associated ADD (`Tasks/architecture/{TASK-ID}-arch.md`) and the TPO task (`Tasks/tasks/{TASK-ID}-*.md`) if they exist.
3. If reviewing a task: also read the relevant PRD sections it references.

---

## Severity Tiers

| Tier | Label | Meaning | Required action |
|---|---|---|---|
| 1 | **Critical** | CLAUDE.md §3 invariant violation, security flaw, or broken contract | Must fix before merge — blocks everything |
| 2 | **Major** | Significant non-compliance, missing required output (tests, docs, Doxygen), design risk | Must fix this sprint / cycle |
| 3 | **Minor** | Style violations, missing edge-case tests, non-obvious code without comments | Fix before marking Done |
| 4 | **Advisory** | Improvement suggestions, alternative approaches | No block — use judgment |

---

## Task Review Checklist

- [ ] Task ID follows `AEV-NNN` format (zero-padded, sequential)
- [ ] Summary is precise enough to work from without a meeting
- [ ] All acceptance criteria are written in Given/When/Then form
- [ ] Acceptance criteria include documentation and test requirements explicitly
- [ ] Documentation Standards section is present and complete
- [ ] Test Requirements section is present (unit + integration + bench as applicable)
- [ ] Definition of Done checklist includes all non-negotiable gates
- [ ] Dependencies are listed (blocked-by / blocks)
- [ ] Technical Notes reference relevant PRD sections and ADRs

---

## Design Review Checklist (ADD)

- [ ] §3 Public API: all signatures are exact (not sketches), fully Doxygen-annotated
- [ ] §3 Public API: no Asio types in any `include/aevox/` declaration
- [ ] §3 Public API: no third-party library types in public signatures
- [ ] §4 Internal Design: concurrency model documented
- [ ] §5 File Map: every file listed exists or will be created — no phantom files
- [ ] §6 Dependency Graph: no upward layer dependencies, executor.hpp boundary marked
- [ ] §8 Test Architecture: named test cases cover happy, error, edge, thread-safety
- [ ] §10 Open Issues: all resolved or explicitly deferred with rationale
- [ ] §11 Handoff Checklist: complete and unambiguous for the Developer

---

## Code Review Checklist

### Correctness
- [ ] Every acceptance criterion in the TPO task is met by the implementation
- [ ] Every test case named in ADD §8 exists and passes
- [ ] `std::expected` errors are checked at every call site — none silently discarded
- [ ] Error paths are exercised in tests, not just happy paths
- [ ] No test is skipped, commented out, or marked `PENDING`

### C++23 Compliance (CLAUDE.md §4)
- [ ] No violations of CLAUDE.md §4 prohibition list (all items apply)

### Public API (`include/aevox/`)
- [ ] No Asio headers or types
- [ ] No third-party library types
- [ ] All `[[nodiscard]]` annotations present on `std::expected` / `std::optional` returns
- [ ] Every public symbol has a complete Doxygen block

### Architecture
- [ ] File layout matches ADD §5 exactly
- [ ] No dependency introduced that is not in ADD §6
- [ ] Layer boundaries respected — nothing flows upward
- [ ] All deviations from the ADD are in the Developer Log

---

## Output Format

Report findings inline. For non-trivial reviews or when requested, also write to `Tasks/architecture/review-{slug}.md`.

```markdown
# Review: {subject}
**Date:** {date}
**Reviewed by:** {role — TPO | Architect | Developer}
**Files reviewed:** {list}
**Severity summary:** {N} Critical · {N} Major · {N} Minor · {N} Advisory

---

## Critical — Must Fix Before Merge
{ID}. **Location:** `file:line`
**Rule violated:** CLAUDE.md §{N} / PRD §{N}
**Current:** `{offending code or text}`
**Required:** `{what it should be}`
**Impact:** {why this matters}

---

## Major — Must Fix This Sprint
{same format}

---

## Minor — Fix Before Done
{same format}

---

## Advisory
{same format}

---

## What Is Correct
{list what is right — always include this section}

---

## Verdict
**Approved** | **Approved with conditions** | **Rejected**

{One paragraph: the most important action needed.}
```

---

## Rules for Writing Findings

- Every finding cites the exact rule it violates (CLAUDE.md §N, PRD §N, or named checklist item).
- Every Critical and Major finding includes a concrete fix — not just "fix this".
- Never mark a finding Critical unless a CLAUDE.md §3 invariant or security contract is broken.
- Always include "What Is Correct" — a review that only lists problems is less useful than one that confirms what is working.
- Be direct and specific. Line numbers and code snippets beat prose.
