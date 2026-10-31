# Documentation router

One directory per application. Decide which application your change touches, then read that directory's own docs — each is a self-contained docs-as-source-of-truth system with its own contributor guide, spec, roadmap, backlog and ADRs.

| Working on | Code | Docs | Start at |
|---|---|---|---|
| **The CLI** — the harness itself: commands, backends, the TUI, `serve`, machine mode | `lib/src/cli/` | [`cli/`](cli/) | [`cli/assistant/CLAUDE.md`](cli/assistant/CLAUDE.md) |
| **The GUI** — the desktop applications (macOS, Linux, Windows), front ends that drive the CLI | `lib/src/darwin/` · `linux/` · `windows/` | [`gui/`](gui/) | [`gui/assistant/CLAUDE.md`](gui/assistant/CLAUDE.md) |
| **The mod** — the Claude plugin: Apogee's models and features inside Claude sessions, driving the same binary | `lib/src/mod/` | [`mod/`](mod/) | [`mod/assistant/CLAUDE.md`](mod/assistant/CLAUDE.md) |

Two facts that cross the boundary:

- **Every other application is a client of the CLI.** The GUI drives it over the stdio machine mode and the mod over its JSON documents, machine mode and the MCP server — the contracts are [`cli/reference/machine-mode.md`](cli/reference/machine-mode.md) and the 28h documents, pinned to the binary by the CLI's own conformance suites — and no client links the CLI or reimplements a capability it owns.
- **Repo-wide process lives with the CLI docs for now.** Versioning, the release pipeline, CI and the working process are recorded in [`cli/assistant/CLAUDE.md`](cli/assistant/CLAUDE.md); the other homes state their own deltas rather than repeating them. If a genuinely shared home is ever carved out, it lands beside the application directories here.
