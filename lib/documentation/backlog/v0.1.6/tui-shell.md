# The TUI shell: bare `apogee` opens a screen

**What / why.** The user's direction (2026-10-04, resolved from the same day's spike): running `apogee` with nothing after it should open a **full-screen terminal UI**, the way `claude` opens into an interactive session — with the raw CLI left exactly as it is. The spike measured the gap (sandboxed probe on the shipped binary, temp `APOGEE_HOME`): bare `apogee` today prints the top-level help and exits 0 — **18 subcommands**, all of which stay untouched — and the terminal machinery in `views/` paints *lines* (the status line, the repainted busy line, streamed markdown, the replxx editor), composing no screen: no layout regions, no focus model, no view switching. This item delivers the screen: **FTXUI** (the user's call: full-screen compositor, not inline) vendored at a pinned tag through FetchContent like every dependency; a new presentation module `tui/` holding the shell — the screen manager, the event pump, the focus model, view registration; and the entry: **bare `apogee` on a terminal opens the shell; everything else is byte-identical to today** — a non-TTY invocation (pipe, script, CI) prints exactly the help it prints now, and every subcommand behaves as if this item never happened. The SPEC revision rides this set, already made (2026-10-04, the user's call, unconditioned): the "No TUI, and no second in-binary front-end" non-goal is removed; what remains policy is that the TUI is **strictly additive** — the CLI stays the contract and the GUI siblings remain committed beside it. The siblings put content on this stage: [tui-session-view.md](tui-session-view.md) (32c) the chat, [tui-workbench-views.md](tui-workbench-views.md) (32d) the rest.

**Core constraint(s).**
- **Additive, byte for byte:** with no TTY on stdout/stdin, bare `apogee` emits today's help exactly (golden-pinned); `--help`, `--version` and all 18 subcommands are untouched in parsing, output and exit codes. No script, completion stub, installer or machine-mode driver can observe this item.
- **One screen owner:** while the shell runs, it owns the terminal whole — no line painter (status line, busy line, `cli_reporter`) writes beside it; M1's one-painter discipline generalizes to "the compositor is the painter." On exit — including crash paths — the terminal state is restored (modes, cursor, alternate screen off): a shell that wedges the user's terminal is the bug class this constraint names.
- **Renderer over cores** *(the design default, recorded vetoable — the user's SPEC removal was unconditioned, so this binds as this item's architecture, not as law)*: the shell and its views render what the existing seams produce and act by calling the existing cores — the Reporter seam for live turns, `operations/` and the command cores for everything else. The anti-pattern is cited below in Reference; a view that reimplements an action is the trap returning.
- **The executable stays thin** (the nothing-links-`apogee` invariant): `tui/` is an ordinary presentation module with its row in `cmake/modules.cmake`, linked into `apogee_core`; the layering holds (`views/` never parses argv, `machine/` never paints — `tui/` paints and composes, reaching down only).
- **Never-listens untouched; stderr capture untouched:** the shell opens no socket and inherits no child's stderr; `cli.no_listen_symbols` covers the new dependency through the existing link-graph walk by construction.
- **The dependency is pinned and scoped:** FTXUI via FetchContent (pinned tag, `SYSTEM`, `FIND_PACKAGE_ARGS` per the house dependency rules), linked only by `tui/` — nothing below presentation may touch it.
- **Windows is stated, not assumed:** FTXUI carries Windows console support, but Apogee's Windows verification is recorded as weaker (the PTY/termios test skips); the shell ships with that caveat extended to it explicitly, and a Windows terminal that cannot host the screen gets a said refusal falling back to help — never a garbled screen.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; tests in the layer of what they test ([ADR](../../adrs/cli/tests-mirror-architecture.md)).

