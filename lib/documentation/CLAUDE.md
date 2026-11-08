# Documentation router

The documentation is split **by kind, then by platform**, so each kind of document sits in one place (the change of 2026-11-07, the user's call):

| Kind | Holds |
|---|---|
| [`assistant/`](assistant/README.md) | Contributor docs: how to work on each application. Start at its `CLAUDE.md`, then SPEC, ROADMAP, MILESTONES and DEVELOPER |
| [`backlog/`](backlog/README.md) | The work queue: each application's index (its `README.md`) and one document per pending item |
| [`adrs/`](adrs/README.md) | The architecture decision records, dated and append-only |
| [`reference/`](reference/README.md) | Integrator-facing contracts |

Inside each kind, one folder per platform:

| Working on | Code | Start at | Backlog | ADRs |
|---|---|---|---|---|
| **The CLI**: the harness itself (commands, backends, the TUI, `serve`, machine mode) | `lib/src/cli/` | [`assistant/cli/CLAUDE.md`](assistant/cli/CLAUDE.md) | [`backlog/cli/`](backlog/cli/README.md) | [`adrs/cli/`](adrs/cli/README.md) |
| **The Apple apps**: macOS, iPadOS and iOS, one Swift codebase that drives the CLI | `lib/src/darwin/` | [`assistant/darwin/CLAUDE.md`](assistant/darwin/CLAUDE.md) | [`backlog/darwin/`](backlog/darwin/README.md) (an item for one OS sits in `macos/`, `ipados/` or `ios/`) | [`adrs/darwin/`](adrs/darwin/README.md) |
| **Every GUI app**: what binds all of them, whatever the language: the GUI spec, the four layers, the parity table | - | [`assistant/gui/SPEC.md`](assistant/gui/SPEC.md) | - | [`adrs/gui/`](adrs/gui/README.md) |
| **The mod**: the Claude plugin, driving the same binary | `lib/src/mod/` | [`assistant/mod/CLAUDE.md`](assistant/mod/CLAUDE.md) | [`backlog/mod/`](backlog/mod/README.md) | [`adrs/mod/`](adrs/mod/README.md) |
| **Linux** and **Windows** apps: not built yet | `lib/src/linux/` · `windows/` | - | `backlog/unix/`, `backlog/windows/` | - |

Facts that cross the boundary:

- **Every other application is a client of the CLI.** The GUI drives it over the stdio machine mode and the mod over its JSON documents, machine mode and the MCP server — the contracts are [`reference/cli/machine-mode.md`](reference/cli/machine-mode.md) and the 28h documents, pinned to the binary by the CLI's own conformance suites — and no client links the CLI or reimplements a capability it owns.
- **Development runs on the dev channel** (the user's call, 2026-10-31): the release `apogee` over `~/.apogee` and the installed app are the user's production install, changed only by what a release ships, so its updaters are tested for real. Every application's development build drives `apogee-dev` over `~/.apogee-dev` -- the rule is the CLI docs' [Development runs on the dev channel](assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel).
- **Superpowers works inside the lifecycle** (the user's call, 2026-11-07): the repository enables the Superpowers skills, and the CLI docs' [Superpowers fits the docs-first lifecycle](assistant/cli/CLAUDE.md#superpowers-fits-the-docs-first-lifecycle) maps each one onto Apogee's steps for every application -- the backlog document is the spec, plans stay out of the repo, one commit per item, never a merge into `stable`.
- **Repo-wide process lives with the CLI docs for now.** Versioning, the release pipeline, CI and the working process are recorded in [`assistant/cli/CLAUDE.md`](assistant/cli/CLAUDE.md); the other homes state their own deltas rather than repeating them. If a genuinely shared home is ever carved out, it lands beside the application directories here.
