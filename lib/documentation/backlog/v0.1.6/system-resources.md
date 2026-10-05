# System resources: the machine, read honestly

**What / why.** The user's ask (2026-10-04, opening **v0.1.6**): access the system's resources — CPU, GPU, RAM usage and kin — with Apogee. Today the machine is read in exactly one place for exactly one purpose: the 26a machine budget feeds the context-window sizing (`kv_cache`'s `fits`), and 27e's admission math composes over the same numbers — but nothing *shows* the machine to the user, and nothing reads more of it than memory. **`apogee system`** closes that: a one-shot snapshot of **CPU** (model, core counts, load and a sampled utilization with its window named), **memory** (total, used, free; Apogee's own process footprint; and what Apogee holds resident — the providers' loaded models — separated clearly from system-wide numbers), **GPU** (what the platform can say cheaply and truthfully: on Apple silicon the device name and the unified-memory story; elsewhere an honest `unknown` in the first cut), and **disk** (the model store's footprint and the free space on its volume). Human table by default, `--output-format json` as one document (the 28h shape, shipped with v0.1.5), fast enough to need no busy line. The design rule is the one 27e wrote down: **composition, not a second estimator** — the new reads join the same platform seam the 26a budget uses, so the window sizing, suite admission and this surface all describe one machine, and a number none of them can know is `unknown`, never a guess.

**Core constraint(s).**
- **One machine truth:** the probe extends the platform seam the 26a budget reads — never a parallel estimator; a future consumer (admission, placement, the trainer) reads the same functions. Divergence between what `system` prints and what sizing uses must be unrepresentable.
- **The platform layer owns the OS** ([ADR layer-enforcement](../../adrs/cli/layer-enforcement.md)): `sysctl`/`host_statistics` on macOS, `/proc` on Linux, the Win32 counters on Windows, each behind one `infrastructure/platform` interface; no `#ifdef` leaks above it.
- **Honest per platform:** every field carries unknown-ness explicitly; a platform that cannot answer says so in that field and the command still succeeds — the 27e admission precedent (`unknown`, never guessed) applied to a whole surface. Windows's weaker story is stated, not papered over.
- **Zero model loads, zero network, no children** where the OS API suffices (no `nvidia-smi`/`system_profiler` spawning in the first cut); **never-listens** untouched; **no daemon, no watch loop** — one reading per invocation.
- **Apogee's own accounting is Apogee's to answer:** resident models come from the harness's `providers_` map (what is actually loaded, per backend), the process footprint from the OS — labeled as Apogee's, never mingled with system totals.
- **Secrets hygiene:** nothing here reads configs or credentials; the snapshot is hardware and process numbers only.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; tests in the layer of what they test ([ADR](../../adrs/cli/tests-mirror-architecture.md)); completion for the command and its flags per [ADR tab-completion](../../adrs/cli/tab-completion.md).

**Seam + files.**
- `infrastructure/platform/system_info.h/.cpp` (new): the probe — `MachineSnapshot` (cpu/memory/gpu/disk structs, every field optional-or-unknown), per-OS implementations behind the one header; the 26a memory read re-homed or delegated here so there is one.
- `data/modelstore/kv_cache` (shipped, 26a): its `fits` input documented as coming from the shared probe — the no-second-estimator pin.
- `business/harness/harness.h` (shipped): the resident-models read (backend name, model, resident or not — the 27e `models status` accounting, reused).
- `presentation/cli/system_cmd.h/.cpp` (new): `apogee system` — the table, `--output-format json` (one document), the per-field unknown rendering; registered in the one command table with completion.
- `presentation/views/`: the table painter rows (A3's painter/argv separation holds).
- Tests: `tests/infrastructure/platform/` — snapshot-shape tables over a fake OS seam (every field present/unknown × rendering); `tests/presentation/cli/` — output goldens (human and JSON), the unknown cases; a smoke assertion that the real probe returns totals > 0 on the build host.
- Consumes: 26a (shipped — the budget read this unifies with), 27e (shipped by then — the resident accounting and the `unknown` idiom), 28h (shipped with v0.1.5 — the one-JSON-document read shape).

**Reference.** In-house: the 26a machine budget (the one existing machine read), 27e's admission honesty (`unknown`, never guessed; stated arithmetic), M1's silent-on-pipes output discipline.

**Decisions made** (dated):
- 2026-10-04 — Asked for by the user, opening **v0.1.6** (their placement call) as its first item; the release's track is **32** by the ever-assigned rule — 29, 30 and 31 were assigned on 2026-10-03 and vacated the same day by the merges and migration, so they stay spent, living in the index's history notes (the M5/M6 precedent at track scale).
- 2026-10-04 — One command, not a split: the probe, the surface and the JSON read are one focused session; depth (per-process breakdowns, vendor GPU tooling, watch modes) is explicitly later.
- 2026-10-04 — Composition over a second estimator (27e's rule, consumed): the probe is the 26a read's home, extended — not a sibling.
- 2026-10-04 — CPU utilization is sampled over a short named window with POSIX load averages beside it *(recorded as the default, vetoable)*: load needs no sampling and is free; a two-sample utilization (~500 ms, the window printed) answers "how busy right now" without a monitor loop.
- 2026-10-04 — GPU first cut is what the OS answers cheaply *(the default, vetoable)*: Apple silicon's device name and unified memory honestly presented; other platforms print `gpu: unknown (not read on this platform yet)` — spawning vendor tools is a later deliberate item, not a rider.

**Open calls:**
- [default: the command is `apogee system` — a noun command beside `models` and `check`, growing sub-reads later if ever needed; `apogee resources` is the veto spelling]

**Guardrail(s).**
- The fake-OS tables: every field × known/unknown renders correctly in both formats; an all-unknown platform still exits 0 with every row present.
- The no-second-estimator pin: the 26a window math and `apogee system` read memory through the same function (asserted by construction — one symbol, call-counted in a seam test).
- Output discipline: piped output is byte-stable and plain (M1's rule); the JSON document validates as one object with the unknown fields explicit, not absent.
- The smoke row: on a real host, totals are positive, used ≤ total, and the resident-models section matches `models status`'s accounting on the same state.

**Acceptance criteria:**
- [ ] `apogee system` on this machine prints CPU (model, cores, load, sampled utilization with its window), memory (total/used/free, Apogee's footprint, resident models matching `models status`), GPU (the Apple-silicon story here; `unknown` stated elsewhere), and the store's disk footprint with volume free space.
- [ ] `apogee system --output-format json` emits one document with every field present and unknown-ness explicit; the human table says `unknown` where the platform cannot answer, and the command succeeds anyway.
- [ ] The context-window sizing and the snapshot agree on total memory by construction (one read), demonstrated in the seam test.
- [ ] The command and its flags tab-complete; piped output is plain and byte-stable.

**Scope note.** Item **32a**, the first of **v0.1.6** (table and directory created with it); gated on nothing pending. Out of scope: watch/monitor modes and any periodic sampling loop; per-process system-wide breakdowns; vendor GPU tooling (`nvidia-smi` and kin — a later deliberate item); admission or placement *decisions* from these numbers (27e owns suite admission; future consumers read the same probe); exposing the snapshot as a model tool (a later call, with its own prompt-injection thinking).
