---
name: pr
description: >-
  Pull Request creation and update skill for the Aevox project. Use this skill when the user asks to create a PR, update a PR description, or prepare a branch for review. It checks for existing PRs, analyses the diff, writes a structured title and description, and creates or updates the PR via the GitHub CLI. Trigger on: "create a PR", "update the PR", "prepare this branch for review", "write a PR description", "open a pull request", "PR this branch".
---

# PR — Create or Update

You are managing a Pull Request for the Aevox C++23 web framework. Your job is to produce a clean, informative PR title and description, then create or update the PR via the GitHub CLI (`gh`).

---

## Pre-Flight Checklist

Before touching the PR:

1. **Branch check** — confirm you are on a feature branch, not `main`.
2. **Commit check** — verify the branch has commits ahead of `main`.
3. **Existing PR check** — query `gh` to see if a PR already exists for this branch.
4. **Diff analysis** — read the commits and the diff to understand what changed.

If no commits exist ahead of `main`, stop and report: *"No commits to PR. Commit your changes first."*

---

## Step 1 — Check for Existing PR

```bash
gh pr list --head {branch-name} --state open --json number,title,body,url
```

- If one exists → **update** the title and body (Steps 3–5 Update).
- If none exists → **create** a new PR (Steps 3–5 Create).

---

## Step 2 — Analyse the Diff

Read the diff between `main` and the current branch:

```bash
git log --oneline main..{branch}          # commit messages
git diff main..{branch} --stat            # changed files (for context only)
git diff main..{branch}                   # full diff (for understanding)
```

Extract:
- **Task ID** — from branch name (`feature/AEV-NNN-*`) or commit messages (`AEV-NNN`). If ambiguous, ask the user.
- **Type of change** — feat, fix, docs, chore (see table below)
- **Breaking changes** — any public API or behavioural change
- **Motivation** — why this change exists (from commit messages or context)
- **Verification** — what tests, manual checks, or CI jobs confirm correctness

---

## Step 3 — Write the Title

Format: `<type>(AEV-NNN): <imperative summary>`

| Type | When to use | Example |
|---|---|---|
| `feat` | New feature, refactor, or performance improvement | `feat(AEV-010): add wildcard segment matching` |
| `fix` | Bug fix | `fix(AEV-023): prevent dangling reference in dispatch` |
| `docs` | Documentation only | `docs(AEV-027): add WebSocket and middleware examples` |
| `chore` | Build, CI, tooling, deps, test-only changes | `chore(AEV-005): add mkdocs build gate to PR pipeline` |

**Rules:**
- Scope is always the **Task ID** (`AEV-NNN`), not a subsystem name.
- Use the imperative mood (`add`, `fix`, `update`, not `added`, `fixed`, `updating`).
- Keep the summary under 72 characters.
- No period at the end.

---

## Step 4 — Write the Description

The description must be **structured and informative** — but it must **never** repeat what GitHub already shows:

- **DO NOT** list changed files (the *Files changed* tab already shows this).
- **DO NOT** paste the raw diff (the *Files changed* tab already shows this).
- **DO NOT** count lines added/removed (the PR header already shows this).

Instead, write a description that answers:

1. **What** changed at a high level (1–2 sentences).
2. **Why** it changed (motivation, bug, feature request, technical debt).
3. **How** it was verified (tests run, manual checks, CI jobs).
4. **Breaking changes** — explicit call-out if any.
5. **Related references** — task IDs, issue numbers, ADRs (only if they add context).

### Template

```markdown
### Summary
{One or two sentences describing the high-level change.}

### Motivation
{Why this change is needed. Reference a bug, task, or design decision.}

### What Changed
- {bullet describing a functional change}
- {bullet describing another functional change}
- ...

### Verification
- {test suite status or specific tests added}
- {manual checks or CI jobs that passed}

### Breaking Changes
{None, or explicit description of what breaks and how to migrate.}
```

---

## Step 5 — Create or Update

### Create (no existing PR)

```bash
gh pr create \
  --base main \
  --head {branch} \
  --title "{type}(AEV-NNN): summary" \
  --body "{description}"
```

### Update (existing PR)

```bash
gh pr edit {pr-number} \
  --title "{type}(AEV-NNN): summary" \
  --body "{description}"
```

---

## Step 6 — Report Back

```
PR {created|updated}: {url}

Title: {title}
Branch: {branch} → main
State: {open | draft}
```

---

## Constraints

- Never assume the user wants to merge. Only create/update the PR description.
- Never deploy or publish anything from this skill (docs, packages, releases).
- If `gh` is not authenticated, stop and instruct the user to run `gh auth login`.
- If the branch has uncommitted changes, warn the user but proceed with the PR using committed state.
