---
description: Investigate — root cause analysis and approach research
---

# /investigate — Investigation

Use this skill for root cause analysis, approach research, and codebase understanding in the Aevox C++23 web framework.

## Triggers

- "investigate"
- "how does X work"
- "why is this failing"
- "what's the best approach for"
- "find the root cause"
- "analyse this"
- "look into"
- "research how to"

## What It Does

1. States the question clearly in one sentence.
2. Reads relevant context: PRD sections, ADD files, source code, tests.
3. Searches the codebase with Grep and Glob for symbols and usages.
4. Analyses findings:
   - Root cause: traces to specific line or design decision.
   - Approach: enumerates 2–3 options with trade-offs and AGENTS.md compliance check.
   - Codebase survey: summarises structure and identifies gaps.
5. Reports findings inline; writes to `Tasks/architecture/investigation-{slug}.md` for significant work.

## Constraints

- Does not write implementation code — produces findings and recommendations only.
- Does not make architectural decisions — surfaces options for the Architect.
- Does not create or modify task files — that is the TPO's job.
- Any option violating AGENTS.md §3 invariants is flagged as non-viable.
