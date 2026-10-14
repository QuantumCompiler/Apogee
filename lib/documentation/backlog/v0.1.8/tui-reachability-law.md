# The reachability law: a classified door, proven on a fresh install

**What / why.** The disparity audit's quietest finding is the one that lets the others recur: **the parity law tests classification, not reachability**. For a day, `tui_parity.cpp:28` classified `execute` as `View("Session (the picker's execute door)")` and the runner's refusal pointed there (`:61–63`) while a fresh install had **no picker at all** and a suite-less one no execute row — the law green, the door absent (W1's measured frames). [ADR 0012](../../adrs/cli/doors-are-reachable.md) states the rule; this item lands its teeth: **every `View` row's door proven reachable on a fresh install**, on a real PTY — the shell opened against a seeded-empty home, each classified door walked to and seen (the view switched to by its number or category, the picker's rows present, the exec line opening) — so a door that regresses to conditional fails the law by name instead of hiding behind a green classification. Rows whose door is *deliberately* conditional record the condition in the table and the test asserts the refusal states it. Closes the audit's W5.

**Core constraint(s).** The test extends the house's PTY idiom (`cli.tui_shell`'s recipe: a pseudo-terminal from a shell, `stty -g` recorded, reads until exit, PENDIN masked) and **refuses to pass vacuously** — an empty `View`-row list fails it, and it must be seen reaching the execute door specifically (the regression that motivated it, planted once to verify). The classification table stays **the one source** (37a's law): the reachability case reads the same rows the classification case reads — a row added to one is covered by the other with nothing to remember. Fresh-install truth means a seeded sandbox home (`check --fix`), no suite and no saved state beyond what seeding makes: the doors must stand on exactly what a new user has (the door items make that true; this test keeps it true).

**Seam + files.**
- `cli/tui_parity.cpp` — the row shape gains the reachability facts the test drives (how a door is reached: the view's number/category, a picker row's label, an exec-line spelling; and the recorded condition where one is deliberate).
- `tests/scripts/py/pty_tui_check.py` (or a sibling `pty_door_check.py`, the build's call) — the fresh-install walk: every `View` row's door reached and seen, every recorded condition's refusal worded as the table says; registered as a `cli.*` ctest case beside `cli.tui_shell`.
- [ADR 0012](../../adrs/cli/doors-are-reachable.md) — its Enforcement section flipped from "lands with this item" to the test's name, dated.
- Tests of the test: the planted regression (the execute door made conditional again in a throwaway build) fails the case naming the row — verified once, the house's planted-violation idiom.

**Reference.** 37a — the classification table and its vacuity guards (the law this tightens); 32b's `cli.tui_shell` — the PTY recipe extended; [ADR 0012](../../adrs/cli/doors-are-reachable.md) (44a, shipped 2026-10-14) `(consumed decision)`; the disparity audit (2026-10-14) for W1/W5's evidence.

**Decisions made** (dated):
- 2026-10-14 — The set lands in v0.1.8 as track 44, the law's teeth last — the test asserts the fixed truth, so it gates on both door items (the user's ADR-first call shaping the order).
- 2026-10-14 — Reachability is proven **on a fresh install**: a door needing prior state (beyond seeding) is either fixed or its condition recorded and said — never a green row over an absent door.
- 2026-10-15 — The walk runs as one PTY session visiting every door (one boot); per-row boots only if flake forces the split (the default, confirmed 2026-10-15).


**Guardrail(s).** The vacuity guards (empty row list fails; the execute door required seen); the planted conditional-door regression failing by name; the PTY recipe's restore checks unchanged; the suite green on every platform that runs the PTY checks (POSIX; Windows the recorded skip, as the siblings are).

**Acceptance criteria:**
- [ ] Every `View` row's door is reached and seen on a fresh install by the new case; a planted conditional door fails it naming the row.
- [ ] Deliberately conditional rows carry their condition in the table and the refusal's wording is asserted against it.
- [ ] ADR 0012's Enforcement names the live test, dated; the full suite is green.

**Scope note.** Earmarked for v0.1.8; gated on 44b (shipped 2026-10-14) and [44c](tui-session-launch.md) (the truth it pins is theirs). Out of scope: new doors, any classification change beyond the condition field, Windows PTY coverage.
