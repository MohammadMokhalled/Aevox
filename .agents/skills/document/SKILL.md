---
name: document
description: Trigger for any docs/ work — new or updated pages, mkdocs.yml changes, changelog entries, style fixes. Not for source comments or README-only edits.
---

### docs/ folder structure

```
docs/
├── index.md                       ← project overview
├── getting-started.md             ← quick-start entry point
├── changelog.md                   ← keepachangelog format
├── api/
│   ├── index.md                   ← module inventory + dependency map
│   └── {module}.md                ← one page per public module
├── guide/
│   ├── index.md                   ← guide inventory
│   └── {topic}.md                 ← one page per user-facing workflow
├── architecture/
│   ├── index.md                   ← architecture inventory + ADR table
│   └── {topic}.md                 ← one page per cross-cutting concern
└── examples/
    ├── index.md                   ← example inventory
    └── {example}.md               ← one page per runnable example app
```

---

## 1. Document a new public module

### Step 1 — Read the source
| What to read | Why |
|---|---|
| `include/aevox/{module}.hpp` | Public API surface: classes, enums, concepts, free functions |
| `src/{domain}/` (e.g. `src/net/`, `src/http/`) | Implementation details for the **Implementation Notes** section only |
| Existing `docs/api/*.md` | Tone, heading order, mermaid style, code-block style |

### Step 2 — Create `docs/api/{module}.md`

Use this exact heading order:

```markdown
# {Module Name}

> One-sentence description.

**Header:** `#include <aevox/{module}.hpp>`

---

## Overview
## Quick Start
## API Reference
## Error Reference        (if the module exposes error enums)
## Thread Safety
## Implementation Notes   (pimpl, hidden types, perf caveats)
## See Also               (link to guide + architecture pages)
```

Rules:
- Every public symbol gets a `####` sub-heading under **API Reference**.
- Every `[[nodiscard]]` return value shows a check pattern (`if (!result) { … }`).
- Every enum gets a table with columns: `Value`, `Meaning`, `Typical response`.
- Use mermaid diagrams for lifetime, sequence, or state flows.
- Use `!!! warning` / `!!! note` admonitions for pitfalls.

### Step 3 — Update `docs/api/index.md`

Add the new module to the **Available Today** table with: module link, header, one-line description.

If the module introduces new public headers, update the dependency-map mermaid diagram.

### Step 4 — Register in `mkdocs.yml`

Open `mkdocs.yml` at project root. Add the new page under the `nav:` → `API Reference:` section, keeping alphabetical order:

```yaml
nav:
  …
  - API Reference:
      - Overview: api/index.md
      - …
      - {Module Name}: api/{module}.md
```

Never remove or reorder existing entries.

### Step 5 — Cross-link

Update these pages to link to the new module:
- `docs/guide/*.md` — any guide page that uses the module.
- `docs/architecture/*.md` — any architecture page that mentions the component.
- `docs/getting-started.md` — if the module is part of the quick-start example.

---

## 2. Document a new guide page

### When to add a guide page
- A feature has a user-facing workflow that is **not** obvious from API signatures alone.
- There is a common pattern (e.g. "how to write an async handler", "how to configure via TOML").

### Step 1 — Read the source
| What to read | Why |
|---|---|
| `docs/api/{module}.md` | Accurate signatures and error types to reference |
| `examples/` or `tests/` | Real runnable code to turn into snippets |
| Existing `docs/guide/*.md` | Heading order and tone |

### Step 2 — Create `docs/guide/{topic}.md`

Use this exact heading order:

```markdown
# {Topic}

One-paragraph intro.

## {Sub-topic 1}
## {Sub-topic 2}
## Error Responses         (if the feature produces automatic HTTP errors)
## See Also
```

Rules:
- Lead every concept with a **runnable** code snippet.
- Use tables for syntax variants (e.g. route patterns, config keys).
- Keep code blocks ≤ 40 lines; extract repetition into helper comments.

### Step 3 — Update `docs/guide/index.md`

Add the page to the **Pages in This Guide** table.

### Step 4 — Register in `mkdocs.yml`

Add the page under `nav:` → `User Guide:`, keeping reading-order logic.

### Step 5 — Cross-link

Link from the corresponding `docs/api/{module}.md` **See Also** section back to the guide.

---

## 3. Document a new architecture topic

### When to add an architecture page
- A design decision affects multiple modules.
- The topic needs ADR-style rationale, trade-offs, or layer diagrams.

### Step 1 — Read the source
| What to read | Why |
|---|---|
| `docs/architecture/index.md` | Existing ADR table and layer diagram |
| All related `docs/api/{module}.md` | Correct symbol names to reference |
| `src/` implementation | Concrete types hidden behind abstractions |

### Step 2 — Create `docs/architecture/{topic}.md`

Use this exact heading order:

```markdown
# {Topic}

One-paragraph intro.

## Design Rationale
## Invariants / Guarantees
## Diagrams
## Trade-offs
## Related ADRs
## See Also
```

Rules:
- Every claim references a file path (`src/net/asio_executor.cpp`, `include/aevox/executor.hpp`).
- Diagrams use mermaid; keep them simple (no more than 8 nodes).
- ADRs are numbered sequentially (`ADR-N`).

### Step 3 — Update `docs/architecture/index.md`

- Add the page to the **Detailed Topics** table.
- Update the layer-diagram mermaid if a new layer is introduced.
- If an ADR is created, append it to the **ADR Summary** table.

### Step 4 — Register in `mkdocs.yml`

Add the page under `nav:` → `Architecture:`.

---

## 4. Update an existing section

### Update API signatures
1. Read `include/aevox/{module}.hpp` — diff against the doc.
2. Edit `docs/api/{module}.md` — update signature blocks, parameter tables, return descriptions.
3. If a new error value is added, update the **Error Reference** table.
4. If a new method is added, add a new `####` sub-heading in **API Reference**.

### Update examples
1. Read `examples/` and `tests/` for current, compiling code.
2. Edit `docs/examples/*.md` and `docs/guide/*.md` — paste the verified snippets.
3. Verify the snippet still compiles by reading the surrounding context in the source.

### Update changelog
1. Read `docs/changelog.md`.
2. Add entries under `[Unreleased]` → `### Added` / `### Changed` / `### Fixed`.
3. Format: ``- **`symbol`** — description (file path if internal)``.

### Update dependency map
1. Read `docs/api/index.md`.
2. Edit the mermaid `graph` to include new headers and edges.
3. Add a one-line note below the diagram if transitivity changed.

---

## 5. Style rules

| Rule | Enforcement |
|---|---|
| C++23 only | All snippets compile with `-std=c++23`. |
| `std::expected` checked | Every snippet shows `if (!result) { … }`. |
| No exceptions for control flow | Docs never mention `try/catch` for Aevox errors. |
| File paths in backticks | `include/aevox/executor.hpp`, `src/net/asio_executor.cpp`. |
| Admonitions for pitfalls | `!!! warning` for UB, `!!! note` for behaviour subtleties. |
| See Also on every page | Minimum two links: one to API, one to guide or architecture. |

---

## 6. MkDocs build check

build statically:

```bash
mkdocs build --strict
```

`--strict` treats warnings as errors. Fix any broken internal links before committing.