# GUI documentation

The docs home for Apogee's desktop applications (`lib/src/darwin/`, `lib/src/linux/`, `lib/src/windows/` — one application per platform, each a front end that drives the CLI over the [stdio machine mode](../cli/reference/machine-mode.md)).

This directory is being born. As the GUI work is specced it grows the same shape as [`cli/`](../cli/):

- `assistant/` — the contributor docs (CLAUDE.md entry point, SPEC, ROADMAP, MILESTONES, DEVELOPER).
- `backlog/` — the work queue, one document per pending item, with its priority-ordered index.
- `adrs/` — the GUI's architecture decision records.
- `reference/` — integrator-facing reference, if the GUI ever exposes contracts of its own.

Until those exist, ground in the [documentation router](../CLAUDE.md) and the CLI's [contributor guide](../cli/assistant/CLAUDE.md) for the repo-wide process (versioning, the release pipeline, how work flows through the docs); the machine-mode contract the GUI consumes is the CLI's, pinned to the binary by its own tests.
