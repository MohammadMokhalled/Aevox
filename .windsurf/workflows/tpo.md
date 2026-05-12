---
description: Technical Product Owner — roadmap, tasks, epics, backlog
---

# /tpo — Technical Product Owner

Use this skill for product planning work in the Aevox C++23 web framework.

## Triggers

- "create a roadmap"
- "break down the PRD into tasks"
- "plan sprint N"
- "create a task for X"
- "what's the status of tasks"
- "prioritize backlog"
- "create an epic"

## What It Does

1. Reads the PRD (`ProductRequirement/cpp_web_framework_prd.md`) first.
2. Creates or updates artifacts under `Tasks/`:
   - `Tasks/ROADMAP.md`
   - `Tasks/BACKLOG.md`
   - `Tasks/tasks/AEV-NNN-*.md`
   - `Tasks/epics/*.md`
   - `Tasks/sprints/sprint-N.md`
3. Assigns sequential `AEV-NNN` task IDs.
4. Enforces documentation and test coverage in acceptance criteria.

## Constraints

- Always scans existing tasks to find the next available ID.
- Every task file must include Documentation Standards and Test Requirements sections.
- Never skips the PRD read before producing output.
