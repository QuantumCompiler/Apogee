# The darwin pipeline: built, tested and packaged beside the CLI

**What / why.** The repo's law for a sibling application is recorded but unexercised: each app joins `cicd.sh`'s `APPS`, gets its own `changed.sh` entry, **and a pipeline of its own beside the CLI's** (the CLI docs' map; the GUI spike measured the gap — `APPS=(cli)` at `cicd.sh:40`, `changed.sh` knowing only `cli`). 1a wires the local halves; this item is the CI half: a **darwin job** on the macOS runner (the runner already exists — the CLI's `macos-arm64` jobs prove it) building and testing `lib/src/darwin/` through `cicd.sh` — the one build entry point local runs and CI share — gated by `changed.sh gui-darwin` the way the CLI's jobs gate on `changed.sh cli` (a docs-only merge skips the build under the required name, the CLI pipeline's own pattern); and a **package step**: `Apogee.app` assembled by 1a's bundling step, **ad-hoc signed** (the user's call, 2026-10-20 — no Developer ID, no notarization now), `codesign --verify`'d, zipped as `apogee-gui-darwin.zip` and kept as a run artifact. The artifact stays **a per-run CI artifact until the session window (1c) ships** (the user's call, 2026-10-20, at grooming): the pipeline proves every PR builds a signed app, and release attachment — the repo's merge-publishes law extended to the GUI — is its own later wiring, landed with or after 1c.

**Core constraint(s).** **One build path**: CI calls `cicd.sh`, never its own build steps — local and CI builds stay one path (the repo's standing rule). **The pipeline is the GUI's own**, beside the CLI's, per the recorded law: the CLI pipeline's jobs, names and gating are untouched (its "what changed" discipline is the template, not the host). **Required checks follow the repo's process**: new job names mean `required-checks.py` is re-derived and `--apply` asked of the user (the CLI docs' pipeline-change checklist, binding here too). **Ad-hoc only**: no signing secrets exist in CI; `codesign --sign -`; the spctl/Gatekeeper reality (right-click-to-open on other machines) is documented honestly in the artifact's README line, not worked around. Windows/Linux runners never build darwin — `cicd.sh`'s defer machinery, proven by 1a locally, holds in CI.

**Seam + files.**
- `.github/workflows/` — the darwin pipeline (its own workflow file beside `ci.yml`, per the one-pipeline-per-deliverable decision recorded 2026-09-25): the gate job over `changed.sh gui-darwin`, the build+test job on the macOS runner through `cicd.sh`, the package job (bundle, ad-hoc sign, verify, zip, upload).
- `lib/scripts/cicd.sh` / `lib/scripts/changed.sh` — grown by 1a; this item consumes them and adds only what CI needs (nothing, if 1a did its job — a gap found here goes back as 1a's fix, not a second declaration).
- `lib/scripts/required-checks.py` flow — run after the first green PR, `--apply` with the user's go-ahead.

**Reference.** The CLI's `ci.yml` — the what-changed gating, the required-names-while-skipped pattern, the package-action discipline (an artifact proven before upload); `release-from-pr.sh` and the merge-is-the-release law — the wiring the GUI artifact joins when shipping is decided; the GUI spike (2026-10-20) — the seam measurements.

**Decisions made** (dated):
- 2026-10-20 — **Ad-hoc signing in CI** (the user's call): no Developer ID, no notarization, no signing secrets; the Gatekeeper consequence said plainly beside the artifact.
- 2026-10-20 — The GUI gets its own workflow beside `ci.yml` (the recorded 2026-09-25 decision: the CLI pipeline is the CLI's; a GUI application gets a pipeline of its own).
- 2026-10-20 — CI builds through `cicd.sh` only — the one-path rule carried over.

- 2026-10-20 — **A CI artifact until 1c ships** (the user's call, at grooming): no release attachment in this item; the release scripts stay untouched, and the wiring lands with or after the session window.
- 2026-10-20 — **Job and artifact names at build** in the CLI pipeline's naming style; the first green PR's `required-checks.py --apply` asked of the user per the standing rule (the default, confirmed at grooming).

**Open calls:** none — groomed 2026-10-20.

**Guardrail(s).** The pipeline rehearsed before merge the repo's way (`make pr-ci` covers the CLI; the darwin job's rehearsal is its own `cicd.sh` invocation on the dev host — the same command CI runs); the gate proven both ways (a darwin-only diff builds, a docs-only diff skips under the required name); `codesign --verify` in the package job; the artifact downloaded and launched once on the dev host (the user's manual check, recorded); no CLI pipeline job, name or gate changed.

**Acceptance criteria:**
- [ ] A PR touching `lib/src/darwin/` runs the darwin job: built and tested on the macOS runner through `cicd.sh`, packaged as an ad-hoc-signed, verified `Apogee.app` zip artifact.
- [ ] A PR not touching the GUI's inputs skips the build under the required job name; the CLI pipeline is byte-for-byte untouched.
- [ ] Required checks updated through `required-checks.py` with the user's approval; the release question above settled and wired accordingly.

**Scope note.** Earmarked for v0.2.0; gated on [`darwin-scaffold.md`](darwin-scaffold.md) (1a). Out of scope: release attachment and any release-script change (the user's 2026-10-20 call — wiring lands with or after 1c), notarization and Developer ID (recorded later), linux/windows pipelines, auto-update machinery, any CLI pipeline change.
