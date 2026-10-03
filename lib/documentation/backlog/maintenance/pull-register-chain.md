# One command from pull to runnable: `--register` and `--register-with`

**What / why.** Getting a full-weight model from Hugging Face to a chattable backend is three commands today, each typed after watching the last one finish — the user's own transcript (2026-10-03): `models pull … --safetensors`, then `models convert … --type f16`, then `models quantize … --type Q4_K_M`, then still a `config add-backend`. Every stage already exists as a shipped core; this item chains them: **`apogee models pull <ref> --safetensors --register`** runs pull → convert (F16, projector included) → register, and **`--register-with Q4_K_M[,Q5_K_M,…]`** additionally quantizes the F16 into each listed level — one command, keys in hand at the end. The same transcript surfaced a discipline break that rides along because clean chain output depends on it: **`models quantize` leaks llama.cpp's raw metadata dump** (`llama_model_loader: Dumping metadata keys/values…`) straight onto the terminal, where `convert` reports cleanly — llama's log callback gets routed through the progress seam like everything else.

**Core constraint(s).**
- **A chain of shipped cores, not a new path:** pull is the acquisition ladder, convert and quantize are the existing engines with their staging rules, registration goes through the one config editor — the chain is orchestration in `commands/`, and each stage's own guarantees (`.partial` → verify → commit; Ctrl-C cleans staging; hash-named store homes; the projector carried to quants) hold unchanged because the chain calls the same functions.
- **Per-stage honesty, resume on failure:** each stage reports as it runs, and a failure mid-chain leaves every earlier artifact intact in the store and prints the exact command that resumes from the failed stage — never a torn-down chain, never a silent partial success.
- **Warnings propagate once, decisions don't change:** the base-model note (no chat template) and a failed projector appear in the chain's summary exactly as they do standalone — and a base model still registers, with the convert-the-`-it`-release hint beside its backend name.
- **No network after the pull stage;** the whole chain after it runs offline, and zero model calls anywhere (conversion and quantization are not inference).
- **Config edits are byte-disciplined:** every `add-backend` through the comment-preserving editor; role pointers untouched; names collision-checked before the chain starts so stage one never runs for a chain that would refuse at stage four.
- **One status discipline:** stage progress through the standing status/busy machinery ([M1](../../assistant/MILESTONES.md#milestone-g--the-terminal-ux-layer)'s busy line, shipped 2026-10-03, for a stage with no progress of its own; `download_progress` and convert's reporting where they have it), and the quantize log-leak fix means **no third-party library writes raw lines to the terminal** on any `models` path — the captured-stderr rule, applied to an in-process library's logging.

**Seam + files.**
- `commands/models_pull.cpp`: the `--register` / `--register-with <levels>` flags (comma or repeatable), the stage orchestrator (a small plan over closures: pull, convert, quantize×N, register×M — the promote-plan shape in miniature), the resume-command printing, the upfront name-collision check.
- `models/quantize.cpp` (or the llama boundary it calls): the log callback routed to the progress sink; nothing else about quantization changes.
- `commands/helpers.cpp` / config editing: backend names `<model>-<quant>` (the default below) through the existing add-backend core.
- Tests: the orchestrator's stage table over injected stage closures (success, each stage failing, resume from each — golden summaries incl. propagated warnings); the quantize-silence check (a quantize run's terminal output is Apogee's own lines only, asserted on a fixture GGUF); config golden for a two-quant chain's registrations.

**Reference (Ommi).** No analog — Ommi had no open acquisition and no conversion ladder to chain. In-house precedents consumed: the acquisition ladder and store (Milestone N), convert/quantize with the projector rule (2026-09-23/24 entries), the promote plan's closure-driven staging (Milestone Z), and the one config-mutation path.

**Decisions made** (dated):
- 2026-10-03 — Asked for by the user with the flag shape (`--register` = F16; `--register-with <levels>` = additionally those quants), from the three-command transcript above. Placed in the new **Maintenance** category at its creation.
- 2026-10-03 — The quantize log leak rides along: the chain's output contract is unbuildable while a stage dumps raw library logs, and it is the same seam either way.

**Open calls:**
- [default: every artifact produced gets a backend — `<model>-F16`, `<model>-Q4_K_M` — so the user picks at chat time and deletes what they don't want; the other reading ("register only the listed quants") is one flag away later] What registers.
- [default: F16 is always produced and kept (it is the quantization source and the training input); retention is the user's, via `models delete`] The F16's fate.
- [default: `--register*` without `--safetensors` is refused naming the dependency — the chain is defined for full-weight pulls; a GGUF pull is already runnable] Flag composition.
- [default: the post-pull hint, when `--register` was *not* passed, now suggests it next time] Discoverability.

**Guardrail(s).**
- The stage table, exhaustively: every stage's failure leaves prior artifacts intact and prints the resuming command; mutation-tested where convention applies.
- The quantize-silence assertion on a fixture GGUF: zero non-Apogee lines on stdout/stderr at default verbosity.
- Config byte-goldens: a chain's registrations are exactly the entries `add-backend` would write by hand, pristine comments kept.
- On real weights (live, recorded on ship): one full-weight pull from [the model families](../../assistant/DEVELOPER.md#on-real-weights-the-model-families) chained to a registered quant and chatted with — the capability is pull-to-first-token in one command.

**Acceptance criteria:**
- [ ] `apogee models pull <ref> --safetensors --register` ends with a registered F16 backend and prints each stage as it ran; `--register-with Q4_K_M` adds the quant and its backend — one command, no further typing.
- [ ] Killing the chain mid-quantize leaves the snapshot and F16 in the store and prints the exact resume command, which completes the chain.
- [ ] `apogee models quantize` (standalone or chained) no longer prints llama.cpp's metadata dump — its terminal output is Apogee's own reporting only.
- [ ] A base-model chain registers, with the no-chat-template note in the summary and beside the hint.
- [ ] The config after a two-quant chain is byte-identical to the hand-typed equivalent.

**Scope note.** Maintenance item **M3**; gated on nothing pending. Out of scope: registering GGUF-direct pulls (already runnable; a `--register` there is a one-line follow-up if wanted); MLX targets (the 30 track's store row can join the chain when it exists); retention policy; parallel quantization.
