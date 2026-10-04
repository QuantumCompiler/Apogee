# Base-model sessions: honest, clean, and tool-free

**What / why.** The user's transcript (2026-10-03, `Gemma4-E4B-Q4KM` — the base model pulled and converted earlier that day) shows a session the product *knew* was compromised and let limp anyway: one dim warning at the top ("ships no chat template… continues text rather than answering, and cannot use tools"), then a confident fabricated temperature, a Bitcoin price with float garbage (`67,200.000000000005`), invented playoff answers — each presented exactly like a real answer — with **turn-marker fragments spilling onto the screen** (`<|end|`, `<|end|><|im|`) and **`--tools` left advertised to a model the warning itself said cannot use them**. The 2026-09-28 fix ("Base models said, and stopped", Milestone J) said the warning once and stopped the *guessed format's own* markers; this transcript is the evidence it was incomplete: a base model emits **foreign and truncated** markers from any family it ever saw in pretraining, and one scrolled-away warning does not carry a whole session. Three fixes, one item: the marker spill closed generically, tools honestly off, and the base state visible for the session's whole life. Deliberately **not** a gate — the open-models principle stands: any model the user supplies runs; honesty lives in the framing, never in a refusal.

**Core constraint(s).**
- **Open models, unchanged:** the session runs, every turn streams, nothing refuses. This item changes presentation and advertisement, not permission.
- **The marker fix is generic and censorship-safe.** For a **template-less** model: a trailing `<|…` fragment is held until it resolves (the hold-until-resolved pattern the thinking view and Markdown renderer already use) and never painted if the stream ends inside it; a complete marker from the known stop families ends the reply like the guessed format's own. For a model **with** a template, behavior is untouched — an instruct model legitimately *quoting* `<|end|>` must render it; the filter's scope is the template-less case, where markers are noise by definition.
- **Tools follow the truth already known:** no chat template ⇒ the registry is not advertised and `ask_user` (a tool) is withheld, with one clear notice at startup ("tools off: a base model has no tool format") — the same honesty `complete`'s nobody-to-ask rule shows. `--tools` is accepted, not errored: the notice explains instead.
- **The state outlives the scroll:** the base-model fact is visible for the session's life, not once — through the existing banner/status machinery, never a per-turn nag and never a transcript mutation (the saved session stays the model's text, byte for byte; the display rules never touch history — the standing contract).
- **One filter home:** the fragment-holding extends the existing streaming filters in `backends/` (`markup_filter`, the profile filters) — not a second scrubber in the view layer. Machine mode and pipes carry what they carry today for compatibility; the display contract is the CLI's (consumed decision — the renderer's view-not-transform rule).

**Seam + files.**
- `backends/markup_filter.h/.cpp` + `backends/model_profile.h`: the template-less marker mode — trailing-fragment hold across chunk boundaries, the known stop-marker families as reply enders, end-of-stream residue never emitted.
- `backends/llama_chat.cpp` / `llamacpp.cpp` (the detection + today's once-only warning): the base-model fact exposed on `ModelBehavior` so surfaces can act on it, not just print it.
- `agentloop/loop.cpp` consumers / `cli/chat.cpp`: tools and `ask_user` withheld on the behavior flag with the startup notice; the banner and status resting state carry the base tag for the session.
- Tests: recorded-stream replays (this transcript's byte patterns as fixtures — truncated `<|end|`, chained `<|end|><|im|`, fragments split across chunk boundaries) through the filter, plain and rendered; the quoting guard (an instruct-templated stream containing literal `<|end|>` renders it untouched); the tools-off notice and withheld registry asserted in `chat_test`.

**Reference (Ommi).** No analog — Ommi's allowlist meant a base model could never arrive, which is exactly why open-models Apogee keeps meeting problems Ommi never had. The predecessor is in-house: the 2026-09-28 base-model fix (Milestone J), extended here with the transcript that found its edges.

**Decisions made** (dated):
- 2026-10-03 — Asked for by the user from the live transcript ("very poor performance"), placed in **Maintenance** at their direction.
- 2026-10-03 — Moved back into the **v0.1.3** tail as **26r** (the user's call, later the same day), re-numbering Maintenance's remaining M5/M6 to **M4**/**M5**.
- 2026-10-03 — **Framing, not gating:** a refusal or a confirm-to-continue would contradict the recorded open-models principle; the fixes are clean output, honest tool advertisement, and a visible state.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): the indicator is the banner line plus the status line's resting state carrying `base model`; answers are never decorated.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): the marker families live in the profile registry (ChatML `<|im_*|>`, `<|end|>`-style, `<|eot_*|>`-style); new ones join the registry, not the filter.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): machine mode is untouched — a driver sees today's bytes; the `session` event's fields stand.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): the warning gains the missing sentence — a base model's answers are continuations and may be confidently wrong.

**Guardrail(s).**
- The replay fixtures: no `<|` fragment ever reaches the painted screen from a template-less stream, across every chunk split, plain and Markdown-rendered — mutation-tested.
- The censorship guard: a templated model's literal `<|end|>` in prose renders exactly.
- Tools: with no template, the request carries zero tool definitions (asserted at the wire recorder), and the notice prints once.
- The saved transcript byte-equals the model's output in all cases — the display never leaks into history.
- On real weights (live, recorded on ship): a base build from [the model families](../../assistant/DEVELOPER.md#on-real-weights-the-model-families) where one is installed — the capability is a clean screen (no marker bytes) and tools reported off, for a full multi-turn session.

**Acceptance criteria:**
- [ ] Replaying the motivating session's model: no `<|…` fragments anywhere on screen, `--tools` answered by the startup notice with zero tools advertised, the base tag visible at the banner and status line throughout — and the model still streams its (wrong) answers, ungated.
- [ ] An instruct model quoting `<|end|>` mid-answer renders it verbatim; its session carries no base tag and keeps its tools.
- [ ] The saved session for both cases is byte-identical to the models' raw output.
- [ ] `models info` and the conversation warning agree on the one wording, including the confidently-wrong sentence.

**Scope note.** Item **26r**, earmarked for **v0.1.3** (the release's end, after 26q); gated on nothing pending. Out of scope: any refusal or confirmation gate (contradicts the open-models principle); guessing a *better* chat template (the existing guess stands); the attachments relevance floor the same transcript brushed (excerpts injected at `top 0.032` — a retrieval-quality question for the attachments machinery, its own item if the user wants it); machine-mode changes.
