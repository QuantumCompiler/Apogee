# Execute sessions, plays and recipes: the session surface completes

**What / why.** The session window speaks plain chat; the CLI's session surface is wider, and the audit (2026-10-20) names the partials: **execute sessions** — a session opened with a suite, bare on one model, or in a project folder (its `AGENTS.md` read, the coder persona carried, the folder attached), with `--orchestrate` offering the symphonies as tools — all of it machine-mode-carried today (`execute` takes the same two flags; the TUI's picker has an execute door by ADR 0011); **plays** — `/play` in a session (the events already flow; 2e builds the catalog side); **task recipes** — `task run --recipe` expanding to the very flags a hand-typed run takes (1j's recorded out); and **`analyze`** — classified console-covered (2b), recorded here so the table's row has its owner. This item completes the GUI's session picker and sheets: the execute door (suite / model / project folder pickers, orchestrate's switch, the banner's honesty), plays launched and narrated in the transcript as the protocol says them, and the new-task sheet's recipe picker (recipes listed from their documents, the expansion shown before launch — the flags visible, never hidden).

**Core constraint(s).** ADR 0001/0002; the execute door drives `execute` through the one client core — the same child shape as chat, different flags (ADR 0011's spirit: one opening per mode, the GUI's door calling it); the recipe expansion is shown as the exact flags (the ledger's provenance honesty); orchestrate's banner states what the CLI's does (`orchestrating N`); nothing here invents a session capability — every affordance maps to a flag or line the contract carries. **Design boards grown** (the session boards and the new-task board, per the design law).

**Seam + files.** `source/presentation/sessions/` (the picker's execute door, sheets), `source/presentation/tasks/` (the recipe picker), `source/business/session/` (the execute launch), `source/business/reads/` (recipes, symphonies); `lib/design/spec/` — the boards grown.

**Reference.** The CLI's 27s/27t and 41a–41c — execute's rungs the door exposes; ADR 0011 — one session opening; the TUI's picker — the proven door shape; 40c — the recipes' expansion honesty.

**Decisions made** (dated):
- 2026-10-20 — From the parity audit (the user's mandate); `analyze` is console-covered, the table's row saying so (the TUI's own classification mirrored).

**Open calls:**
- [default: the door's picker layout and the recipe sheet's expansion rendering per the grown boards; details at build.]

**Guardrail(s).** Launch argv pins per rung (suite/model/project/orchestrate, recipes' expansion byte-exact); play and orchestrate narration over scripted streams; the banner wordings golden; hermetic.

**Acceptance criteria:**
- [ ] An execute session opens from the picker with suite/model/project/orchestrate, behaving as the flags say; plays narrate in the transcript.
- [ ] The new-task sheet offers recipes with their expansion shown before launch; `analyze`'s classification stands in the table; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 2); gated on 2a, the parity law (shipped 2026-10-20: [MILESTONES → Milestone C](../../assistant/MILESTONES.md#milestone-c--cli-parity)) and 2e, agents, MCP and symphonies ([shipped 2026-10-20](../../assistant/MILESTONES.md#milestone-c--cli-parity)) (2e — the symphony catalog plays draw on). Out of scope: new protocol lines (none needed — measured), the console (2b).
