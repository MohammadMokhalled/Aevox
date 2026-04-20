---
name: investigate
description: Investigation and analysis skill for the Aevox C++23 web framework, used by the Architect and Developer. Use this skill to find the root cause of an issue, determine the best implementation approach, understand how existing code works, or answer a technical question about the codebase before committing to a design or fix. Produces findings and recommendations — no implementation code is written. Trigger on: "investigate", "how does X work", "why is this failing", "what's the best approach for", "find the root cause", "analyse this", "look into", "research how to".
---

# Investigate — Aevox

You are conducting a focused investigation into a technical question, issue, or design decision for the Aevox C++23 web framework. Your output is **findings and recommendations only** — you do not write implementation code in this skill. Results feed the Architect (for design decisions) or the Developer (for targeted fixes and implementation choices).

---

## Investigation Types

| Trigger | Type | Output |
|---|---|---|
| A failing test or runtime error | Root-cause analysis | What is wrong and exactly why |
| "How should I implement X?" | Approach analysis | Options, trade-offs, recommendation |
| "How does X currently work?" | Codebase survey | Existing behaviour and structure |
| "Is approach Y valid for Aevox?" | Feasibility check | Yes/no with evidence and constraints |
| "What does PRD §N say about X?" | Requirements lookup | Relevant constraints extracted from PRD |

---

## Investigation Protocol

### Step 1 — State the question

Restate the question in one sentence before searching anything:
> "I am investigating: {specific question}"

If the question is ambiguous, identify the most likely interpretation and state it explicitly.

### Step 2 — Read relevant context

Gather only what you need:

- **Bug / failing test:** read the failing test first, then the implementation under test, then its dependencies.
- **Approach question:** read the relevant PRD section, any associated ADD, then the closest existing code.
- **Codebase survey:** glob for relevant files, then read the key ones.
- **PRD question:** read `ProductRequirement/cpp_web_framework_prd.md` directly — the PRD is authoritative.

Do not read files speculatively. Every file you read must directly contribute to answering the question.

### Step 3 — Search the codebase

Use Grep for symbol searches, Glob for file discovery. Examples:

```
# Find all uses of a type
Grep: pattern="AsioExecutor", type="cpp"

# Find all public headers
Glob: pattern="include/aevox/**/*.hpp"

# Find usages of a method
Grep: pattern="executor\.run\(", type="cpp"

# Find tests for a module
Glob: pattern="tests/**/*executor*"
```

### Step 4 — Analyse findings

**Root cause:** trace the issue back to its origin. Symptoms are not causes. Keep asking "why" until you reach a specific line, design decision, or constraint.

**Approach question:** enumerate 2–3 realistic options. For each, state:
- What it requires
- What it rules out
- Whether it violates any CLAUDE.md invariant or PRD constraint
- Trade-offs in complexity, performance, and maintainability

**Codebase survey:** summarise structure, responsibilities, and layer boundaries. Identify any gaps or inconsistencies relevant to the question.

---

## CLAUDE.md Constraint Check

Before recommending any approach, verify it against the invariants in CLAUDE.md §3:

- **No Asio types in `include/aevox/`** — any approach requiring Asio in public headers is non-viable.
- **No third-party types in public headers** — any approach requiring llhttp, glaze, spdlog, etc. there is non-viable.
- **No raw `new` / `delete`** — non-viable.
- **`std::expected<T,E>` for errors** — any approach using exceptions for control flow is non-viable.
- **Layer diagram must be respected** — nothing flows upward.

Flag any non-viable option explicitly, even if it would be simpler.

---

## Output Format

Report findings inline. For significant investigations (root-cause of a major bug, approach selection for a new module), also write to:

```
Tasks/architecture/investigation-{slug}.md
```

### Report structure

```markdown
# Investigation: {title}
**Date:** {date}
**Triggered by:** {user request | failing test | review finding | architect question}
**Question:** {exact question}

---

## What I Examined
{files read, searches run, PRD sections consulted}

## Findings
{concrete findings — cite file:line where relevant}

---

## Root Cause / Answer
{One clear statement.
Root cause: the specific line/decision that is wrong and why.
Approach: the recommended option with rationale.
Survey: structured summary with any gaps identified.}

---

## Options (approach questions only)

### Option A: {name}
**Requires:** ...
**Rules out:** ...
**CLAUDE.md compliance:** Pass | Fail ({reason})
**Trade-offs:** ...
**Recommendation:** Yes | No | Conditional

### Option B: {name}
{same format}

---

## Recommendation
{What should happen next.
Root cause → what the fix should target.
Approach → which option and why.
Survey → issues found or gaps to address.}

---

## Constraints for Whoever Acts on This
{Any PRD or CLAUDE.md constraints that must be respected by the Architect or Developer.}
```

---

## What This Skill Does Not Do

- Does **not** write implementation code. Code changes go through `/implement` (Developer) or `/fix` (Developer).
- Does **not** make architectural decisions. It surfaces options and constraints so the Architect can decide with `/architect`.
- Does **not** create or modify task files. New tasks are the TPO's job with `/tpo`.

This skill answers questions. Acting on the answers is a separate role.
