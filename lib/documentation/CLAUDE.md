# Documentation router

One directory per application. Decide which application your change touches, then read that directory's own docs — each is a self-contained docs-as-source-of-truth system with its own contributor guide, spec, roadmap, backlog and ADRs.

| Working on | Code | Docs | Start at |
|---|---|---|---|
| **The CLI** — the harness itself: commands, backends, the TUI, `serve`, machine mode | `lib/src/cli/` | [`cli/`](cli/) | [`cli/assistant/CLAUDE.md`](cli/assistant/CLAUDE.md) |
| **The GUI** — the desktop applications (macOS, Linux, Windows), front ends that drive the CLI | `lib/src/darwin/` · `linux/` · `windows/` | [`gui/`](gui/) | [`gui/assistant/CLAUDE.md`](gui/assistant/CLAUDE.md) |

Two facts that cross the boundary:

- **The GUI is a client of the CLI.** It drives the CLI over the stdio machine mode — the contract is [`cli/reference/machine-mode.md`](cli/reference/machine-mode.md), pinned to the binary by `cli.machine_schema_conformance` — and never links it or reimplements a capability the CLI owns.
- **Repo-wide process lives with the CLI docs for now.** Versioning, the release pipeline, CI and the working process are recorded in [`cli/assistant/CLAUDE.md`](cli/assistant/CLAUDE.md); the GUI's docs state their own deltas rather than repeating them. If a genuinely shared home is ever carved out, it lands beside the application directories here.
