# The monitor overlay and the suites screen

**What / why.** Two system-shaped surfaces. **The monitor** (`ScreenMonitor`): CPU, memory, GPU and disk from `system --output-format json` every 2 seconds while open (ready, per the contract board), plus memory held by each of the app's own children (app-side — the app reads its children). **Suites** (`ScreenSuites`): the suites and their members — readable through `models status` and `config get` (verified by the spike: both answer) — and "will it fit?", which the CLI computes for `chat --suite` but offers no read of (G10, marked Later): the board's fallback is **the app works it out from `models info`**, which this item does within the measured limits (the spike: window/cache fields absent on non-GGUF sets — the verdict says "unknown" where the facts are, honestly).

**Core constraint(s).** ADR 0001/0002; the monitor polls **only while open** (the design's row), through the reads runner, stopping when closed; children's memory from the app's own process handles, never `ps` parsing; the fit arithmetic says unknown where the document gives no sizes — never a guessed verdict (the store's honesty law, client-side). Suites mutations (default suite, members) go through their config verbs with read-back.

**Seam + files.** `source/presentation/monitor/` (the overlay; its view-model), `source/presentation/suites/`, `source/business/reads/` (system/status/config reads), `source/business/session/` or `jobs` (children's memory surface). Tests: the monitor's poll lifecycle (open→tick→close) over scripted reads; the fit table over fixture documents (known, partial, unknown); hermetic.

**Reference.** `ScreenMonitor.dc.html`, `ScreenSuites.dc.html`; the contract board's SETTINGS AND SYSTEM + SUITES rows (G10's fallback); the spike's `models info` field measurements; the CLI's 27e footprint — the arithmetic's shape, reimplemented client-side only as far as the documents allow (and retired if G10's read ever lands).

**Decisions made** (dated):
- 2026-10-20 — From the design-completion spike (the user's call); the fit verdict is honest-partial until/unless G10's read exists (Later, per the design).

**Open calls:**
- [default: the monitor's layout and the 2 s cadence per the design; details at build.]

**Guardrail(s).** Poll-lifecycle pins (no polling when closed); fit-table matrix incl. the unknown rows; hermetic.

**Acceptance criteria:**
- [ ] The monitor shows the system read live while open and stops when closed, with the app's children's memory beside it.
- [ ] Suites list their members with an honest fit verdict (unknowns said); membership edits read back; the full suite is green.

**Scope note.** Earmarked for v0.2.0; gated on 1e, the app shell (shipped 2026-10-20: [MILESTONES → Milestone B](../assistant/MILESTONES.md#milestone-b--the-designs-screens)). Out of scope: G10's CLI read (deferred per the design), suite editing beyond the existing verbs.
