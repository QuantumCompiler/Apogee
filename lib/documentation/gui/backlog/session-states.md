# Session states and the input's reach

**What / why.** The design gives the conversation its full vocabulary — the states (`ScreenSessionNew`, `…Error`, `…Private`, `…Streaming`, the prototype's `chat:retry`, `chat:wal`, `chat:crash` starts) and the input's reach (attach a file, folder, image, recording or video; `@path` mentions; the context meter; switching model, thinking, effort or system prompt mid-session; the slash actions). The contract board maps each: attach and `@` are **ready** (`{"type":"attach"}` lines, outcomes as notices); the context meter is **app-side arithmetic** (`result.usage.input_tokens` against the window from `models info`, plus the budget notice); mid-session settings are the restart-with-`--resume` fallback until the CLI's `set` line (52d) lands; private is the `--no-recall`-plus-delete fallback until `--private` (52d); detach is hidden until its line (52d); recall names and source chips arrive with 52a's `memory`/`sources` events — each the board's own fallback, upgraded on landing, gated on nothing. This item builds those states and inputs over the shipped window: error and crash rendered in the CLI's words with retry, the new-session start state, attach/@ with outcome notices, the meter, the settings sheet (restart mechanics under it), and the slash actions as app actions (the board's "machine mode doesn't carry them" row).

**Core constraint(s).** ADR 0001/0002; the transcript stays the protocol's truth (states from events, never timers); fallback law as above — every G-dependent affordance has its board-stated fallback and its upgrade noted; private's fallback is said honestly in the UI ("deleted when closed", not "never written") until `--private` exists. Slash actions map one-to-one onto cores/lines that exist — none invents capability.

**Seam + files.** `source/presentation/session/` grown (states, the input bar's attach/@/meter, the settings sheet, slash actions); `source/business/session/` (restart-with-resume, attach lines, usage tracking). Tests: the view-model over scripted event sequences per state; attach/@ line emission pinned; the meter's arithmetic over scripted usage; the restart path; fallback renders.

**Reference.** The `ScreenSession*` boards, `ComponentsTurn`/`ComponentsPrompts`; the contract board's SESSIONS rows with fallbacks; [52a](../../cli/backlog/v0.2.0/session-event-fields.md) and [52d](../../cli/backlog/v0.2.0/session-inbound-lines.md) cross-referenced, not gated.

**Decisions made** (dated):
- 2026-10-20 — From the design-completion spike (the user's call); fallback-first, upgrades cross-referenced.

**Open calls:**
- [default: which slash actions ship first (the prototype's set) and their app mappings, at build.]

**Guardrail(s).** Per-state event-sequence matrices; line-emission pins; meter arithmetic; the private-fallback honesty string golden; hermetic.

**Acceptance criteria:**
- [ ] Every designed session state renders from its events; attach and `@` work with their notices; the meter tracks usage against the window.
- [ ] Mid-session settings apply via restart-resume and say so; private runs with the fallback said honestly; the full suite is green.

**Scope note.** Earmarked for v0.2.0; gated on [`session-library.md`](session-library.md) (1f). Out of scope: the CLI growths themselves (track 52), sources/recall chips' final form (upgrades when 52a lands).
