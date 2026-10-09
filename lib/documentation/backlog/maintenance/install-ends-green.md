# Installs end green: no warnings left for the user

**What / why.** The user's ask (2026-10-08, from a real fresh-install `check` transcript): installing the CLI should not leave warnings for the user to resolve by hand — the install process should resolve them. The transcript shows exactly two on a fresh install (`no failures, 2 warning(s)`), and they need different fixes because one is cheap and offline while the other is a network act the SPEC forbids doing unasked:

1. **`warn config.json — not found … run: apogee config init`.** Cheap, offline, deterministic — and every install path already ends with `apogee check --fix` (the one-seeding-path rule). The fix: **`--fix` seeds the config template where no config file exists at all** — the JSONC template, byte-identical to `config init`'s output, created skip-if-present and *never* touching an existing file in any way. One change greens all three install paths (curl, PowerShell, `make install`) at their shared final step, with no installer growing a second call site.
2. **`warn python env — not created … run: apogee train setup`.** Creating the venv runs pip over the network — and the SPEC's non-goal is explicit: *installers download nothing unasked*. So this row cannot be auto-resolved at install; instead it is **reclassified from `warn` to `skip`**, in exactly the idiom its MLX siblings already use (`skip runtime — not set up — only an mlx backend needs it; run: …`): the venv is an opt-in feature not yet set up, not a defect — `skip python env — not created — only 'datasets prepare' and the trainers need it (never the system Python); run: apogee train setup`. The remediation line stays; the warning count stops charging the user for a feature they haven't asked for.

The outcome: a fresh install on every path ends `apogee check` with **`no failures, 0 warning(s)`** — skips present and honest, nothing demanding resolution.

**Core constraint(s).**
- **The one-seeding-path rule carries:** the config seeding lives in the binary's `--fix` repair set, reached by all three installers through the call they already make — no installer gains its own `config init` line (a second call site is the drift the layout invariant exists to prevent).
- **The check-never-edits-config invariant is refined, not broken:** seeding a template where *no config exists* decides nothing about the user's configuration — there is nothing to decide; an absent file has no meaning to preserve. `--fix` still may not touch, migrate, or reinterpret any existing config file (YAML or JSONC), and `check` without `--fix` still only reports. The ⚠ section's wording in [CLAUDE.md](../../assistant/CLAUDE.md) gains this clause in the same change — the invariant's letter must match what the code does.
- **Installers download nothing unasked** (the SPEC non-goal, the reason for the split): no pip, no venv, no network at install beyond fetching the archive itself. The venv row's reclassification is the honest resolution — the MLX rows set the precedent that "opt-in feature not set up" is a `skip`, not a `warn`.
- **`--fix` says what it did:** a seeded config is printed (`created config.json from the template`), exactly as directory repairs are — a user who deliberately deleted their config and runs `--fix` learns what happened rather than discovering a fresh template later.
- **Warn stays meaningful:** after this item, a fresh install has zero warnings by construction, so any `warn` a user ever sees signals a real defect — that sharpening is the point, and the install-parity test enforces it so it cannot regress.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; tests in the layer of what they test ([ADR](../../adrs/cli/tests-mirror-architecture.md)).

**Seam + files.**
- `presentation/cli/check.cpp`: the Config section's `--fix` path — seed-if-absent through the template (`contracts/`' compiled-in copy, the same bytes `config init` writes); the Training section's venv row reclassified `warn` → `skip` with the MLX rows' phrasing.
- `contracts/` (the config engine): nothing new — `--fix` calls the existing template-write used by `config init` (one implementation; a second template writer would be the config twin of the two-seeders bug the layout invariant records).
- [CLAUDE.md](../../assistant/CLAUDE.md) → the ⚠ layout/one-seeding-path section: the seed-where-absent clause added to `--fix`'s stated powers, dated.
- `tests/presentation/cli/check_test.cpp`: the seed-if-absent cases (absent → created + said; present YAML → untouched byte-for-byte; present JSONC → untouched; `check` without `--fix` → still warns and names `config init`); the venv row's skip wording golden.
- `tests/scripts/sh/install_parity.sh` (`cli.install_parity`): tightened — a freshly seeded install must pass `apogee check` with zero failures **and zero warnings** (today it requires zero failures only), on the release and dev channels both.
- Consumes: the one-seeding-path machinery (shipped), `config init`'s JSONC template (shipped, 28i), the MLX skip-row idiom (shipped, 27a), M10's channels (shipped — the parity test's dev-channel leg).

**Reference.** In-house throughout: the MLX `skip` rows (the exact idiom the venv row adopts), the one-seeding-path invariant and its recorded two-seeders lesson (why the fix lives in `--fix`, not in three installers), and 28i's `config upgrade`/`check` note (the config row's behavior *after* a config exists — untouched by this item).

**Decisions made** (dated):
- 2026-10-08 — Asked for by the user with the fresh-install transcript as evidence; placed in **Maintenance** (their call), M11 by the ever-assigned rule (M1–M10 spent).
- 2026-10-08 — The two warnings get different fixes because the SPEC decides it: config seeding is offline and deterministic → automated at the shared `--fix` step; the venv needs network → never run unasked, reclassified to the skip its MLX siblings already are. An install that silently ran pip would trade a warning for a violated non-goal.
- 2026-10-08 — Seed-where-absent lives in `--fix`, not in the installers: three call sites drift, one does not (the two-seeders lesson, applied to config).
- 2026-10-08 — An existing config file of either format is never touched by `--fix`, under any flag, full stop — the refinement narrows the invariant's letter without moving its line.

**Guardrail(s).**
- The seed-if-absent table: absent → created, said, byte-identical to `config init`'s output; present (either format, including a deliberately broken one) → untouched byte-for-byte and the existing behavior (notice, migrate pointer, or parse error) unchanged; plain `check` → warns exactly as today.
- The tightened parity gate: `cli.install_parity`'s fresh install asserts `0 warning(s)` on both channels — the regression-proofing that keeps future features from quietly re-warning on fresh installs.
- The skip goldens: the venv row renders in the MLX idiom with its remediation; `train setup` afterwards flips it to ok (the existing behavior, re-asserted).
- The invariant text check: `harness.layer_context`-style — the CLAUDE.md clause lands in the same change (reviewed, not scripted).

**Acceptance criteria:**
- [ ] A fresh install on each path (`install.sh`, `install.ps1` where runnable, `make install`) ends with `apogee check` reporting **no failures, 0 warning(s)** — config green with the seeded template, the venv row a skip naming `train setup`.
- [ ] `apogee check --fix` on an install whose config exists (either format) leaves the file byte-identical; with no config, it creates `config.json` equal to `config init`'s output and says so.
- [ ] `apogee train setup` still flips the venv skip to ok; nothing about training's behavior changes.
- [ ] `cli.install_parity` enforces the zero-warning fresh install on the release and dev channels.

**Scope note.** Maintenance item **M11**; gated on nothing. Out of scope: an install-time opt-in for the training venv (`--with-training` or kin — a later item if ever asked for; the non-goal stands); touching any existing config under `--fix` (never); the MLX skips (already correct); changing what `warn` means anywhere else.
