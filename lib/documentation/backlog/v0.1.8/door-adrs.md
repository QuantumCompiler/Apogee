# The door ADRs: one opening, and every door reachable

**What / why.** The CLI↔TUI disparity audit (2026-10-14, the installed binary on a PTY against sandboxed homes) found one drift class behind every wall it measured: **a front end composed its own second opening of a mode**. The shell's execute door is its own suite-only path — `TuiSessionDriver::open_execute(const std::string& suite)` (`tui_session.cpp:333`) — so 41a's bare door never reached the shell, 41b's project opening and 41c's persona reach `apogee execute` but not the same session opened from the shell (zero references to the ritual in `tui_session.cpp`), and `apogee tui` takes **zero flags against execute's 40**. Meanwhile the parity law's table classifies `execute` as `View("Session (the picker's execute door)")` (`tui_parity.cpp:28`) and the runner's refusal points there (`:61–63`) — yet a fresh install shows **no picker at all** until a saved chat exists, and no execute row without a suite: a classified door that is often unreachable, with the law green. This item writes the two records that close the class **before** the fixes are built, so [`tui-execute-door.md`](tui-execute-door.md), [`tui-session-launch.md`](tui-session-launch.md) and [`tui-reachability-law.md`](tui-reachability-law.md) build under law rather than re-learning it (the user's call, 2026-10-14: the ADRs first):

- **ADR 0011 — A mode is opened by its command's own path.** One open function and one flag table per mode; every front end's door calls that function and speaks that table; a second composition of the same opening is the defect ([ADR 0002](../../adrs/cli/mode-parity.md)'s one-core rule, applied to session *openings*). Consequences: opening behavior added to a mode (a context ritual, a persona, a new rung) reaches every front end by construction; the cost of a new door is wiring, never a copy. Enforcement: the door goldens — a session opened by a front end's door byte-equal to the command's own opening on the same state — named here, landed by the door items.
- **ADR 0012 — A classified door is reachable.** Extends [ADR 0010](../../adrs/cli/the-shell-is-a-mode.md): a `View` classification asserts a door **reachable on a fresh install**; a door that is genuinely conditional either becomes unconditional or its row records the condition and the refusal states it. Enforcement: the per-row reachability case on a real PTY — named here, landed by [`tui-reachability-law.md`](tui-reachability-law.md).

**Core constraint(s).** ADRs are **append-only**: 0011 and 0012 are new records; ADR 0010 gains one dated forward-pointer line and ADR 0002 one naming 0011, never a rewrite. This is a **documents-only item** — no code, no test, no behavior change; the enforcement each record names is explicitly "lands with" its door item, so neither ADR claims teeth it does not yet have (each record's Enforcement section says so, dated). The ADR index, the Implementing-a-Feature checklist and the presentation layer card move with the records where touched (`harness.layer_context` holds the card set).

**Seam + files.**
- `lib/documentation/adrs/cli/one-session-opening.md` (ADR 0011) and `lib/documentation/adrs/cli/doors-are-reachable.md` (ADR 0012) — the records, each carrying the audit's evidence in Context (`tui_session.cpp:333`, the zero-ritual grep, the 0-vs-40 flag count; the fresh-install picker capture, `tui_parity.cpp:28/61–63`).
- `lib/documentation/adrs/cli/README.md` — the two index rows; `mode-parity.md` and `the-shell-is-a-mode.md` — one dated forward line each.
- `lib/documentation/assistant/CLAUDE.md` — the checklist line (the ADR 0009 line's shape): a change that adds or alters how a mode opens lands it in the mode's one open path, every front end's door included.
- `lib/src/cli/source/presentation/CLAUDE.md` — the card's one-line pointer.

**Reference.** ADR 0002 (the one-core law these records extend to openings and doors); ADR 0010 and its classification table (the surface 0012 tightens); the 37a precedent (the law shipped first, its table seeded with the day's truth, the views flipping rows as they shipped); the disparity audit (2026-10-14) for every measurement above.

**Decisions made** (dated):
- 2026-10-14 — The set lands in v0.1.8 as track 44, **the ADRs first, gating the fixes** (the user's call on the spike report: future-proof before building).
- 2026-10-14 — Two records, not one: the opening rule (0011) and the reachability rule (0012) are separate concerns with separate enforcement, per the one-concern-per-record format.

**Open calls:**
- [default: each record's Enforcement section names its test and says "lands with <item>" until it does — an ADR whose teeth are pending says so rather than implying them.]

**Guardrail(s).** The docs flow's link sweep over the new records; `harness.layer_context` green with the card line; no source or test file touched (the diff is documentation alone).

**Acceptance criteria:**
- [ ] ADR 0011 and ADR 0012 exist with the audit's evidence in Context and their index rows; 0002 and 0010 carry dated forward pointers.
- [ ] The CLAUDE.md checklist line and the presentation card's pointer exist.
- [ ] No code changes; the full suite is byte-stable.

**Scope note.** Earmarked for v0.1.8; first of track 44, ungated — the documents the sibling fixes build under. Out of scope: any code, any test, the fixes themselves.
