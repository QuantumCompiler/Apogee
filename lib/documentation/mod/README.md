# Mod documentation

The docs home for **the Apogee mod** — the Claude plugin (`lib/src/mod/`): a TypeScript plugin running inside Claude on the user's own machine, bringing Apogee's local models and features into Claude sessions as tools, slash commands, a pane and a status line — by running the same `apogee` binary the GUI drives and reading its JSON and machine-mode output, never a port.

- [`assistant/`](assistant/CLAUDE.md) — the contributor docs: [CLAUDE.md](assistant/CLAUDE.md) (the entry point), [SPEC.md](assistant/SPEC.md), [ROADMAP.md](assistant/ROADMAP.md), [MILESTONES.md](assistant/MILESTONES.md), [DEVELOPER.md](assistant/DEVELOPER.md).
- [`backlog/`](backlog/README.md) — the work queue, one document per pending item, with its priority-ordered index.
- [`adrs/`](adrs/README.md) — the mod's architecture decision records; [ADR 0001](adrs/layer-enforcement.md) is the four-layer law, held regardless of language and framework (the user's call, 2026-10-21 — the same law the CLI and GUI carry).
- [`reference/`](reference/README.md) — integrator-facing contracts the mod itself exposes (deliberately empty: the contracts the mod consumes are the CLI's JSON documents and machine mode, and Claude's plugin API).

Repo-wide process (versioning, the release pipeline, how work flows through the docs) is recorded in the CLI's [contributor guide](../cli/assistant/CLAUDE.md) per the [documentation router](../CLAUDE.md); the mod docs state their own rules and deltas.
