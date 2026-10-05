# Symphony chains: composition without a second concept

**What / why.** The user's vision (2026-10-04): "Symphonies should be able to be chained together, where the output of symphony is passed into the input of the other." The design that keeps the model small: **a chain is itself a symphony.** [27q](symphonies.md)'s stage gains a second kind — beside a role stage, a stage may `play:` another symphony by name, its output threading onward exactly as a role stage's does. No chain object, no chain lifecycle, no chain surface: a chain is defined, listed, shipped, created and played with the machinery 27q already built, and composition nests to any mix of role stages and plays. What this item adds is the composition rules and their enforcement: **cycles are refused at definition time** (validation walks the play graph and names the loop — the gate convention's no-cycles spirit applied to definitions), nesting is **depth-capped**, budgets **aggregate across the whole walk** so a chain cannot multiply its way past the caps a single symphony honors, and a failure deep in a chain surfaces with its **position named** (`research → verify, stage 2`) rather than a bare error.

**Core constraint(s).**
- **Composition changes nothing about execution:** the runner recurses through the same stage walk and the same bounded-call core; a played symphony's stages are indistinguishable on the wire from inline ones. One runner, no chain engine.
- **Cycles unrepresentable in accepted definitions:** the validation walk runs at parse/create time and on config load — a definition that reaches itself is refused naming the cycle path, never discovered at play time.
- **Depth-capped by construction:** the cap lives in the walk, not in convention; exceeding it at *definition* time is refused like a cycle (the static walk knows), and spec files that cross-reference late-bound names are checked at play start before any model call.
- **One budget for the whole walk:** stage-call counts and token caps aggregate across nesting; a chain hitting a cap stops honestly with the position and the spent budget named, partial output discarded, never half-returned as the answer.
- **Narration tells the nesting:** 26n's stage lines carry the chain position, so a user watching a play always knows which symphony's which stage is running.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only.

**Seam + files.**
- `business/symphony/definition.h/.cpp`: the `play:` stage kind; the validation walk (cycle detection, static depth).
- `business/symphony/runner.h/.cpp`: recursion through the one walk; the aggregate budget threaded through; position-carrying errors and narration labels.
- Tests: `tests/business/symphony/` — composition tables (nesting depths, mixed role/play stages, output threading across the boundary), cycle refusals (direct, transitive, self), the depth cap at both refusal points, aggregate-budget stops with position goldens.
- Consumes: [27q](symphonies.md) (everything — this is a delta on its definition and runner).

**Reference.** The in-house precedent is the training pipeline's staged manifest (one walk, resumable positions), minus persistence: a chain play is one process invocation, not a resumable run.

**Decisions made** (dated):
- 2026-10-04 — Asked for by the user with the vision; split from 27q so the definition/runner item stays one focused session and composition's rules get their own tests.
- 2026-10-04 — **A chain is a symphony** *(recorded as the default, vetoable)*: the `play:` stage kind instead of a first-class chain object — one concept to define, list, ship and complete, and nesting falls out instead of being built.
- 2026-10-04 — Depth cap **4** *(the default, config-adjustable as the caps in 27f are)*: deep enough for real composition, shallow enough that the serial-latency bill stays legible.

**Guardrail(s).**
- The cycle tables: direct (`a` plays `a`), mutual (`a`↔`b`) and transitive loops each refused at definition time with the path named; mutation-test that no accepted definition can cycle.
- The aggregate pin: a chain whose inner symphony would individually pass its caps stops when the walk's aggregate crosses them — call counts asserted.
- Boundary threading: the outer stage after a `play:` receives the inner symphony's output verbatim (wire-recorded through the template rendering).
- Position goldens: the narration lines and the failure message for a stage-2-of-inner failure name `outer → inner, stage 2`.

**Acceptance criteria:**
- [ ] A chain of two shipped starters (`play:` stages) runs end to end with the first's output threaded into the second (wire-recorded), narrated with positions.
- [ ] Creating a definition that cycles — directly or through an intermediary — is refused naming the loop; a spec file that cycles is refused at play start before any model call.
- [ ] A nesting five deep is refused at definition time; four deep runs.
- [ ] A chain stopped by the aggregate budget reports the position and the spend, and returns no partial answer as if complete.

**Scope note.** Item **27r**, earmarked for **v0.1.4**; **gated on [27q](symphonies.md)**. Out of scope: conditional or branching stages (a chain is a straight walk in this cut); resumable/persisted chain runs (the task runner owns long-lived work); parallel plays.
