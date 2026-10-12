# Thinking summaries: the gist, not the stream

**What / why.** The thinking block shows the model's raw reasoning as it streams — the terminal paints "a window of the last two wrapped lines, repainted in place" (`views/thinking_view.h:25`), and the shell's session view draws "the reasoning's last rows" (the 32c record) — verbose text where a reader wants the gist. This item makes **a one-line summary the default display**: a fourth utility chore, `reasoning_summary`, watches the accumulating reasoning and keeps one short present-tense line current (*"comparing the two config paths"*), both painters drawing it in the raw window's place, the `✻ Thought for Ns` fold unchanged. It works across both worlds by routing, not by hope (the thinking-summary spike, 2026-10-11, all evidence by `file:line`): **local models** (and the Ollama CLI's DeepSeek-style streams, already isolated by the thinking demultiplexer) emit genuinely raw chain-of-thought — the chore summarizes it on the utility role's member, itself local and free; **cloud vendors mostly never send raw thought** — the Anthropic wire's `thinking`/`redacted_thinking` blocks (`anthropic_wire.cpp:74–85`) carry vendor-summarized text, which is displayed as-is since re-summarizing a summary wastes a call. A `ModelBehavior` fact says which world a stream is from.

**Core constraint(s).** **Spend-honest by the house idiom**: the chore runs only on an **unmetered** member (the `embedding_is_metered` pattern, `contracts/provider.h:100–104` — "unknown is metered"); with no unmetered utility member the raw window stays, said dimly once per turn — honest degradation, never a silent cost and never a paid API call for a display nicety. **One seam**: reasoning already crosses `Reporter` as chunks (`agentloop/reporter.h:33–42`); the summary is one new event on that seam, drawn by the terminal and the shell — **the wire is unchanged** (machine mode and `serve` keep raw reasoning, `json_reporter.cpp:49`; the 26n side-call precedent — skipped and said per [ADR 0002](../../adrs/cli/mode-parity.md)). The already-summarized fact crosses as **plain `ModelBehavior` data** (never a type test). The chore is bounded (cadence, input slice and output length all named constants), cancelled with the turn, and its member call rides the existing chore machinery (26b) — no new calling path. The raw view stays reachable (the toggle below).

**Seam + files.**
- `lib/src/cli/source/business/agentloop/reasoning_summary.h/.cpp` — the fourth chore beside `query_rewrite`/`tool_summary`/`thinking`: the cadence (a summary every few seconds of active reasoning), the input slice (the tail of the accumulated reasoning), the one-line format, the unmetered gate, the previous line standing while the next computes.
- `agentloop/reporter.h` + the loop — `on_thinking_summary(line)`; `agentloop/loop.cpp` wiring the chore between `on_thinking` and the answer.
- `data/contracts/provider.h` — the `ModelBehavior` fact (reasoning already summarized); `data/backends/anthropic.cpp` sets it per its models; others as their wires warrant.
- `presentation/views/thinking_view.h/.cpp` — the summary line in the window's place (raw mode keeps today's path); `presentation/tui/session_view.cpp` — the same slot on the stage.
- `presentation/cli/` — the config knob `cli.thinking_display: summary|raw` through the one editor, `/thinking-display` in the one command table (Tab-completed, both front ends).
- Tests: the chore's cadence/format/gate table over scripted reasoning; both painters' goldens (summary standing, raw toggled, the fold unchanged); the routing table (raw-CoT backend → chore; summarized-behavior backend → as-is; no unmetered member → raw + the one notice); machine-mode conformance byte-stable (the wire untouched).

**Reference.** 26b — the chore shape and formats (the home this extends); 26i — the thinking switch (`/think`) this composes with, untouched; 26n — side-call narration and the wire-omission precedent; 32c — the session view's block; the thinking-summary spike (2026-10-11) for every measurement above.

**Decisions made** (dated):
- 2026-10-11 — The item lands in v0.1.6 (the user's placement call on the spike report; 43 by the ever-assigned rule).
- 2026-10-11 — **The summary is the default display** — the request itself ("a summary … instead of the verbose text"); raw stays one toggle away.
- 2026-10-11 — **Both worlds by routing** (the user's question, answered the same day): raw chain-of-thought (local models, Ollama-style streams) gets the chore on an unmetered member; vendor-summarized thinking displays as-is; the `ModelBehavior` fact decides — and the asymmetry (vendor granularity vs. our one-liner) is accepted.
- 2026-10-11 — **The wire is unchanged**: machine-mode drivers and served streams keep raw reasoning and make their own display decisions (the 26n precedent), skipped and said.

**Open calls:**
- [default: compressing vendor summaries to our one-liner when a free local utility member exists stays **out** (the offered option, not taken) — vendor summaries display as-is; a veto here flips one gate in the chore.]
- [default: the cadence, input slice and line length are named constants — roughly a refresh every few seconds of active reasoning, one line ≤ ~80 characters; exact values at build.]
- [default: the degradation notice is one dim line the first time a turn falls back to raw, not per-chunk.]

**Guardrail(s).** The chore table (cadence, bounds, the unmetered gate — a metered-only install runs no chore and shows raw, the planted case); the routing table over fake providers (behavior fact set/unset); both painters' goldens for summary, raw, toggle mid-turn and the fold; `cli.machine_schema_conformance` byte-stable; the leak test's surfaces gain the summary line; an optional real-weights pass per the model families (one family, one reasoning-heavy prompt, the summary line present and sane, recorded).

**Acceptance criteria:**
- [ ] A local model's reasoning shows as a rolling one-line gist in the terminal and the shell; the fold and `/think` behave as today.
- [ ] An Anthropic session displays its vendor-summarized thinking as-is, chore-free; the behavior fact routes it.
- [ ] No unmetered utility member → the raw window and one dim notice; no paid call is ever made for the summary.
- [ ] `cli.thinking_display` and `/thinking-display` flip the display live in both front ends; machine mode and `serve` are byte-stable.

**Scope note.** Earmarked for v0.1.6; ungated. Out of scope: any wire or protocol change, compressing vendor summaries (the recorded default-out), compaction or history changes — the summary is display-only and never enters the conversation.
