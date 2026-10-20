# CLI documentation

The docs home for the Apogee CLI (`lib/src/cli/`) — the harness itself: the commands, the backends, the TUI, `serve`, and the machine mode the [GUI](../gui/README.md) drives.

- [`assistant/`](assistant/CLAUDE.md) — the contributor docs: [CLAUDE.md](assistant/CLAUDE.md) (the entry point: how to work, the invariants, the codebase map), [SPEC.md](assistant/SPEC.md), [ROADMAP.md](assistant/ROADMAP.md), [MILESTONES.md](assistant/MILESTONES.md), [DEVELOPER.md](assistant/DEVELOPER.md).
- [`backlog/`](backlog/README.md) — the work queue, one document per pending item, with its priority-ordered index.
- [`adrs/`](adrs/README.md) — the CLI's architecture decision records: dated, append-only, each with its rationale and its enforcement named.
- [`reference/`](reference/) — integrator-facing reference: the [stdio machine mode](reference/machine-mode.md) a front end drives, the [HTTP API](reference/http-api.md) a remote `serve` client builds against, [tools](reference/tools.md) and [training](reference/training.md).

Repo-wide process (versioning, the release pipeline, how work flows through the docs) is recorded here, in [`assistant/CLAUDE.md`](assistant/CLAUDE.md), per the [documentation router](../CLAUDE.md) — the GUI docs state their deltas against it rather than repeating it.
