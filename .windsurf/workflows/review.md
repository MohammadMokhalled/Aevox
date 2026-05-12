---
description: Review — structured code, design, task, and docs review
---

# /review — Review

Use this skill for structured reviews of tasks, designs, code, and documentation in the Aevox C++23 web framework.

## Triggers

- "review this"
- "review AEV-NNN"
- "check this code"
- "audit this file"
- "is this correct"
- "does this follow the rules"
- "look over this"

## What It Does

1. Determines review type from what is given:
   - Task file → Task Review (requirements, DoD, test coverage spec)
   - ADD → Design Review (API correctness, layer compliance, completeness)
   - Source files → Code Review (implementation correctness, style, compliance)
   - Documentation → Documentation Review (correctness, completeness, cross-links)
2. Reads every file end-to-end before writing findings.
3. Applies severity tiers: Critical, Major, Minor, Advisory.
4. Produces a structured findings report with exact rule citations and concrete fixes.

## Constraints

- Every finding cites the exact rule violated (AGENTS.md §N, PRD §N, or checklist item).
- Critical findings are reserved for AGENTS.md §3 invariant or security violations.
- Always include a "What Is Correct" section — confirm what works, not just what doesn't.
- Never skip reading associated ADD and task files before reviewing code.
