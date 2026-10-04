# Execute mode: a session opened with a suite

**What / why.** The user's vision (2026-10-04): "a new mode that is similar to chat called `execute` where instead of starting up a window with a model, you start it up with a suite." **`apogee execute --suite <name>`** is chat's sibling whose unit is the suite: the shipped session machinery whole — persistence, resume, the one command table, completion, the busy line, permission presets — with symphonies first-class: **`/play <symphony> [input]`** runs one (completable, narrated per stage), `/symphonies` lists what's available, and the banner names the active suite and its symphony count. **Bare input addresses the suite's root model exactly as chat does** *(recorded as the default, vetoable)* — execute is a superset of chat for the suite case, not a different conversation model — which is precisely what lets [orchestrator.md](orchestrator.md) land later as an upgrade (the root gains symphony tools) instead of a second loop. This is a **new core surface**, and it pays a surface's full bill: the dated SPEC revision (Core surfaces gains the fourth front-end — made with this set, 2026-10-04), mode parity stated below, and completion per [ADR tab-completion](../../adrs/cli/tab-completion.md) for the command, its flags, suite names and symphony names.

**Core constraint(s).**
- **Reuse, never fork:** execute shares chat's session core — one session/REPL machinery parameterized by mode, never a copied loop. A fix in chat's core is a fix in execute by construction (the one-concern-one-home rule at surface scale).
- **Suite-first by definition:** `execute` with no suite named uses the config default suite; with none configured it refuses with the remediation (`apogee config add-suite …`) — it never silently degrades into plain chat, because the mode's promise is the suite.
- **Mode parity, stated honestly** ([ADR mode-parity](../../adrs/cli/mode-parity.md)): suite *resolution* is already surface-identical through the one chain (27d's contract, `serve` included). The interactive session rides machine mode through the existing protocol — a play is carried in the events a turn already emits (stage narration as the side-call lines are), no new event types in this cut. `serve` remains what it is (an API, not a session surface); never-listens untouched.
- **Plays are session history:** a `/play` and its output land in the transcript like any exchange — resumable, compactable, recallable; the symphony's stages are narration, the output is the record.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; tests in the layer of what they test.

**Seam + files.**
- `presentation/cli/execute.cpp/.h`: the command — thin over the shared session core; the banner, the refusal, `--suite`/`--warm` (27e's admission and warmup fire here exactly as they do for `chat --suite`).
- `presentation/cli/chat.cpp` + the session core it opens: the parameterization seam (chat is the first consumer of its own generalization; byte-identical behavior for chat asserted).
- The one command table + `chat_completer`: `/play`, `/symphonies` with completion; the `__complete` resolver picks up suite and symphony names as the free rider it was built to be.
- `presentation/machine/`: the play's stage narration through the existing side-call event shape; nothing new on the wire.
- Tests: `tests/presentation/cli/` — the refusal and default-suite paths, banner goldens, `/play` end-to-end over scripted providers, chat-unchanged byte-equivalence; machine-mode transcript goldens for a played symphony.
- Consumes: [27q](symphonies.md) (symphonies to play), [27d](model-suites.md) (the suite, via 27q's gate), 27e's admission/warmup (shipped by then or riding the same seam), 26o (shipped — permission presets), M1 (shipped — the busy line).

**Reference (Ommi).** No analog — Ommi had one interactive surface and no suites. The in-house precedent is chat itself, deliberately generalized rather than copied; the SPEC's no-TUI lesson is untouched (execute is a CLI surface like chat, not a second front-end in the non-goal's sense).

**Decisions made** (dated):
- 2026-10-04 — Asked for by the user (the vision's surface); placed in **v0.1.4** with the set at their direction.
- 2026-10-04 — **Execute ⊇ chat for the suite case** *(recorded as the default, vetoable)*: bare input goes to the root model through the ordinary loop; symphonies are explicit via `/play` until the Orchestrator (27t) makes them model-reachable. One conversation model across both surfaces.
- 2026-10-04 — The SPEC revision rides this set, dated: Core surfaces grows the fourth front-end; the no-second-in-binary-front-end non-goal is not touched (that lesson is about TUIs/GUIs, and execute is a CLI command).
- 2026-10-04 — No new machine-mode event types: stage narration rides the side-call shape 26n established; a host sees a play as a turn with narrated side calls and an answer.

**Guardrail(s).**
- Chat-unchanged: the parameterization lands with a byte-equivalence run of chat's existing test battery — the generalization is invisible to chat.
- The refusal paths: no suite named and none configured → the stated remediation; a named suite that fails 27e's admission behaves exactly as `chat --suite` does (`--force` honored).
- Session lifecycle: a `/play`-heavy session resumes with its transcript whole; compaction treats play outputs as ordinary history.
- Completion goldens: command, flags, suite names, symphony names (ADR 0007's offer-is-a-contract bar).

**Acceptance criteria:**
- [ ] `apogee execute --suite research` opens a session whose banner names the suite and symphony count; bare input converses with the root model; `/play <starter> <input>` runs the symphony with per-stage narration and the output in history.
- [ ] The session resumes with plays intact; `apogee execute` with a config default suite uses it, and with none refuses with the remediation.
- [ ] A machine-mode driver sees a played symphony as an ordinary turn — side-call narration events plus the answer — with zero new event types.
- [ ] Chat's test battery is byte-identical before and after the session-core parameterization.

**Scope note.** Item **27s**, earmarked for **v0.1.4**; **gated on [27q](symphonies.md)**. Out of scope: the Orchestrator ([orchestrator.md](orchestrator.md)); any `serve`/HTTP session surface (the GUI direction consumes machine mode, per SPEC); new event types; a TUI (never).
