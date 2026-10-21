# Gemma 4 draft heads as MLX drafters

**What / why.** The user stores four Gemma 4 assistants: `Gemma4-12B-Assistant` (`Gemma4UnifiedAssistantForCausalLM`), and the 26B, E2B and E4B ones (`Gemma4AssistantForCausalLM`). Since 2026-10-21's draft-heads change, every conversion refuses them as draft heads, which is true of what they are: a few layers that propose tokens for their base model to verify, never a model of their own. The refusal was right for chat. What it closed off is the reason they exist: making their base faster. Wall **W5**. Measured, not guessed (2026-10-21, the MLX-for-every-kind spike):

- **mlx-vlm converts and runs them as drafters.** The MLX community's `gemma-4-E4B-it-assistant-bf16` was converted "using mlx-vlm version 0.4.5" (its model card). It runs beside its base with `python -m mlx_vlm generate --model <base> --draft-model <head> --draft-kind mtp --draft-block-size 6`, and `load_drafter(..., kind="mtp")` with block 3 for batches. The card says output at temperature 0 matches the base alone. mlx-vlm's README names `--draft-kind dflash|eagle3|mtp`. mlx-lm's own docs show no Gemma 4 drafting.
- **What it is worth is not measured here.** Google says its MTP drafters (for the 31B, 26B MoE, E2B and E4B) run "up to 3×" faster, its own claim. Apogee's one measurement of a head of this sort, Qwen's MTP head on llama.cpp (26k), found **1.14×**.
- **Apogee runs no drafting on MLX:** `assets/mlx/mlx_generate.py` has no draft path, by grep. Its text turns go through mlx-lm (`:849`).

**Core constraint(s).**
- **A drafter is never a model of its own:** an MLX conversion of a draft head records `drafter` (53e). It is never registered as a chat backend, never offered a role, and runs only beside the base its record names. The draft-heads change's refusals stand for GGUF and for chat.
- **Only the base it reads:** a drafter pairs with the stored base the draft-heads change already finds by family and width (`draft_head_bases`), never by name.
- **The answer is the base's:** drafting changes only how fast the answer comes. A turn's text at temperature 0 equals the base's alone, which the guardrail holds.

**Seam + files.**
- **`source/business/models/convert.cpp`, `source/presentation/cli/models_pull.cpp`:** the draft-head refusal lifted for `--mlx` alone, routing through mlx-vlm (53f's route) and recording `drafter` and its base.
- **`source/data/contracts/config*`:** a `draft: <path>` field on an `mlx` entry, written through the one editor (`config set-backend --draft`), with its completion (ADR 0007).
- **`source/data/backends/mlx_local.cpp`, `assets/mlx/mlx_generate.py`:** an entry with a drafter generates through mlx-vlm's speculative path (`draft_kind="mtp"`, the block size from the record or the config).
- **`source/data/modelstore/`:** the drafter's base, read from its record.
- **Tests:** a stub `mlx_vlm` drafter that records the pairing. The refusal kept for chat and GGUF. A drafter refused beside a base of another family or width. The temperature-0 answer equal with and without it (the stub's).

**Reference.** The draft-heads change (MILESTONES, 2026-10-21: the reader, `draft_head_bases`, the refusals). 26k (speculative decoding measured: Qwen's MTP head, 1.14×). [The MLX community's E4B drafter card](https://huggingface.co/mlx-community/gemma-4-E4B-it-assistant-bf16). [Google's MTP drafters post](https://blog.google/innovation-and-ai/technology/developers-tools/multi-token-prediction-gemma-4/).

**Decisions made** (dated):
- 2026-10-21 — **Drafters ride the base's MLX backend**, never their own. Pairing is a field on the base's entry.

**Open calls:**
- [user] **Build drafters now, or measure first?** Google's figure is "up to 3×", its own. Apogee's one comparable measurement found 1.14×. This item can either be built whole, or start by measuring the speed-up on the user's Mac with one base and its drafter through mlx-vlm, and be built only if the gain earns it. Building is several days of work. Measuring is one real-weights run the user makes.
- [default: the 12B unified assistant pairs only if the installed mlx-vlm drafts `gemma4_unified`, and is refused saying so otherwise. Google's drafters name the 31B, 26B, E2B and E4B.]

**Guardrail(s).**
- **The user's four configs as fixtures:** each records `drafter` and its base, is refused as chat and as GGUF, and pairs only with its base.
- **The temperature-0 answer** is the same with and without the drafter (the stub).
- **A real-weights speed measurement,** on the model families rules, is the user's run.

**Acceptance criteria:**
- [ ] `models convert Gemma4-E4B-Assistant/... --mlx` makes a drafter recorded beside `Gemma4-E4B-Instruct`, and `config set-backend <its MLX base> --draft <it>` pairs them.
- [ ] A turn on the paired backend answers as the base does. Its speed with and without, measured on the user's machine, is recorded.
- [ ] Chat and GGUF conversions of a draft head are refused as before.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** v0.2.0, gated on [53f](mlx-vlm-conversions.md) (mlx-vlm's route and its text path), and blocked by its `[user]` call. Drafting on llama.cpp, where Gemma 4's heads load as `GEMMA4_ASSISTANT`, is out of scope. The GUI's [3b](../../../gui/backlog/v0.2.0/mlx-engines-and-drafters.md) pairs a drafter in the app.
