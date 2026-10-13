# Shipped regimes: training suites in the box

**What / why.** The training track's "suite" shape already exists and nothing ships in it — measured 2026-10-11 (the training-suites spike, sandboxed install): `train regime run [name]` takes "a regime under `training.regimes` **(or a spec file path)**" with `--teacher`, `--student`, `--kit`/`--all-kits`, a judge and the mock trainer — but `train regime` has **exactly one verb, `run`** (W2: no list, no show), no seeded regime spec exists (W1: `training.regimes` is the user's config, spec files are the user's files), and the four bundled kits (`instruction-following`, `reasoning`, `structured-output`, `summarization`, seeded by `check --fix`) each carry **8 eval items** — too thin for the pipeline's cumulative gate to mean anything (W4). This item ships the mechanism's missing half: **regime specs on the source ladder** (a user's entry or file over the seeded file over the compiled-in copy), **discovery** (`train regime list|show`, human tables and JSON documents), the first shipped suite — **`foundation`**, today's four kits in a tuned order (instruction-following → structured-output → summarization → reasoning) with per-kit counts and iterations — and the four bundled kits' **eval sets deepened to the floor** so the gate the suite rides is real. A shipped regime is **install-agnostic by construction**: kits and tuning defaults only; the teacher and student are always the user's, named at run time or in their config, never in a spec Apogee ships.

**Core constraint(s).** No backend name ever appears in a shipped spec — the existing refusal ladder (no teacher, no student, no venv — each naming its fix) is the honesty surface and is not duplicated. The source ladder is the symphonies' (27q), seeded skip-if-present through the one seeding path — a new seeded location means **the layout row lands in the same commit** (the one-layout-declaration invariant) unless the specs live under the existing `training/` rows, which is the preferred shape. Reads have twins ([ADR 0002](../../adrs/cli/mode-parity.md)): `train regime list|show --output-format json` ship with the verbs, documented in machine-mode.md (the 28h idiom); the TUI's Train view and the exec line get them for free (ADR 0010's floor). The asset pipeline is the law for the spec and the deepened eval sets: compiled-in copies via the generators, byte-identity-tested. Spend rules unchanged: nothing trains or synthesises on Apogee's initiative — a regime runs when the user runs it.

**Seam + files.**
- `lib/src/cli/source/business/training/` — the regime spec's source ladder (the loader already parses a spec; it gains the seeded/compiled-in rungs and the name walk), the list/show views.
- `lib/src/cli/source/presentation/cli/train.cpp` — `train regime list` and `train regime show <name>` (+ `--output-format json`), beside `run`.
- `lib/src/cli/assets/training/regimes/foundation.yaml` — the first shipped suite; `assets/training/kits/*.yaml` — the four kits' eval sets deepened to the floor; the generators regenerate the compiled-in copies.
- `lib/documentation/reference/machine-mode.md` — the new reads' rows.
- Tests: ladder precedence (user over seeded over compiled-in); `list`/`show` goldens and document conformance; a `foundation` run end-to-end on the **mock trainer** (the hermetic vehicle) gated on the deepened evals; byte-identity; the refusals unchanged.

**Reference.** Milestone Z — the regime, the gated pipeline, the judge, the mock trainer, the kit seeding this extends; 27q — the source ladder copied; 28h — reads as documents; the training-suites spike (2026-10-11) for every measurement above.

**Decisions made** (dated):
- 2026-10-11 — The set lands in v0.1.7 as track 42 (the user's placement call on the spike report; 42 by the ever-assigned rule).
- 2026-10-11 — Shipped specs never name backends: teacher and student stay the user's at run time (the spike's default, standing) — a "training suite" is a regime spec, and the docs defer to the regime vocabulary so the model-suite concept stays unambiguous.
- 2026-10-11 — The eval floor is **24 items per kit**, judge-scored where no `expected` fits (the spike's default, standing); the gate's meaning is this item's deliverable as much as the suite is.
- 2026-10-13 — `foundation`'s per-kit counts and iterations are the build's tuning, recorded in the spec's comments with why; the kit order as specced (the default, confirmed 2026-10-13).
- 2026-10-13 — `train regime list` shows name, kits, source (user/seeded/shipped) and description — the `datasets kits` table's shape (the default, confirmed 2026-10-13).

**Guardrail(s).** Ladder precedence exhaustively; the mock-trainer `foundation` run green and gated (a deliberately failing eval set fails the stage — the planted case); document conformance for both reads; byte-identity of spec and kit assets; `cli.install_parity` green (seeding unchanged or its row updated in the same commit); every existing training suite of tests untouched and green.

**Acceptance criteria:**
- [ ] `train regime list` shows `foundation` on a fresh seeded install; `show` prints it whole; both have documented JSON twins.
- [ ] `train regime run foundation --teacher <b> --student <s>` runs the four kits as one gated pipeline on the mock trainer in tests.
- [ ] Every bundled kit carries ≥24 eval items; the gate fails a planted bad stage.
- [ ] No shipped file names a backend; the refusal ladder is byte-stable.

**Scope note.** Earmarked for v0.1.7; first of track 42, ungated. Out of scope: new kits ([`role-chore-kits.md`](role-chore-kits.md)), any trainer/pipeline change, any auto-run.
