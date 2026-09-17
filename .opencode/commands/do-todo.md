---
description: Do a specific PrusaSLA roadmap todo by ID (e.g. /do-todo M3.4)
agent: build
---
Todo to work on: $1

Roadmap:
@doc/sla-fork/ROADMAP.md

Find todo `$1` in the roadmap. If it doesn't exist, is already ticked, is tagged `[human]`, or has any unticked ID in `needs`, say so and stop without changing files.

Otherwise, carry it out exactly as described in the "Workflow for a todo" section of AGENTS.md. For todos in M3 (resin profile import), also follow the M3 Design section: its mapping table, statuses and rules.
