# The TUI parity law

**What / why.** The full-screen shell shipped 2026-10-10 ([Milestone AH](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui)) with seven views — Session, Models, Chats, Suites, Config, Home, Keys — and the same day's parity spike measured the gap they leave: of the binary's **25 root subcommands, 7 have a shell surface and 18 have none**, and of its **118 leaf verbs, 24 carry a machine read** (`--output-format`), both counted off the installed binary in a sandboxed `APOGEE_HOME` (2026-10-10, help sweep + a PTY capture of the live tab strip). Nothing holds that gap honest: [ADR 0002](../../adrs/cli/mode-parity.md) opens "The CLI is driven three ways" — the shell is a fourth door no record names — the Implementing-a-Feature checklist has no TUI line, and which commands deliberately get **no** view lives in chat history, not in a test. This item lands the law before the views: a new ADR naming the shell a mode, a classification-table test in the house's own idiom (`tests/presentation/httpserver/parity_test.cpp`, where every CLI verb is a twin, a backfill naming its pending item, or a `carve_out("reason")`, and `tui` is already classified "an interactive surface, as chat is"), the checklist line, and the recipe — so a subcommand shipped without its shell surface classified **fails a test** instead of drifting. Track 37's sibling items then flip backfill rows into view rows as they ship; the table seeded here is today's truth.

**Core constraint(s).** The TUI stays **strictly additive** (SPEC.md → Non-goals, the 2026-10-04 revision): the law classifies surfaces, it never re-tiers a command. ADRs are **append-only**: ADR 0002 gains one dated forward-pointer line naming the new record, never a rewrite. The hand-built-views anti-pattern stays cited: a view renders a read and calls a core (32d's structure), and the law exists precisely so "few views, each earning its place" is a recorded judgment per command, not folklore. The classification test must **refuse to pass vacuously** (an empty command registry fails it — the house's vacuity-guard idiom). The presentation layer card, the Codebase Map and ADR 0008's table move together where touched (`harness.layer_context` fails any one alone).

**Seam + files.**
- `lib/documentation/adrs/cli/the-shell-is-a-mode.md` — **ADR 0010** (the next number; 0009 landed 2026-10-10): Context (the spike's numbers; ADR 0002's three doors), Decision (the shell is a mode: every root subcommand is classified — rendered by a named view, backfilled by a named pending item, or carved out with a recorded reason; a view's mutation is byte-identical to its command; a view's read is the command's own carved rows), Consequences (the cost of a user-facing feature includes its shell classification), Enforcement (the table test below + the 32d byte-parity pattern + the leak test's view surfaces).
- `lib/documentation/adrs/cli/README.md` — the 0010 index row; `lib/documentation/adrs/cli/mode-parity.md` — one dated line pointing forward.
- `lib/src/cli/tests/presentation/cli/tui_parity_test.cpp` (new) — builds the root registry in-process (the `tui_cmd_test` way), walks every subcommand against the classification table: `view("Models")` / `backfill("37b")` / `carve_out("…")`. Unlisted or unclassified fails, naming the command; an empty registry fails.
- `lib/documentation/assistant/CLAUDE.md` — one Implementing-a-Feature checklist line (ADR 0009's line is the shape): a change that adds or alters a user-facing read or mutation classifies its shell surface in the same change — drawn, or carved out and said.
- `lib/documentation/assistant/DEVELOPER.md` — an **"Adding a TUI view"** recipe (sibling of "Updating a carried model list"): carve the read, wire the hooks, byte-parity test, leak-test row, flip the classification entry.
- `lib/src/cli/source/presentation/CLAUDE.md` — the card's `tui/` line gains the law's pointer.

Seeded classification (today's truth): **views** — `chat` (Session), `chats` (Chats), `models` (Models), `config` (Config), `version` (Home), `tui` (the door itself), `system` (the monitor bar; its full page backfilled to [37b](tui-doctor-views.md)); **backfills** — `check`/`providers` → 37b, `knowledge`/`embed`/`graph` → [37c](tui-knowledge-views.md), `execute`/`symphonies` → [37d](tui-execute-session.md), `task` → [37e](tui-progress-seam.md), `train`/`datasets` → [37f](tui-training-views.md), `agents`/`mcp`/`auth` → [37g](tui-agents-mcp-views.md); **carve-outs** — `serve`, `uninstall`, `reset`, `complete`, `analyze` (reasons under Open calls).

**Reference.** `tests/presentation/httpserver/parity_test.cpp` — the classification-table idiom this copies (twin / backfill / carve-out, reasons recorded, unclassified fails); ADR 0009's checklist line — the shape of the new CLAUDE.md line; the Ommi precedent carried on the 32 items — a TUI of eight hand-built views removed whole under its per-view parity burden — is the anti-pattern this law answers structurally.

**Decisions made** (dated):
- 2026-10-10 — The split lands in v0.1.6 as track 37 (the user's placement call on the spike report; 37 by the ever-assigned rule — 36 was assigned and shipped the same day).
- 2026-10-10 — The law ships **first**, seeded with today's truth, and sibling items flip rows as they ship — so coverage drift becomes a test failure from the first day, not after the views exist (the spike's split, accepted).
- 2026-10-10 — Classification is at **root-subcommand granularity**; per-verb coverage lives in each view's own parity tests (the HTTP table's precedent: its rows are verbs because HTTP twins are per-verb; the shell's unit is the view).

**Open calls:**
- [default: `serve`, `uninstall`, `reset`, `complete` are permanent carve-outs — `serve` is a daemon for remote clients, `uninstall` and `reset` destroy the ground the shell stands on, `complete` is a scripting surface the Session view supersedes; `analyze` is carved out too, revisited when 37g's Agents view exists.]

**Guardrail(s).** The classification test fails on a planted unclassified subcommand (verified against a fake registration, the house's planted-violation idiom) and on an empty registry; `harness.layer_context` holds the card edit; the docs flow's link sweep covers the new ADR.

**Acceptance criteria:**
- [ ] ADR 0010 exists with its index row, and ADR 0002 carries the dated forward pointer.
- [ ] `tui_parity_test.cpp` is green over the seeded table and fails, naming the command, when a fake subcommand registers unclassified.
- [ ] The CLAUDE.md checklist line and the DEVELOPER.md "Adding a TUI view" recipe exist; the presentation card points at the law.
- [ ] No view, command, or behavior changes — the help sweep and the full suite are byte-stable.

**Scope note.** Earmarked for v0.1.6; first of track 37, ungated. Documents plus one test — the views are 37b–37g's.
