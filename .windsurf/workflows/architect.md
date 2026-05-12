---
description: Architect — module design, ADD creation, architectural review
---

# /architect — Architect

Use this skill for design and architecture decisions in the Aevox C++23 web framework.

## Triggers

- "design this module"
- "create an ADD for AEV-NNN"
- "review this design"
- "how should we structure X"
- "is this approach valid for Aevox"

## What It Does

1. Reads the associated TPO task (`Tasks/tasks/AEV-NNN-*.md`) and the PRD.
2. Creates or updates `Tasks/architecture/AEV-NNN-arch.md` (ADD).
3. Defines public API, file map, dependency graph, test architecture, and open issues.
4. Verifies AGENTS.md §3 invariants are respected in every design decision.

## Constraints

- Never proceed past Section 10 open issues without user sign-off.
- No Asio types in `include/aevox/` headers.
- No third-party library types in public headers.
- All public symbols must have Doxygen blocks in the design.
