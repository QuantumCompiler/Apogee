# The busy line: transient status for every slow command

**What / why.** `apogee models list` goes silent for seconds (the user's report, 2026-09-30): it reads every stored GGUF's header (`models::inspect_gguf` per store entry in the listing sweep, and again for role status), and nothing on screen says work is happening. Chat solved this problem in Milestone G: `StatusLine` — one self-overwriting line, spinner and elapsed time, active only on a terminal — and its own header records the doctrine ("no construction-time notice may write raw stderr on an interactive path… every surface takes a `StatusLine` rather than reaching for `std::cerr`"). But the component is only *constructed* on the conversational paths, so the doctrine stops at chat's edge: an ordinary command has no line to speak on. This item carries the existing component across the application: a scoped **busy line** any command opens around work that may take a while — `models list`'s sweep says `reading model headers… (12/40)` on one repainted line and clears to nothing when the table prints — wired into the known slow spots (`models list/info/status`, `check`, `graph build/update`, `embed ingest`), with phase and count updates flowing up from the layers doing the work as plain callbacks.

**Core constraint(s).**
- **One painter.** This extends `commands/status_line` through the same `TerminalWriter`; a second transient-line implementation is the bug class the thinking view's erase arithmetic already closed once. If a frame needs something `StatusLine` cannot draw, `StatusLine` grows it.
- **stdout stays the answer.** Decoration goes to **stderr**, and only when stderr is a terminal: `apogee models list | grep …` receives exactly the rows, byte-identical to today, and machine mode's rule 3 (stdout is events, stderr is diagnostics) is preserved by construction. `Options.active = false` on a pipe already means *no frames and no escape codes* — that stays structural.
- **Never the last column**, width measured at every repaint, `display_width` cells — the rule the thinking view earned, inherited by every frame this item draws.
- **Layering holds:** `models/`, `embedstore/` and `graph/` never include `commands/` — progress crosses upward as an optional, default-empty callback sink, the closure pattern the guarded packages already use everywhere else.
- **No timing lies:** elapsed is wall clock; a count renders only when the layer truly knows the total; there is never a synthesized percentage.
- **Silence stays an option:** `--quiet`, machine mode, any `--output-format json`, and a non-TTY stderr each mean zero bytes of decoration.

**Seam + files.**
- `commands/status_line.h/.cpp`: a general `spinner_frame` overload — label, tick, elapsed, optional `done/total` — beside the chat-shaped one (whose token estimate stays chat's); a small RAII `BusyLine` scope (construct = the line may appear, destruct = cleared), exposing `set(text)` and `progress(done, total)`.
- `commands/models.cpp`: the listing and role-status sweeps report through the sink, naming the file being inspected; `commands/check.cpp`, `commands/graph.cpp`, `commands/embed.cpp`: the same adoption where nothing paints today (`download_progress` keeps owning `models pull`'s line).
- `models/` (the store walk and inspect loops): optional progress-sink parameters, defaulted empty, so the pure layers stay pure and hermetic tests pass no sink.
- Tests: `spinner_frame` goldens for the general frame; a PTY check in the `pty_startup_check.py` pattern (the line appears, repaints in place, clears without residue); the pipe contract extended (`models list` piped: byte-identical, zero escapes); narrow-width frames never touch the last column.

**Reference (Ommi).** The citation is already in the code: `status_line.h` names Ommi's OMMI-14 — "startup speaks on one line" was retrofitted there after raw stderr writes wrecked interactive paths. Apogee built `StatusLine` from day one but scoped it to conversations; no Ommi analog exists for command-wide busy reporting. This item finishes the thought the header started.

**Decisions made** (dated):
- 2026-09-30 — Asked for by the user (`models list` takes a while, silently): one status updater usable across the application in CLI mode, a single repainted line like the thinking view's; **targeted at the tail of v0.1.3**, the user's call.
- 2026-09-30 — Extend `StatusLine`, never a second painter — the overlap scan's conclusion: the component exists and is proven; its *reach* is the gap.

**Open calls:**
- [default: frame = spinner · label · `(done/total)` when known · elapsed once past 2 s; label truncated against the live width] The frame's shape — taste, veto freely.
- [default: the line appears only after 150 ms of work, so a fast command never flickers] The latency gate.
- [default: first consumers are `models list/info/status`, `check`, `graph build/update`, `embed ingest`; every later slow command adopts the same scope as it is touched] The adoption set.
- [default: `--quiet` suppresses the busy line everywhere it suppresses the status line today — one verbosity model, not two] Quiet semantics.

**Guardrail(s).**
- Golden frames for the general renderer, including the no-total and pre-2 s shapes.
- The PTY check: on a real terminal the line appears during a slowed sweep (a test seam delays inspection), repaints in place, and clears — the final screen holds only the table.
- The pipe contract: piped `models list` output is byte-identical to a build without this item; zero escape bytes on any non-TTY stream, mutation-tested where the convention applies.
- The 150 ms gate: a fast fake sweep paints nothing.

**Acceptance criteria:**
- [ ] `apogee models list` on a terminal with a populated store shows one repainting line naming the phase and count while it works, then the table, with no residue row and nothing in the last column.
- [ ] `apogee models list | cat` and `> file` produce exactly today's bytes — no spinner, no escapes.
- [ ] `apogee check` and `apogee graph build` speak through the same component; `--quiet` silences all of it.
- [ ] A command finishing under 150 ms never flickers a frame.

**Scope note.** **Maintenance item M1** (created 2026-09-30 at the v0.1.3 tail as a track-26 item; moved into the Maintenance table at its creation, 2026-10-03 — the user's call); release-agnostic, claimable any time, gated on nothing pending. Out of scope: making the sweeps themselves faster (caching GGUF header reads is the real latency fix for `models list` and worth its own item if wanted); multi-line progress displays; progress *events* in machine mode (the 27/28 tracks own that protocol's growth).