**Seam + files.**
- `presentation/tui/` (new module; its `cmake/modules.cmake` row and layer card updates per A5): `shell.h/.cpp` (the screen manager: view registry, focus, the top-level frame), `pump.h/.cpp` (the FTXUI event loop wrapped behind one seam so views stay testable headless), `theme.h/.cpp` (NO_COLOR and the `ansi/` mode matrix honored).
- `presentation/cli/root.cpp`: the default action — no subcommand + stdin and stdout both TTYs → launch the shell; otherwise today's help path, untouched.
- `lib/src/cli/CMakeLists.txt` + `cmake/`: the FTXUI pin; the modules row.
- `views/terminal.h/.cpp`: the ownership handshake (enter/leave alternate screen, restore-on-exit including the signal paths).
- Tests: `tests/presentation/tui/` — headless goldens (FTXUI renders to a string, so the frame, focus moves and view switching pin as text); the non-TTY byte-identity golden against today's help; restore-on-exit under the PTY harness (`tests/scripts/py/`, the house recorded-bytes replay precedent); the Windows refusal path behind a fake capability probe.
- Consumes: `ansi/` (the mode matrix), M1's painter discipline (generalized), the PTY test harness (shipped).

**Reference.** The product shape is `claude`'s own CLI (bare invocation opens the interactive surface). The recorded anti-pattern is the reference project's Milestone O — a full Bubble Tea TUI (bare entry, eight views, a `ctrl+p` palette, a form engine) **shipped across two releases and then entirely removed**, the reasons recorded: binary and dependency shrink, and "retired the per-view CLI-parity maintenance burden." That record shaped the old non-goal; with the non-goal removed (2026-10-04), it binds here as the design rule above: views render seams and call cores, never reimplement them. External: [FTXUI](https://github.com/ArthurSonzogni/FTXUI) (C++20, MIT, Windows console support, headless string rendering).

**Decisions made** (dated):
- 2026-10-04 — The user's three calls, resolved in the spike's follow-up: the SPEC non-goal **removed, unconditioned** (the revision is in SPEC.md → Non-goals, dated, landing with this set); the shell is **full-screen on FTXUI**, not inline; the raw-CLI re-tiering half of the original ask **withdrawn** — the TUI is strictly additive and no command changes.
- 2026-10-04 — Targeted **v0.1.6** (the user's placement), taking 32b–32d after 32a.
- 2026-10-04 — Renderer-over-cores recorded as the design default (vetoable): the cheapest build is also the one that cannot re-create the parity burden the reference project recorded.
- 2026-10-04 — Bare entry is TTY-gated on both stdin and stdout: a pipe on either side gets today's behavior — detection errs toward the raw CLI, never toward a screen nobody can see.

**Open calls:**
- [default: an explicit `apogee tui` subcommand exists beside the bare entry (completable, per [ADR tab-completion](../../adrs/cli/tab-completion.md)) — the spelled way in for odd TTY situations and documentation; veto drops it and bare-TTY stays the only door]
- [default: FTXUI pinned at its latest stable tag at build time, recorded in the pin comment like the llama.cpp pin]

**Guardrail(s).**
- The byte-identity golden: piped bare `apogee` equals today's help output exactly; `apogee --help` and every subcommand's `--help` unchanged (sampled set in the suite, full sweep once at build).
- Restore-on-exit: under the PTY harness, quit, SIGINT and a planted crash all leave the terminal usable (modes restored, alternate screen left) — asserted on the replayed byte stream.
- Headless frame goldens: the shell's empty frame, focus traversal and view switching render deterministically to strings.
- The link policy and `cli.no_listen_symbols` pass with FTXUI in the graph (the existing walks pick it up; asserted once explicitly).
- The layering scan: `tui/` declared, linked to exactly its row, nothing below presentation includes it.

**Acceptance criteria:**
- [ ] `apogee` at an interactive terminal opens the full-screen shell; `q`/`Ctrl-C` leaves it with the terminal intact; `apogee </dev/null` and `apogee | cat` print today's help byte-identically.
- [ ] All 18 existing subcommands behave exactly as before, verified by the existing suite running green with zero output changes.
- [ ] The shell renders on macOS and Linux terminals (PTY-harness-verified); on a Windows console that cannot host it, the said refusal falls back to help.
- [ ] FTXUI is pinned, `SYSTEM`-included, linked only by `tui/`; configure fails if anything else links it (the modules map holds it).

**Scope note.** Item **32b**, earmarked for **v0.1.6**; gated on nothing pending (the SPEC revision landed with this set). Out of scope: the session view and every content view ([32c](tui-session-view.md), [32d](tui-workbench-views.md)); any raw-CLI change (withdrawn, the user's call); mouse support beyond what FTXUI gives free; a `machine/` or wire change of any kind.
