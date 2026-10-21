# The parity law: every CLI capability has a classified place in the GUI

**What / why.** The user's first hands-on run of the built app found the structural hole (2026-10-20): the GUI covers the design's 29 screens, but **nothing holds it to the CLI's whole surface** — the audit against the TUI's own classification table (`lib/src/cli/source/presentation/cli/tui_parity.cpp`, ADR 0010's 30 top-level commands) measured **8 whole command families with no GUI place at all**: `knowledge`, `embed` (collections), `symphonies`, `train`, `datasets`, `agents`, `mcp`, `review` — plus partials (execute sessions beyond plain chat, prose graphs and communities, task recipes, `analyze`) and no recorded carve-outs. The TUI never drifted this way because ADR 0010 made its coverage law mechanical; the GUI has no analog. This item builds it: **ADR 0003** — every capability the CLI offers a person has a classified place in the GUI: a **screen** (shipped or named as its queued item), the **console** ([`gui-console.md`](gui-console.md), the catch-all), or a **carve-out with its recorded reason** — and **the table**, the GUI's `tui_parity` analog: one declaration in the app classifying every top-level command, held by a test against the binary's own word list (`apogee __complete` / a committed fixture of command words, refreshed deliberately), failing on an unclassified command the day it ships and on a classified one that vanished. Today's truthful classification lands with it: the covered fifteen by their screens, the eight gaps by their queued items (2c–2g), the carve-outs below.

**Core constraint(s).** The law is **mechanical from birth** (ADR 0001's precedent: teeth with the first file): the test refuses an empty word list, and a new CLI command fails the GUI's suite until classified — which is the point. **Carve-outs mirror the TUI's three with the GUI's own reasons**: `serve` (a daemon the app would orphan), `reset` and `uninstall` (they destroy the CLI's data directory under the running app — and the app never uninstalls the binary it drives); the `__*` plumbing is a protocol, not a page. **The classification is honest**: a gap names its queued item, never "later"; a shipped screen names its module. ADR 0003 written per the ADR house shape (Status/Date/Context/Decision/Consequences/Enforcement), the gui ADR index grown.

**Seam + files.** `lib/documentation/gui/adrs/parity.md` (ADR 0003) + the index row; `lib/src/darwin/source/presentation/app/` or a small `data/` unit — the classification table as one declaration; `lib/src/darwin/tests/architecture/` — the parity check (the word list from a committed fixture of `apogee __complete`'s answer, a refresh script beside it); the gui CLAUDE checklist gains the classify-with-the-change rule.

**Reference.** The CLI's ADR 0010 and `tui_parity.cpp` — the law and table mirrored; ADR 0012 (doors reachable) — the companion discipline, adopted here as the table's screens naming real modules; the audit (2026-10-20) — the measured gap above.

**Decisions made** (dated):
- 2026-10-20 — **The user's mandate**: the GUI does everything the CLI and TUI do; recorded as the gui SPEC's Full-parity principle the same day, this item its teeth.
- 2026-10-20 — The carve-outs are the TUI's three plus the plumbing, each with its reason in the table.

**Open calls:**
- [default: the fixture-vs-live word list (committed fixture refreshed by a script, the hermetic default; a live `__complete` pass optional and said) at build.]

**Guardrail(s).** The parity test over the fixture word list (unclassified fails by name; vanished classified fails; empty list refused); a planted new command word failing; the ADR indexed and linked from the checklist.

**Acceptance criteria:**
- [ ] ADR 0003 exists in the house shape; the table classifies every command word the fixture carries — fifteen screens, the console, the queued-gap items by id, the carve-outs with reasons.
- [ ] The check fails a planted unclassified word and a vanished classification; the fixture refresh is one script; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 2, CLI parity); ungated — the track's head, everything else classifies against it. Out of scope: the screens themselves (2c–2g), the console (2b), any CLI change.
