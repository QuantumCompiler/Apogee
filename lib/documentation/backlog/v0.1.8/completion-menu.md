# The completion menu: a real picker where we own the pixels

**What / why.** The shells render what their nature allows, but two completion surfaces are entirely Apogee's — the TUI's exec line and the shell session's input — and today they draw the same flat strings the stubs get (the completion-presentation spike, 2026-10-14). This is **Concept D, the user's pick (2026-10-14)**: a real menu on the owned surfaces — opened by Tab when more than one candidate stands, **grouped** (configured · a vendor's roster with its fetched date · local models), **described** (a backend's type and model; a local file's family, quant and size; a chat's title, date and turns), **arrow-selectable and filtering as you type**, Esc dismissing, Enter/Tab filling. The groups and descriptions travel **in-process only**: the completion core's candidate type gains optional group and description fields the menu renders — the wire (`__complete`, protocol v2) keeps emitting bare candidate strings exactly as today, because Concept B (the wire and stub renderings) is out by the user's call. The terminal REPL (replxx) is not a menu surface: it benefits from [`completion-namespaces.md`](completion-namespaces.md)'s shaping alone — skipped and said. Closes the spike's W4.

**Core constraint(s).** **One answerer still** ([ADR 0007](../../adrs/cli/tab-completion.md)): the menu consumes the same core call the stubs and the session completer already use — richer fields on the same candidates, never a second candidate source; the per-kind metadata (which group, what description) is populated beside the per-kind shaping [`completion-namespaces.md`](completion-namespaces.md) owns, `(consumed decision)` there for the treatment table. **The wire is untouched** (the shape pin from 45a holds). The menu is `tui/`'s (FTXUI stays inside it; the shell's key laws hold — a view taking typing keeps its keys, Esc closes the menu before it quits anything); drawing it never blocks the pump, and the exec line's existing behavior with zero/one candidates (fill or nothing) is unchanged. Secrets never gain a description channel: candidate metadata comes from the reads that already render publicly (`backend_view`'s shapes, the store's records, the chat listings) — nothing new is exposed.

**Seam + files.**
- `cli/complete_sources` (or the candidate type's home) — `Candidate{text, group, description}`, the fields optional and empty on the wire path; populated per kind where the treatment table says (models, chats, store entries; small sets stay bare).
- `tui/completion_menu.h/.cpp` — the widget: the grouped list, selection, type-to-filter, the paint within the stage's clip; keys per the shell's law.
- `cli/tui_runner.cpp` / `tui/runner_view` — the exec line opens the menu on Tab with >1 candidate (today's single-fill kept); `tui/session_view` + the session input — the same menu over `suggest_chat_input`'s candidates.
- Tests: `tui/completion_menu_test.cpp` (headless: groups rendered in order, filter narrowing, selection filling, Esc closing, one candidate filling without a menu); the exec-line and session cases extended (`cli/tui_runner_test`, `tui/session_view_test`); a `cli.tui_shell` PTY case (the menu opened, a candidate picked on a real terminal, modes restored); the wire-shape pin re-asserted.

**Reference.** 32b–32d — the shell's key laws, the clip, the headless-render test idiom this menu joins; 37h — the exec line and its one completion core; 24 — chat input completion (the session input's completer this menu draws over); [`completion-namespaces.md`](completion-namespaces.md) — the shaping and treatment table consumed; the completion-presentation spike (2026-10-14).

**Decisions made** (dated):
- 2026-10-14 — The set lands in v0.1.8 as track 45; **A + D the user's pick, B and C out** `(consumed decision — the sibling records it)`.
- 2026-10-14 — The metadata is **in-process only**: the candidate struct's optional fields, never the wire — a later Concept-B item would serialize them; nothing here presumes it.
- 2026-10-14 — The terminal REPL keeps replxx's plain list over 45a's shaped candidates — the menu is the TUI's, skipped and said for the terminal.
- 2026-10-15 — The interaction: Tab opens with more than one candidate, a second Tab cycles, arrows move, typing filters, Esc closes, Enter and Tab fill — exact keys at build within the shell's key law (the default, confirmed 2026-10-15).
- 2026-10-15 — The menu caps to the stage's height and scrolls; groups collapse to headers when a filter empties them; exact looks at build against the headless goldens (the default, confirmed 2026-10-15).


**Guardrail(s).** Headless goldens for the menu's render states (groups, filter, selection, overflow); the one-candidate fast path unchanged; the pump never blocked (manual-pump case); the PTY pick case; the wire-shape pin (bare strings, byte-identical) re-asserted beside the metadata; the leak test registers the menu's rows (descriptions come only from the public reads).

**Acceptance criteria:**
- [ ] Tab on the exec line and the session input opens the grouped, described, filterable menu when more than one candidate stands; picking fills exactly the candidate.
- [ ] Groups and descriptions match the public reads' facts (a backend's type/model, a local model's family/quant/size, a chat's title/date/turns).
- [ ] The wire and every stub are byte-identical to before; the terminal REPL is unchanged beyond 45a's ordering.
- [ ] The shell's keys, clip and pump laws hold; the full suite is green.

**Scope note.** Earmarked for v0.1.8; gated on [45a](completion-namespaces.md) (the shaping and treatment table it renders). Out of scope: wire/stub changes (Concept B, out — a deliberate later item if ever), curation (Concept C, out), replxx menus.
