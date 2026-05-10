---
name: review
description: >-
  Structured review skill for the Aevox C++23 web framework. Used by all agents — TPO (reviewing tasks and requirements), Architect (reviewing designs and ADD compliance), Developer (reviewing code and tests). Produces a severity-tiered findings report. Trigger on: "review this", "review AEV-NNN", "check this code", "audit this file", "is this correct", "does this follow the rules", "look over this".
---

# Review — Aevox

You are conducting a structured review for the Aevox C++23 web framework. The review type is determined by what you are given. Apply all applicable rule sets from AGENTS.md and the PRD.

---

## Determine Review Type

| You are given | Review type | Primary rule set |
|---|---|---|
| A task file (`Tasks/tasks/AEV-NNN-*.md`) | **Task Review** — requirements, DoD, test coverage spec | AGENTS.md §9, TPO task template |
| An ADD (`Tasks/architecture/AEV-NNN-arch.md`) | **Design Review** — API correctness, layer compliance, completeness | AGENTS.md §3–4, PRD §5–6 |
| Source files (`.hpp`, `.cpp`, test files) | **Code Review** — implementation correctness, style, compliance | AGENTS.md §3–4, ADD §3/§5/§8 |
| Documentation (`.md` in `docs/`) | **Documentation Review** — correctness, completeness, cross-links, style | Document skill §5, mkdocs.yml |
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
| 1 | **Critical** | AGENTS.md §3 invariant violation, security flaw, or broken contract | Must fix before merge — blocks everything |
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

### C++23 Compliance (AGENTS.md §4)
- [ ] No violations of AGENTS.md §4 prohibition list (all items apply)

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

## Documentation Review Checklist

Verify against `include/aevox/`, `mkdocs.yml`, and the Document skill.

### Correctness & Completeness
- [ ] Every signature, enum, parameter, default, and error condition matches the public header exactly
- [ ] No stale symbols; no phantom modules or methods
- [ ] Every public module has an API page; every class/enum/concept/function/method is documented with signature, table, return desc, and example
- [ ] Every module with errors has an **Error Reference** table
- [ ] Every `docs/{section}/index.md` links to all pages in its section; dependency-map mermaid covers all public headers

### Navigation & Cross-links
- [ ] Every page registered in `mkdocs.yml` under the correct section with logical order
- [ ] No broken internal links
- [ ] Every page **See Also** links bidirectionally to related API, guide, and architecture pages

### Style
- [ ] Snippets are C++23, check `std::expected`, no `try/catch` for Aevox errors
- [ ] Admonitions for pitfalls; mermaid ≤ 8 nodes; tables consistent; paths in backticks
- [ ] Headings follow the Document skill template

### Changelog
- [ ] `[Unreleased]` section exists with all public changes under `Added`/`Changed`/`Fixed`
- [ ] Entries use format: `- **symbol** — description`

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
**Rule violated:** AGENTS.md §{N} / PRD §{N}
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

- Every finding cites the exact rule it violates (AGENTS.md §N, PRD §N, or named checklist item).
- Every Critical and Major finding includes a concrete fix — not just "fix this".
- Never mark a finding Critical unless a AGENTS.md §3 invariant or security contract is broken.
- Always include "What Is Correct" — a review that only lists problems is less useful than one that confirms what is working.
- Be direct and specific. Line numbers and code snippets beat prose.
