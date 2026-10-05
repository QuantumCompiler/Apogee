# One CI pipeline: the manual release workflow retired

**What / why.** The user's ask (2026-10-04): one pipeline instead of two, the tag-and-release happening **only when the branch is merged**, and the now-unneeded extra pipeline's Actions title dropped. Today the Actions sidebar shows two workflows: **CI** (`.github/workflows/ci.yml` — the everyday path: pull-request gates, five builds, and `tag and release` after the merge) and **Release** (`.github/workflows/release.yml` — the manual escape hatch kept since 2026-09-25: a hand-pushed `v*` tag rebuilds every target from the tag and publishes; plus a dry-run dispatch). The merge path already publishes only on merge — what this item changes is making it **the only automatic publish path**: `release.yml` is deleted (its "Release" title disappears from Actions — the user's explicit ask), the `v*` tag-push trigger with it, and the one capability only it had — the **re-cut** (rebuild all five targets from source and publish, for when a merged pull request's archives cannot be used: the tree check refused them, or the artifacts expired) — moves into CI as a dispatch input, so the escape hatch survives inside the one workflow. A plain dispatch of CI already builds all five platforms and keeps the archives as artifacts ("on request: everything runs"), which is `release.yml`'s dry run in all but name; the delta is one opt-in publish leg on that run.

**Core constraint(s).**
- **The merge path is untouched, byte for byte:** pull-request jobs, their names, the `changes`/copy-the-release discipline, `tag and release` on `closed`+merged via `release-from-pr.sh`, and the rehearsal dispatch all stay exactly as they are. **No required check changes** — `release.yml`'s jobs were never required, and no `ci.yml` job is renamed — so the "Stable: CI must pass" ruleset stands without a `required-checks.py --apply` (verified anyway, see guardrails).
- **Publish-from-dispatch is explicit and idempotent:** the new input (`publish`, default false) does nothing on an ordinary dispatch; with it true, the publish leg runs only after **all five** builds pass and refuses a version already released (the `release-from-pr.sh` idempotency, carried). One deliberate tightening, named honestly: `release.yml` let Windows fail without blocking a re-cut (`continue-on-error`); the one pipeline has one gating policy — all five or nothing — because a release with missing archives is the state the merge path already refuses.
- **The tag stays downstream of the release:** the tag and the release are created together at publish time, nothing earlier pushes a tag (the strand-nothing rule both workflows already share); with the `v*` trigger gone, a hand-pushed tag runs **nothing** — said in DEVELOPER.md, not left to be discovered.
- **`make release` must not become a silent no-op:** the target currently tags and pushes to start `release.yml`; it is rewired to dispatch the CI publish run (`gh workflow run CI`), keeping `make release-check`'s local preflight intact — a retired trigger with a live Makefile target pointing at it is exactly the silent-failure class this repo refuses.
- **One matrix again, by deletion:** the duplicated five-row matrix (`ci.yml` ↔ `release.yml`, "the same list on purpose" because GitHub cannot share one) collapses to a single copy — the drift risk retires with the file.
- **The reference sweep ships in the same change:** `ci.yml`'s header comments naming `release.yml`, DEVELOPER.md's tree row, "Changing the pipeline" and "Cutting a release" sections, and the Makefile's comments all describe the one-workflow world — a doc that still teaches the two-pipeline shape is drift wearing a filename.

