# `config scan --register`: the store's models, registered in one pass

**What / why.** The user's ask (2026-10-08): the local-store twin of the shipped provider chain — **`apogee config scan --register`**, with feature parity to `apogee providers scan --register`'s shape. Where `providers scan` reads the host (CLIs installed, keys resolving) and `--register` writes the provider entries, **`config scan` reads the model store** — every runnable set of weights on disk — and `--register` registers each unregistered one as a backend **under its default name**. Both halves of the machinery shipped and need only composing: **M7's store-aware fill** already turns one stored model's name into its type, path and projector (`config add-backend <name>`, `config_cmd.cpp` — "a name the model store knows fills what the flags leave open"), and **M3's naming rule** already produces the default names the user's way (`<base>-F16`, `<base>-Q4KM` — the chain's registration names since 2026-10-03). The parity contract, mirrored from the providers verb deliberately: a plain `config scan` prints a row per store model with its state (`backend: Gemma4-12B-Q4KM`, or `not registered` when it could be) and the evidence beneath (the file, its family, its level), ends with the pointer to `--register` when anything is registerable; `--register` writes every entry through the one config editor, **skips a taken name and says so** (the 28b precedent), is idempotent (a second run registers nothing and says why), and with no config file the scan still runs and names `apogee config init` while `--register` refuses naming it — each behavior the providers verb already pinned.

**Core constraint(s).**
- **Composition, not new machinery:** the per-model registration is M7's fill and M3's naming, called in a loop — never a second type-detection, path-derivation or naming implementation. A divergence between what `config scan --register` writes and what `config add-backend <name>` writes for the same model must be unrepresentable (same core, asserted by test).
- **One config mutation path:** every entry through the comment-preserving editor, one entry per model, the file's comments and layout untouched around them; the whole batch is N ordinary appends, not one bulk rewrite.
- **What registers is what runs:** GGUF files (llamacpp type, one backend per quantization level — each its own entry, as M3's chain registers them) and MLX directories (mlx type, 27b's rows). SafeTensors snapshots are **skipped and counted** — weights, not runnable backends (M4 already folds them as `converted`); the scan's summary says how many and why.
- **Skip-and-say, never overwrite:** a name already taken (by the same model — already registered — or by anything else) is skipped with the reason on its row; `--register` never edits or replaces an existing entry, never guesses an alternative name. The remediation for a collision is the existing one: `config add-backend <name> --name <other>` by hand.
- **No probes, no loads:** the scan reads the store's records and headers the way `models list` does (M2's buffered reads) — no model is loaded, nothing touches the network, and the scan is fast enough to need only the busy line M1 already gives slow commands.
- **Parity with the providers verb is behavioral, not cosmetic:** the no-config behaviors, the row-plus-evidence shape, the `--register` pointer, idempotence, and completion (per [ADR tab-completion](../../adrs/cli/tab-completion.md)) all mirror; where the two verbs must differ (the store has no "tier", models have levels), the difference is stated in help, not discovered.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; tests in the layer of what they test ([ADR](../../adrs/cli/tests-mirror-architecture.md)).

**Seam + files.**
- `presentation/cli/config_cmd.cpp`: the `config scan` subcommand — the store walk (via `modelstore/`'s one declaration and the lineage read, so folded snapshots are skipped exactly as `models list` folds them), the row rendering, the `--register` loop over M7's fill core; registered in the one command table with completion.
- `presentation/operations/` (if the scan core belongs beside the other shared cores): the scan's row model, so a future admin-plane read renders the same facts — the A4 pattern, decided at build time by where the fill core already sits.
- `business/models/` + `modelstore/`: consumed as-is — the store declaration, the records, M3's naming helper, M7's fill; any gap found (a naming helper not yet callable in isolation) is carved there, never duplicated in the command.
- Tests: `tests/presentation/cli/` — the scan goldens over a scripted store (registered / unregistered / name-collision / SafeTensors-only rows, the summary counts), the register loop (entries byte-identical to N hand-typed `config add-backend <name>` calls — the composition pin), idempotence, no-config behaviors mirroring the providers verb's, completion.
- Consumes: M3 (shipped — the naming rule, `--base-name`'s derivation), M7 (shipped — the fill), M4 (shipped — lineage folding), 27b (shipped — MLX store rows), 28b (shipped — the skip-and-say and no-config precedents), M1/M2 (shipped — the busy line, fast header reads).

**Reference.** In-house throughout: `providers scan --register` (the shape this verb mirrors, deliberately, down to its refusal wording pattern), M7's store-aware `add-backend` (the per-model core), M3's chain registration (the default names and the one-backend-per-level rule). The distinction from [M13](provider-model-rosters.md) (pending, this table — 32f at birth, moved here the same day) is clean and worth stating: M13 discovers **cloud** rosters from the vendor's endpoint; this verb registers **local** weights from the store — no overlap, and a user who runs both ends with everything addressable.

**Decisions made** (dated):
- 2026-10-08 — Asked for by the user with the spelling given (`apogee config scan --register`); placed in **Maintenance** (their call), M12 by the ever-assigned rule.
- 2026-10-08 — The spelling is the user's call and stands as given: `config scan` beside `providers scan`, the parity the ask names. The help text cross-references the two.
- 2026-10-08 — Default names are M3's rule verbatim — the chain's registration names are the store's default names, one source; a `--base-name` per model is out of batch scope (the hand verb has it for the one-off case).
- 2026-10-08 — SafeTensors snapshots skipped-and-counted: a backend must be runnable, and the store already records what a snapshot became.

**Open calls:**
- [default: the scan registers everything unregistered in one pass with per-row results — no interactive per-model confirmation; `config scan` without `--register` *is* the preview, which is exactly the providers verb's split]

**Guardrail(s).**
- The composition pin: for a scripted store, `config scan --register`'s config file is byte-identical to the file N hand-typed `config add-backend <name>` calls produce in the same order — same core, proven, not promised.
- The scan goldens: every row state (registered, unregistered, collision, SafeTensors, MLX) with its evidence lines and the summary; the second run's idempotent output.
- The parity table: no-config scan runs and names `config init`; no-config `--register` refuses naming it; completion offers the subcommand and flag — each mirroring the providers verb's pinned behavior.
- The editor discipline: the batch's appends leave a comment-dense fixture byte-identical outside the appended entries.

**Acceptance criteria:**
- [ ] On a store like the user's (mixed GGUF levels, MLX dirs, SafeTensors snapshots): `apogee config scan` prints a row per runnable model with its state and evidence and ends naming `--register`; `apogee config scan --register` registers every unregistered one under its M3-rule default name, skips collisions and snapshots with reasons, and prints the summary.
- [ ] `apogee chat -m Gemma4-12B-Q4KM` (or any newly registered name) runs immediately after; `models list` shows every registered backend exactly as if each had been added by hand.
- [ ] A second `--register` run writes nothing and says the store is fully registered; the config file is untouched byte-for-byte.
- [ ] With no config file: the scan runs and names `apogee config init`; `--register` refuses naming it — the providers verb's exact split.

**Scope note.** Maintenance item **M12**; gated on nothing. Out of scope: cloud model rosters ([M13](provider-model-rosters.md)'s concern); extending the first-launch offer (28b's providers-only offer stays as shipped — a later item if wanted); per-model naming flags in the batch (`--base-name` stays on the hand verbs); registering SafeTensors (not runnable); any change to the providers verb itself.
