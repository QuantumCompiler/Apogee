# GUI documentation

The docs home for Apogee's desktop applications (`lib/src/darwin/`, `lib/src/linux/`, `lib/src/windows/` — one application per platform, each a front end that drives the CLI over the [stdio machine mode](../cli/reference/machine-mode.md)).

- [`assistant/`](assistant/CLAUDE.md) — the contributor docs: [CLAUDE.md](assistant/CLAUDE.md) (the entry point), [SPEC.md](assistant/SPEC.md), [ROADMAP.md](assistant/ROADMAP.md), [MILESTONES.md](assistant/MILESTONES.md), [DEVELOPER.md](assistant/DEVELOPER.md).
- [`backlog/`](backlog/README.md) — the work queue, one document per pending item, with its priority-ordered index.
- [`adrs/`](adrs/README.md) — the GUI's architecture decision records; [ADR 0001](adrs/layer-enforcement.md) is the four-layer law, held regardless of language and framework, and [ADR 0002](adrs/presentation-mvvm.md) the MVVM-and-`styles` shape inside Presentation.
- [`reference/`](reference/README.md) — integrator-facing contracts the GUI itself exposes (deliberately empty: the contract the GUI consumes is the CLI's machine mode).

Repo-wide process (versioning, the release pipeline, how work flows through the docs) is recorded in the CLI's [contributor guide](../cli/assistant/CLAUDE.md) per the [documentation router](../CLAUDE.md); the GUI docs state their own rules and deltas.
