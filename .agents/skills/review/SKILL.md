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

## Checklists

| Review type | What to verify | Rule source |
|---|---|---|
| **Task** | Task ID format, acceptance criteria, docs/tests in acceptance criteria, DoD gates | TPO task template |
| **Design (ADD)** | §3 Public API exactness, no Asio/third-party types in `include/aevox/`, §4 concurrency model, §5 file map, §6 dependency graph, §8 test architecture, §10 open issues resolved, §11 handoff checklist | AGENTS.md §3–4, ADD |
| **Code** | Every acceptance criterion met, every ADD §8 test case exists and passes, `std::expected` checked at call sites, error paths exercised, AGENTS.md §4 compliance, no Asio in public headers, Doxygen complete, file layout matches ADD §5, no extra dependencies, layer boundaries respected, deviations logged | AGENTS.md §3–5, §8–9, ADD |

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
