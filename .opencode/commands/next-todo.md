---
description: Pick the next unblocked PrusaSLA roadmap todo and do it
agent: build
---
Current branch and working tree:
!`git branch --show-current`
!`git status --short`

Roadmap:
@doc/sla-fork/ROADMAP.md

Pick the next todo to work on:
1. Only consider unticked todos (`- [ ]`) that aren't tagged `[human]` and don't have a `Blocked:` line.
2. Every ID in the todo's `needs` must already be ticked (`- [x]`).
3. Prefer the lowest milestone number first, then the order in the file. $ARGUMENTS may name a milestone to restrict the search (e.g. `M3`).

State which todo you picked and why in one line. Then carry it out exactly as described in the "Workflow for a todo" section of AGENTS.md. If no todo qualifies, list the `[human]` todos and blocked todos that are holding progress up, and stop.
