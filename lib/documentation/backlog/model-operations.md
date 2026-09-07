# Model operations: one role resolver and the `models` suite

**What / why.** The read-only half of model management: the **single shared resolver** for the three `models:` role pointers, and an `apogee models list / info / status` suite that reports what is on the machine — provenance, resolved profile, verification state, GGUF loadability. Nothing here downloads anything; acquisition is [model-acquisition.md](model-acquisition.md), which builds on this item's inspection and reporting.

**The role resolver is the part that must not be got wrong twice.** `harness/config.h` already declares `ModelsConfig` with `default_backend`, `default_embedding`, and `default_extraction`, and its comment already names this item as the resolver's home — until then callers "treat these as raw names and validate them at the point of use", which is exactly the state Ommi was in when the bug class appeared. **Ommi's CLI and its HTTP admin plane grew independent resolution chains and disagreed**; the fix was one exported function both call, each then checking the returned key is configured and reporting in its own idiom (fatal vs 400). Apogee has no HTTP plane yet, which is *why* the resolver must land as a shared function now: writing it as a private helper inside a command is how the second copy gets written when [serve-public-plane.md](serve-public-plane.md) arrives.

The precedence is fixed: **`-m` / request override > per-feature backend > role pointer > `models.default`**. With a role pointer unset the chain collapses to exactly the pre-role behaviour, so nothing that works today changes.