**Seam + files.**
- `.github/workflows/ci.yml`: the `publish` dispatch input; the publish leg on the existing `tag and release` job (dispatch-with-publish joins its `if:`, publishing **this run's** fresh archives — the from-this-run path beside `release-from-pr.sh`'s from-that-PR path); header comments swept.
- `.github/workflows/release.yml`: **deleted.** The package action (`.github/actions/package`) stays — `ci.yml` is its remaining caller.
- `lib/src/cli/Makefile`: `release` rewired to the dispatch (preflight → `gh workflow run CI -f publish=true --ref <release branch>`), `release-check` unchanged in spirit; the header comments that narrate the tag path rewritten.
- `lib/scripts/release-from-pr.sh` (or a sibling entry point): the publish-from-this-run leg, sharing the version/idempotency checks rather than growing a second copy.
- [DEVELOPER.md](../../assistant/DEVELOPER.md): the workflows tree row, **Changing the pipeline** (the matrix-coupling bullet retires), **Cutting a release** (the manual path re-described as the dispatch; the re-run-failed Windows caveat retires with `continue-on-error`).
- Consumes: the pipeline decisions recorded in `ci.yml`'s header (2026-09-19…2026-10-04) — this item extends 2026-09-25's "the everyday release is not release.yml" to its conclusion.

**Reference (Ommi).** No analog — Ommi has no CI workflows at all (`~/Data/Development/Projects/Ommi` carries no `.github/workflows/`); Apogee's pipeline is house-grown, and its own recorded decisions (the workflow headers, DEVELOPER.md → Changing the pipeline) are the precedent this item builds on.

**Decisions made** (dated):
- 2026-10-04 — Asked for by the user: one pipeline, publish only on merge, the extra Actions title dropped. Placed in **Maintenance** (their call), M8 by the ever-assigned rule (M1–M7 spent).
- 2026-10-04 — The merge path is already publish-on-merge-only; the item's substance is retiring the second workflow and the tag-push trigger, so "only when the branch is merged" becomes true by construction rather than by convention.
- 2026-10-04 — The re-cut capability is kept *(recorded as the default, vetoable — the alternative is dropping re-cuts entirely)*: as a CI dispatch input, not a second workflow — the user's one-pipeline ask is about the workflow count and the Actions sidebar, and the escape hatch was created by a real need (archives expire; the tree check can refuse).
- 2026-10-04 — The Windows-non-blocking asymmetry retires with `release.yml`: a re-cut now requires all five builds green, same as a merge. One gating policy, the stricter one.

**Open calls:**
- [default: the `publish` input's guard is the dispatch actor having push rights plus the existing already-released refusal — no extra confirmation input; the Actions UI's run-workflow prompt is the confirmation]

**Guardrail(s).**
- The rehearsal dispatch (`release_rehearsal_pr`) and `lib/scripts/pr-ci.sh` run unchanged — the pull-request jobs are untouched by construction, and the rehearsal proves the merge path still finds and checks archives.
- `required-checks.py --pr <N>` on the change's own pull request reports **zero** ruleset drift (no job renamed, none added to the required set).
- A dry dispatch (publish absent) lands five `apogee-<target>` artifacts and publishes nothing; a `publish=true` dispatch against an already-released version refuses with the version named.
- The grep sweep: zero references to `release.yml`, the `v*` trigger, or the "Release" workflow title anywhere outside dated history (MILESTONES, release notes, this document).

**Acceptance criteria:**
- [ ] The Actions sidebar lists one pipeline; `.github/workflows/` holds `ci.yml` alone, and a hand-pushed `v*` tag starts no workflow (documented in DEVELOPER.md).
- [ ] A merged pull request publishes exactly as before: the tag `v<VERSION>` at the merge commit with the pull request's own (or copied) archives — verified on the first merge after the change.
- [ ] `gh workflow run CI -f publish=true --ref <branch>` (and `make -C lib/src/cli release`, rewired) builds all five targets from that ref and publishes the release `lib/release/VERSION` names; the same dispatch against a released version publishes nothing and says why.
- [ ] DEVELOPER.md's pipeline sections describe only the one workflow, and `required-checks.py` confirms the ruleset needed no change.

**Scope note.** Maintenance item **M8**; gated on nothing. Out of scope: any change to the pull-request gates, the `changes`/copy discipline, the version rules, or the packaging action; new pipelines for other deliverables (the GUI's day will bring its own); release-notes automation.
