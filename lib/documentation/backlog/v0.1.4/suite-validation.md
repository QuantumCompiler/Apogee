# Rubber-duck validation: members checking each other's work

**What / why.** The user's ask: suite members should "rubber duck off of one another to validate input and output between the process." The spike (2026-10-03) found the prior art narrow and the policy absent — **W3**: cross-model checking exists exactly twice, both harness-wired (the rerank judge scoring retrieval, 26b; and 26f's grammars, which hold *structure* with no model call at all), and nothing validates the three places errors actually propagate: **tool arguments before execution** (a wrong `delete_file` path, a malformed `run_command`), **extraction output** (structured records that parse but lie), and **a final answer on request**. This item is the policy, per suite: a `validate:` block naming which seams are checked and by which member (a small-window helper as the verifier, briefed per [suite-consult.md](suite-consult.md)'s isolation contract), with **cheap-first ordering** as a hard rule — structural checks (26f grammars, schema parses, path-existence) run before any model is woken, because the spike's own evidence shows structure catching whole error classes for free (Llama-3.2-3B's captures went 17/20 → 20/20 on grammar alone). Disagreement is **bounded and honest**: one verifier objection → one revision by the producer → surfaced to the user with both positions if still disagreeing — never a silent retry loop, never a silent override. Off by default *(the spike's recommendation, recorded as the default)*: a suite opts in per seam; latency is a real cost and the user measures before defaulting.

**Core constraint(s).**
- **Cheap first, model last:** every seam's check pipeline is structural validators, then (only if configured, only if the structural pass cannot decide) the verifier member. A model is never asked what a grammar already answered.
- **Bounded rounds, by construction:** objection → revision → surface. The counter lives in the loop, not in convention; round three does not exist.
- **Honest surfacing:** a surfaced disagreement shows what the producer said, what the verifier objected, verbatim-where-short — presentation through existing surfaces (the thinking block line per 26n for passes; the answer path for surfaced disagreements), never a transcript mutation.
- **The verifier is briefed, not immersed:** the check rides 27f's brief-only contract (the artifact under check + the criterion, nothing else), sized to the member's small window.
- **Tool-argument checks gate execution, not permission:** a failed check returns the objection to the producer as a tool *result* (round one), it does not substitute for the permission system — gates and `session` answers (26o) are untouched.
- **Serial and budgeted** under 27f's same caps; validation spends from the same per-turn consult budget, so a suite cannot wedge itself checking.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only.

**Seam + files.**
- `agentloop/validate.h/.cpp` (new): the seam registry (tool-args, extraction, answer-on-request), the cheap-first pipeline, the bounded-round state; pure over injected checkers and the member-call closure.
- `agentloop/loop.cpp`: the tool-args hook (between selection and execution) and the answer-on-request path (`/check` or flag — see open call); `knowledge/`'s extraction call sites: the extraction hook.
- `harness/config` (27d's block): `validate:` per suite — seams on/off, the verifier member, through the one editor.
- Tests: `tests/agentloop/validate` — pipeline tables (structural catches → no model call, asserted by call counts; model disagreement → one round → surface), the budget interplay with 27f, goldens for the surfaced-disagreement rendering.
- Consumes: [27d](model-suites.md) (the suite, the verifier member), [27f](suite-consult.md) (the call mechanics and caps — this item adds *policy*, not a second model-calling path); 26f (shipped — the structural floor); the rerank judge's degradation honesty (shipped) as the reporting model.

**Reference (Ommi).** The judge shape is the analog — Ommi's training eval gate and rerank judge (both ported) are models scoring models, harness-wired; a *configurable* validation policy at interaction seams has no analog there.

**Decisions made** (dated):
- 2026-10-03 — Split from the suites spike as the policy layer over 27f's mechanics; one model-calling path (consult), two uses (delegation, validation).
- 2026-10-03 — **Opt-in per seam** is the recorded default from the spike's question 3 (the user may veto toward default-on for destructive tool arguments once latency is measured on real suites).
- 2026-10-03 — Cheap-first is a rule, not a preference: the 26f measurement (grammar alone fixing 17/20 → 20/20) is the evidence that structure eats most of the error class before a verifier is worth waking.

**Open calls:**
- [default: answer-on-request surfaces as `/check` on the just-given answer in chat — explicit, free of per-turn latency; a `validate: answers: always` config value exists for suites that want it standing] The answer seam's trigger.
- [default: extraction validation checks semantic sanity the grammar cannot (counts against source, required-field truthiness) with a fixed rubric brief — not free-form critique] The extraction rubric.
- [default: a verifier objection to a *tool argument* is returned to the producer as the tool result of round one; the tool itself runs only after a clean pass or the round limit with the user shown the dispute] Disposition.

**Guardrail(s).**
- Call-count pins: a structurally-caught error wakes no model (asserted); a clean structural pass with the seam off wakes none either.
- The round bound mutation-tested: no path exists to a third round.
- Budget interplay: validation and consults share the per-turn cap; exhausting it degrades to structural-only with a note, never to silence.
- On real weights, the capability and the measure: on [the model families](../../assistant/DEVELOPER.md#on-real-weights-the-model-families), a planted-flaw check — an extraction with one seeded wrong field — is objected to by the verifier and surfaced within one round, one family at a time.

**Acceptance criteria:**
- [ ] A suite with `validate: {tool_args: on}` returns a verifier objection to a bad `delete_file` path as the tool's round-one result; the revised call passes and executes; the permission gate behavior is byte-identical throughout.
- [ ] A malformed extraction never reaches the verifier (the grammar/schema check catches it first, call counts prove it).
- [ ] `/check` on an answer runs the verifier once with the brief-only contract and prints agreement or the two positions; round three is unreachable.
- [ ] With validation unconfigured, every pipeline is pass-through — wire and output byte-identical to today.

**Scope note.** Item **27g**, earmarked for **v0.1.4**; **gated on [27d](model-suites.md), [27f](suite-consult.md)** — not on the MLX items. Out of scope: multi-verifier quorums; validating harness-internal calls (titles, compaction — chores stay cheap); auto-retry beyond the one bounded round; training-track gates (they have their own, shipped).