**Core constraint(s).**
- **One resolver, exported, used by every surface.** CLI, machine mode, and the future HTTP planes call the same function. A resolver that lives inside a command is a resolver that will be duplicated.
- **A resolver resolves; it does not validate.** It returns a backend key. Whether that key is configured is the caller's check, reported in the caller's idiom — that separation is what lets one function serve a fatal CLI error and a 400 response.
- **`check` reports the contract but never edits config** (the [⚠ One layout declaration](../assistant/CLAUDE.md#-one-layout-declaration-and-every-install-path-reads-it) rule). `--fix` may repair a local install; it may not decide what a dangling `model_path` meant.
- **A check that cannot run reports skipped, never passing.** GGUF inspection on a path that does not exist is "skipped", not "OK".
- **Header reads only.** Loadability inspection touches the GGUF header, never tensor data, so `check` and `models info` can afford to run it on every invocation.
- **Reporting is honest about uncertainty.** An unprofiled model says so; an unverified profile says so. Under the open-model policy there is no allowlist making any model a promise, so the display is the only place a user learns what Apogee actually knows.
- Mutating models actions do not exist in this item, so it adds nothing to the admin-plane parity table's documented-skip list.

**Seam + files.** New: `lib/src/cli/source/harness/roles.h/.cpp` (the one resolver; `harness/` because it is config-plane logic every surface reaches, and it includes nothing from `backends/`), `lib/src/cli/source/commands/models.h/.cpp` (the `models` command and its `list`/`info`/`status` subcommands, registered in `commands/registry.cpp`), `lib/src/cli/source/models/gguf_inspect.h/.cpp` (header read via llama.cpp's `ggml/gguf.h`). Extended: `commands/check.cpp` (a models section: configured paths resolve, headers parse, an unloadable GGUF fails with the exact repair command), `completions/` (the new subcommands reach tab completion through `apogee __complete` automatically — assert it). Tests: `lib/src/cli/tests/harness/roles_test.cpp` (table-driven precedence), `lib/src/cli/tests/models/gguf_inspect_test.cpp` against a tiny fixture GGUF, `lib/src/cli/tests/commands/models_test.cpp`.

**Reference (Ommi).** `lib/cli/src/harness/models.go` (665 lines — `ExtractionBackendKey` is the resolver, and its comment is the record of the bug class: "the ONE resolver both the CLI and the admin plane call, so they can never disagree"; `EmbeddingBackendKey` is its mirror, added 2026-08-16), `lib/cli/src/cmd/ommi/models.go` (627 — the `list` / `status` / `info` subcommand shapes and what each column is for), `lib/cli/src/gguf/gguf.go` (709 — a from-scratch GGUF reader), `lib/cli/src/ollama/transform.go` → `GGUFInfo` (the header-read result: `Parsed`, `ParseError`, `Architecture`, `Tensors`, `TextTensors`).

**Deliberate divergences.**
- **llama.cpp's `gguf.h` replaces Ommi's from-scratch GGUF reader.** In-process linkage means the 709-line parser is a dependency Apogee does not need to own — and more importantly, the header is then read by *the same code that will load the file*, so "the header parses" and "llama.cpp can read it" stop being two different claims.
- **The resolver is `harness/roles.h/.cpp`, a file of its own,** rather than functions hanging off the config struct as in Ommi. It is the seam two future surfaces must share; giving it a filename makes that visible.
- **No `approved` / allowlist columns** in `models list`. The open-model policy deletes them; `provenance` and `verified` carry the honest signal instead.

**Decisions made** (dated):
- 2026-09-06 — **Split out of the `model-profiles-and-management` guard document** as the second of four *(user call: four-way split)*.
- 2026-09-06 — **No hard gate on [model-profiles.md](model-profiles.md).** The role resolver is pure config plumbing and touches no profile; `models list/info` can list and inspect files without one. Where a profile is absent, `models info` reports the model as unprofiled — which is not a stopgap but exactly the permissive zero value the profile design already mandates. This deliberately breaks the guard document's "management gates on profiles" sequencing, because that rationale was about *acquisition* (pulled models get profiled), not about reporting or roles — and it means an agent can build this item while model downloads are still in flight.
- 2026-09-06 — Role resolution lands as a shared exported function **before** any second surface exists, specifically so there is never a moment when duplicating it is the path of least resistance.

**Open calls:**
- [default: `list` is a table, `info <name>` is detailed, `status` reports configured-vs-present] Subcommand division, mirroring Ommi's shape. `convert` / `convert-ggml` / `convert-lora` are **not** ported here — conversion is acquisition-adjacent and belongs in [model-acquisition.md](model-acquisition.md) or the training ring.
- [default: yes, gated on the machine-readable flag already shipped] Whether `models list` honours `--output-format stream-json` ([machine-mode.md](../reference/machine-mode.md)). A GUI listing models is the obvious first consumer beyond chat, and the flag exists.

**Guardrail(s).** The role-resolution chain **table-tested across every rung including the empty-pointer collapse**, so the pre-role behaviour is pinned as a fact. A test asserting the resolver has exactly one definition reachable from both a command and a non-command caller — the structural half of "CLI and HTTP cannot disagree", written now while there is only one surface to check. GGUF header inspection run in CI against a **tiny fixture GGUF** committed to the repo, so loadability is covered without a multi-gigabyte download. `check`'s models section asserted to report *skipped*, not passing, when a path is absent. Every guardrail mutation-tested before it is trusted.

**Acceptance criteria:**
- [ ] Role resolution is table-tested across the full chain, shared, and structurally provable as single-definition so CLI and future HTTP callers cannot disagree
- [ ] With every role pointer unset, resolution is byte-identical to today's `models.default` behaviour
- [ ] `models list` surfaces provenance (which source), resolved profile, verification state, and advisory runnable warnings
- [ ] `models info <name>` reports GGUF architecture, tensor counts, and header parse state; an unparseable header is reported as a failure with its reason, never as an empty field
- [ ] `check` gains a models section that fails an unloadable GGUF **with the exact repair command**, and reports *skipped* rather than passing when a configured path does not exist
- [ ] The new subcommands appear in tab completion on all four shells with no per-shell edit

**Scope note.** Gated ring, local-model depth, **second of four**. No hard gate — buildable today: it needs neither model weights nor [model-profiles.md](model-profiles.md) (see the 2026-09-06 decision above), and the `ModelsConfig` role pointers it resolves already ship in `harness/config.h`. Ring convention also assumes the complete v0.1.0 set. Out of scope: anything that downloads, deletes, repairs, or quantizes a model ([model-acquisition.md](model-acquisition.md)); the profile registry itself ([model-profiles.md](model-profiles.md)); the admin-plane twins of these routes, which arrive with [admin-plane-foundation.md](admin-plane-foundation.md).
