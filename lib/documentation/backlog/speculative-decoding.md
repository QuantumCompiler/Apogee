# Speculative decoding, measured before it is built

**What / why.** Faster generation for a local model by drafting several tokens cheaply and verifying them in one pass of the large model. The pinned llama.cpp supports several drafters through `common/speculative.h`:
- **MTP**: the model's own multi-token-prediction head, with no second model. Qwen3.8-27B ships one; its Q4_K_M carries `qwen35.nextn_predict_layers = 1` and the four `blk.64.nextn.*` tensors.
- **A separate small draft model** of the same family.
- **Several n-gram methods** that draft from the text already in context, suited to editing and quoting.

Generation is the slowest part of a local answer (10–17 tokens/s on the reference machine's 27B), so a real speed-up here is worth a lot. **But the first measurement did not show one**, which is why this item begins with a measurement and may end there.

**The evidence so far (2026-09-25, llama-server on the pinned tree, Qwen3.8-27B Q4_K_M, greedy, thinking off):**

| Method | Prose | Code | Edit (copy-heavy) |
|---|---|---|---|
| None | 16.7 tok/s | 13.6 | 12.0 |
| MTP | 10.5 (55% of drafts accepted) | 13.6 (87%) | 13.8 (99%) |
| n-gram (`ngram-mod`) | 9.8 | 6.4 (5%) | 8.3 (78%) |

The GPU was shared with a terminal and a monitor drawing on it (55–83% busy with no model running), so these numbers are indicative only. Even at 99% acceptance the gain was 15%. A hybrid model's recurrent state must be rolled back for every rejected draft, which is the likely cost; a pure-attention model may fare differently.

**Core constraint(s).**
- **Build only on a clean win.** The build half of this item proceeds only if a measurement on an idle GPU shows at least 1.3× on one of the acceptance models (Qwen3-VL-8B, Qwen3.8-27B), for at least one method, across prose, code and editing. Otherwise the measurement is recorded in MILESTONES, and the item is closed as measured and not worth building.
- **Output is unchanged.** Under greedy sampling, speculative output must equal plain output token for token; the sampled case must keep the target distribution, which upstream's verifier guarantees.
- **Composes with [checkpoints](hybrid-prompt-checkpoints.md) and the [prompt cache](persistent-prompt-cache.md).** Rejected drafts roll back through the same state machinery, never a second copy of it.
- **Off unless it wins, per backend.** A `speculative:` setting on a llamacpp backend (`mtp`, `draft:<backend>`, `ngram`) defaults to off. `models info` shows whether a GGUF carries an MTP head.

**Seam + files.**
- **Measurement first:** a script under `lib/src/cli/tests/` (not in the suite; run by hand like the spike's battery) driving llama-server built from the pin, with the methods above on an idle GPU. It records tokens/s and acceptance per method and task.
- **If it earns a build:**
  - `backends/llama_real.cpp`: `common_speculative_init_from_params` and the draft-verify loop in generation.
  - `backends/llamacpp.cpp`: the setting, and the `generate` loop accepting multiple tokens per step (the per-token callbacks unchanged);
  - `commands/models.cpp`: the MTP head's presence.

**Reference (Ommi).** None; Ommi never used speculative decoding.

**Decisions made:**
- 2026-09-25 — Proposed in the small-models review under "Speed and memory", which the user selected, with measurement first.
- 2026-09-25 — The first measurement is the table above: indicative, and not enough to build on.

**Open calls:**
- [default: the threshold is 1.3× on one acceptance model across all three task types] Less than that does not repay the complexity it adds to the generation loop.
- [default: measure MTP, a Qwen3-0.6B draft for Qwen3-VL-8B, and `ngram-mod`] The three families of method the pin supports that need nothing new downloaded beyond a small draft.

**Guardrail(s)** *(if built)*.
- Greedy output identical with and without speculation on a fixed prompt set.
- Rollback leaves the context correct (the checkpoints item's equivalence test, with speculation on).
- Off by default.
- The setting is refused on a model without the drafter it names.

**Acceptance criteria:**
- [ ] The idle-GPU measurement is run and recorded in MILESTONES, whatever it shows.
- [ ] *(only if it meets the threshold)* The winning method is available per backend, off by default, and shows its measured speed-up on the acceptance model.

**Scope note.** Phase 4, item **25k**; build after 24b (`llama-common`) and 24c. The build half is conditional on the measurement. Out of scope: training a draft model, and EAGLE-style heads that need separate weights.
