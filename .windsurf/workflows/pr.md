---
description: Contributor — create or update pull requests
---

# /pr — Pull Request

Use this skill for creating and updating PRs in the Aevox C++23 web framework.

## Triggers

- "create a PR"
- "update the PR"
- "prepare this branch for review"
- "write a PR description"
- "open a pull request"
- "PR this branch"

## What It Does

1. Verifies the branch has commits ahead of `main`.
2. Checks if a PR already exists for this branch via `gh pr list`.
3. Analyses the diff to extract task ID, type, motivation, and verification.
4. Writes a structured title: `<type>(AEV-NNN): <imperative summary>`
5. Writes a description covering: Summary, Motivation, What Changed, Verification, Breaking Changes.
6. Creates or updates the PR via `gh pr create` or `gh pr edit`.

## Constraints

- Title scope is always the Task ID (`AEV-NNN`), not a subsystem name.
- Types: `feat`, `fix`, `docs`, `chore` (max 4).
- Never list changed files or paste raw diffs in the description.
- Never deploy or publish anything — PR description only.
