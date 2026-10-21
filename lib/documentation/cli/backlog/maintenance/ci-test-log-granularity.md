# CI's test logs gain the terminal's granularity: one line per test, with its time

**What / why.** Running `make test` locally, every case prints its own line — `3586/3593 Test #3586: cli.tui_shell … Passed 11.57 sec` — and the user wants CI's logs to read the same way (their ask, 2026-10-20, the wanted shape quoted from their run). CI cannot show it today by construction: the unit-test jobs run `cicd.sh --unit-tests`, which builds the `apogee_tests` target and **runs the Catch2 binary directly** (`lib/scripts/cicd.sh:335-350`; the recorded 2026-09-22 decision — no `apogee` executable is produced and ctest never runs there), and Catch2's default output is near-silent on success: a green job's log shows totals and nothing per test. This item gives the one shared invocation per-case lines: the `--unit-tests` run gains Catch2's own per-case reporting (each test case named with its result and duration as it completes — Catch2 v3 carries the machinery; the exact reporter/flags are the build's pick), declared **once in `cicd.sh`** so a local `--unit-tests` run and every CI runner print identically — the one-build-path rule, not a CI-only flag in the workflow.

**Core constraint(s).** **One declaration**: the flags live in `cicd.sh`'s unit-test invocation, nowhere in `ci.yml` — local and CI stay one path. **`ci-annotate.sh` keeps working**: it parses Catch2's totals and every failed case's name from the log (its recorded contract since 2026-09-25); the new output keeps or extends what it reads, verified against a planted failing test on the new shape. **Volume is accepted**: ~3,550 lines per green job is the ask, not a problem to optimize away; a line carries the case name, result and duration. **Nothing else moves**: which tests run, the gating tier, `make test`'s own ctest output (already granular) — all unchanged.

**Seam + files.** `lib/scripts/cicd.sh` (the `apogee_tests` invocation's reporter flags, with the one comment saying why); `lib/scripts/ci-annotate.sh` (grown only if the totals/failed-name parsing needs the new shape taught); nothing in `.github/workflows/` (no job, name or gate changes — the pipeline checklist untouched by construction).

**Reference.** The user's run log (2026-10-20) — the wanted line shape; the 2026-09-22 unit-tests-as-the-binary decision — the construction this works within; `ci-annotate.sh`'s header — the parsing contract preserved.

**Decisions made** (dated):
- 2026-10-20 — The user's call: CI logs show the per-test granularity the terminal gets. Achieved inside the existing tier decision (the Catch2 binary's own reporting), never by switching CI to ctest.

**Open calls:**
- [default: the exact Catch2 reporter and flags (per-case result lines with durations) picked at build against Catch2 v3's options, the same on every platform's runner; MSYS2's shells included in the check.]

**Guardrail(s).** A local `cicd.sh --unit-tests` run shows one line per case with result and duration; a planted failing case still produces the GitHub annotation with its name and block (`ci-annotate.sh` proven on the new shape); the invocation diff is confined to `cicd.sh`.

**Acceptance criteria:**
- [ ] A CI unit-test job's log lists every test case as it completes — name, result, duration — on all five platforms' runners.
- [ ] A failing case still annotates by name with its output; green runs' totals still parse.
- [ ] `ci.yml` is byte-identical; local `--unit-tests` prints the same lines; the full suite is green.

**Scope note.** Maintenance item — claimable at any time by name, ungated. Out of scope: changing which tests CI runs or the gating tier (the standing decision), `make test`'s ctest output (already the wanted shape), log retention or artifact changes.
