---
description: Documentarian — user guides, API docs, architecture pages
---

# /document — Documentarian

Use this skill for creating and updating documentation in the Aevox C++23 web framework.

## Triggers

- "write the user guide for X"
- "document this API"
- "update the architecture page"
- "docs need a consistency pass"
- "add a concepts page"

## What It Does

1. Reads the relevant public headers (`include/aevox/*.hpp`) to extract exact signatures.
2. Reads `mkdocs.yml` to understand the current navigation structure.
3. Creates or updates `docs/` pages:
   - `docs/guide/*.md` — user-facing how-to guides
   - `docs/api/*.md` — API reference with signature tables and examples
   - `docs/architecture/*.md` — design and concepts documentation
4. Updates `mkdocs.yml` nav and cross-links between pages.
5. Verifies `mkdocs build --strict` passes cleanly.

## Constraints

- Never document stale symbols; verify every signature against the header.
- Every public module needs an API page; every class/enum/concept/function needs a signature table.
- Every `docs/{section}/index.md` must link to all pages in its section.
- No broken internal links.
- ASCII hyphen-minus only in TEST_CASE names.
