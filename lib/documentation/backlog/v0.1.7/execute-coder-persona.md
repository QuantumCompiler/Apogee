# The coder persona: a shipped system prompt for the project session

**What / why.** A Claude-Code-class session is steered by a substantial coding system prompt; Apogee ships none — `-s/--system` exists, but nothing fills it, and agent policies are tasks-only (measured 2026-10-11, the execute-mode spike's W6: `chat --help` carries no `--agent`). This item ships the **coder persona**: a bundled, model-agnostic coding system prompt that `execute` uses by default when opened in a project (41b's detection), with `-s` winning whole when given. The persona carries the working rules a coding session needs stated once — read before editing, prefer exact-string edits, keep diffs minimal, run checks before claiming done, ask before destructive acts, cite files by path — written for the whole provider range: a cloud frontier model and a Q4 local model alike, so it names the tools generically and assumes nothing vendor-shaped. Content plus one wiring point: no new verbs, no gate changes.

**Core constraint(s).** The persona is **transient system context** — it rides requests the way the environment note does, is never saved into the session's user content, and composes with 41b's project context (the persona states *how to work*, the project file states *this project's rules*; the project file wins on conflict by riding closer to the conversation). The **asset pipeline is the law**: the prompt text compiled in via the generators, byte-identity-tested against its `assets/` file. **Model-agnostic by construction**: no vendor names, no tool-dialect assumptions, no length a small local window cannot carry (the text is budgeted like all system context, 26c). `chat` is untouched; execute outside a project is untouched; `-s` replaces the persona entirely, said on the banner.

**Seam + files.**
- `lib/src/cli/assets/prompts/coder-system.txt` — the persona text; the generator regenerates the compiled-in copy.
- `lib/src/cli/source/presentation/cli/execute.cpp` — the wiring: in a project, no `-s` → the persona; `-s` → the user's text; the banner line naming which.
- Tests: the persona rides requests (FakeTransport capture: present in a project, absent outside, absent under `-s`); byte-identity; the session file never holding it; the budget counting it.

**Reference.** The environment note (the transient-context precedent); the bundled agents' prompts (`assets/prompts/` — the asset shape); 26c (the budget every system text bows to); the execute-mode spike (2026-10-11) for W6; external prior art: Claude Code's system prompt (the genre: working rules, tool etiquette, honesty about results — never copied, the genre only).

**Decisions made** (dated):
- 2026-10-11 — The set lands in v0.1.7 as track 41 (the user's placement call on the spike report).
- 2026-10-11 — The persona applies to **execute in a project** only — chat stays unopinionated, and a bare execute outside a project opens as today (the spike's design, standing).
- 2026-10-11 — `-s` replaces the persona whole; there is no merge of the two (one steering text, the user's when given).
- 2026-10-13 — The persona's exact text is the build's craft within this document's rules, reviewed against one local Q4 model and one cloud model behaving sanely on the same fixture task before ship (the default, confirmed 2026-10-13).
- 2026-10-13 — A general `--agent` policy for sessions stays out of this item — a later item's question if wanted (the default, confirmed 2026-10-13).

**Guardrail(s).** FakeTransport goldens for the three states (project / outside / `-s`); byte-identity of the compiled-in copy; the budget's accounting includes the persona; `chat`'s requests byte-stable; an optional real-weights sanity pass per the model families (one family, the fixture task, result recorded).

**Acceptance criteria:**
- [ ] Execute in a project carries the persona; outside, nothing; under `-s`, the user's text — each said on the banner.
- [ ] The persona is compiled in, byte-identical to its asset, budgeted, and never saved as user content.
- [ ] `chat` and non-project execute byte-stable; the full suite green.

**Scope note.** Earmarked for v0.1.7; gated on 41b, the project opening -- **shipped 2026-10-13** ([MILESTONES.md](../../assistant/MILESTONES.md#milestone-ak--the-coding-mode) → Milestone AK), so the gate is satisfied (the persona rides the project detection, `cli/project_context`). Out of scope: a `--agent` for sessions, any chat change, per-model persona variants.
