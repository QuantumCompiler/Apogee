# The merge gates hold: the required-checks ruleset in place and verified, the version-bump check counting the macOS app, a failed release lookup never read as not released

**What / why.** A merge into `stable` is the release ([CLAUDE.md → Versioning](../../assistant/CLAUDE.md#versioning)), so the checks in front of that merge are the only thing between a pull request and a release that publishes nothing, or the wrong thing. Three of them do not hold today. Nothing requires the pipeline to be green, or the branch to be up to date, before a merge. The `version bump` check does not know the macOS app exists. And a failed release lookup counts as "not released". Each lets a merge go through that `release-from-pr.sh` then declines or refuses, silently or after the fact, so the change ships in no release or the release has to be re-cut by hand. Found by the second bug sweep, 2026-10-31, and re-verified at `b26550bb` the same day:

1. **No required checks are enforced on `stable`: the "Stable: CI must pass" ruleset does not exist.** `ci.yml:43-46`, `required-checks.py:7-10` (and `RULESET_NAME`, `:56`), CLI [DEVELOPER.md → Required checks](../../assistant/DEVELOPER.md#required-checks) (`:1297`) and GUI `DEVELOPER.md:114` all describe it as in force: no bypass list, every pull-request job required, the branch up to date with `stable`. Read-only probes, 2026-10-31:
   - `gh api repos/QuantumCompiler/Apogee/rulesets` lists one ruleset, `23855785 Stable` (created 2026-09-22 19:58, last updated 20:17 that day). `includes_parents=true` adds none.
   - `gh api .../rulesets/23855785` carries `{"strict_required_status_checks_policy":false,"do_not_enforce_on_create":false,"required_status_checks":[]}`, and `bypass_actors` RepositoryRole 2 and 5, `bypass_mode: always`.
   - `gh api .../rules/branches/stable` lists rules from 23855785 only. `branches/stable/protection` returns 404 "Branch not protected".
   - `lib/scripts/required-checks.py` (the dry run, which reads and changes nothing) picks #11's runs (ci.yml 41226403061: 13 jobs passed; darwin.yml 41226403105: 2) and prints 15 checks, every one marked `+`, then `no 'Stable: CI must pass' ruleset yet: --apply creates it, with no bypass list`.

   **Why it is missing: it was never created.** The sweep recorded this as a regression of CLI `MILESTONES.md:2191` ("[x] The "Stable: CI must pass" ruleset, created or updated by `--apply`") and GUI `MILESTONES.md:150` (1d: the Darwin checks joining it). The record says otherwise. The `[x]` is for the code path. The same 2026-10-03 entry ends (`:2214`) "**Not done.** The ruleset has not been created. Running `--apply` changes the repository's settings, and that is the user's to run", and 1d's entry lists the required checks under "Not run, so the user's" (GUI `MILESTONES.md:148`). GitHub's own record agrees: `rulesets/rule-suites?time_period=month` holds eight pushes to `stable`, from 2026-10-04 (`fff2cfeb`) to 2026-10-31 (`b54b25e9`, #11). Every one has `result: bypass`, and in every one the only ruleset evaluated is "Stable": `update` fail ("Cannot update this protected ref."), `pull_request` fail ("At least 1 approving review is required…"), `required_status_checks` pass (it requires nothing). The step was never noticed because nothing made it noticed. The skills run the dry run only when a job's name changes (`apogee-maintenance-update-documents` step 9; `apogee-maintenance-summarize-pull-request:62`), and they word it as *re*-applying. #11's description said "Until it runs, the new jobs aren't required to pass", as if the rest were. The dry run's "no ruleset yet" is a printed line, not a failure.

   **Failure scenario.** A release pull request is merged while `build darwin` is still running on the `xcode-27` image, or after a red run, and nothing blocks either. The merge's `tag and release` runs `release-from-pr.sh` straight away. Step 4d finds no successful `darwin.yml` run holding `apogee-gui-darwin` and dies (`release-from-pr.sh:460-461`), or step 4a finds no green CI run (`:369-370`), so nothing is published. Separately, "up to date" is not required, so `stable` can move after the pull request's last run. Then the tree checks (`:387-388` for the CLI, `:471-472` for the app) refuse the archives, and again the merge publishes nothing and needs a manual re-cut. The ruleset was meant to make both impossible.

2. **The `version bump` check ignores the macOS app: an app-only change with no `VERSION` bump passes green, and the merge ships nothing.** `version-check.sh` takes only `--cli` (`:45-53`). Its rules are keyed on the CLI alone (`:100-110`): with the CLI unchanged and `v<VERSION>` released, it says "merging publishes nothing" and exits 0. The `changes` job never computes `gui-darwin`. Its outputs are `cli` and `latest` only (`ci.yml:294-296`), and `version bump` passes only `--cli` and `--latest` (`ci.yml:358`; `pr-ci.sh:272` the same). `darwin.yml` asks `changed.sh gui-darwin` (`:71-96`) for its own build, but no check holds the release to it. Release-from-pr's step 2 (`release-from-pr.sh:257-260`) then exits 0 on the released tag before step 4d ever looks at the app. Regression of GUI M17 (`MILESTONES.md:2737`, the heading; `:2739`: "every release carries it from here"): the check that guards against a forgotten bump was never extended to the second deliverable.
   - Sweep probe (VERSION 0.2.0, v0.2.0 released): `lib/scripts/version-check.sh --cli false --latest v0.2.0` printed `v0.2.0 is already released and the CLI is unchanged since v0.2.0: merging publishes nothing`, exit=0. Meanwhile `changed.sh gui-darwin b54b25e9 HEAD` printed `14 of its files changed … gui-darwin=true`.
   - Re-run at `b26550bb` (VERSION still 0.2.0): the same line and `exit=0`, while `changed.sh gui-darwin b54b25e9 HEAD` says `24 of its files changed since b54b25e9: the gui-darwin pipeline runs` / `gui-darwin=true`.

   **Failure scenario.** A pull request touches only `lib/src/darwin` (a GUI fix) and leaves `lib/release/VERSION` at a released 0.2.0. `version bump` passes, the pull request merges, and the fixed `Apogee.app` is never published. The app has no version of its own to hold: `build.sh` stamps `MARKETING_VERSION` from `lib/release/VERSION` (`lib/src/darwin/scripts/build.sh:115-137`), so the one rule missing is "changed, so `VERSION` must be unreleased".

3. **A failed `gh release view` reads as "not released", so a network or auth failure passes the CLI-changed rule.** `version-check.sh:85` sends `gh release view "$tag"` stderr to `/dev/null` and takes any non-zero exit as `released=false`, whether that is a 404, a 5xx, a rate limit or a bad token. The `ls-remote` fallback beside it already tells the cases apart: 0 is released, 2 is not, anything else fails the check (`:92-96`). So does the self-computing path's latest-release lookup ("'no release' is an answer, 'could not ask' is not", `:67-69`), and that rule is skipped in CI, because CI passes `--cli`/`--latest` in.
   - Reproduced 2026-10-31 with a stand-in `gh` that prints `HTTP 502: Bad Gateway` and exits 1 (`GH_TOKEN=x`, VERSION 0.2.0 released, CLI version 0.2.0): `version-check.sh --cli true --latest v0.2.0` printed `merging publishes v0.2.0, with the CLI 0.2.0 this run builds (checked the release, through gh)`, `exit=0`. With the real `gh` the same call fails, correctly: `the CLI changed since v0.2.0, but v0.2.0 is already released -- bump lib/release/VERSION …`, `exit=1`.

   **Failure scenario.** The CLI changed, `VERSION` was not bumped, and `gh release view` fails transiently on the runner. The check passes with "merging publishes vX", the pull request merges, and `release-from-pr.sh` step 2 sees the existing tag and exits 0. The CLI change ships in no release, which is exactly what the check exists to stop. The same hole would open for the app once finding 2 is fixed.

What already works: `release-from-pr.sh` refuses rather than publishes in every one of these cases (no green run, tree mismatch, version mismatch), so nothing wrong is ever released. `required-checks.py` derives the right 15 checks from #11 by itself, and its `--apply` creates the ruleset as designed. The version check's CLI rules and its `ls-remote` path are right.

**Core constraint(s).**
- **The merge is the release** ([Versioning](../../assistant/CLAUDE.md#versioning); [Release and Install Infrastructure](../../assistant/CLAUDE.md#release-and-install-infrastructure)). What ships is the pull request's own build, byte for byte what CI tested, from the merge's exact tree. The gates exist so a merge can never be one `release-from-pr.sh` must decline.
- **The gating policy** (`ci.yml` header; user decisions 2026-09-19 and 2026-10-03). Every job that runs on a pull request is required. There is no `continue-on-error`. The "Stable: CI must pass" ruleset has no bypass list, so an admin may merge without an approval, never past a red or unfinished pipeline, and the branch must be up to date with `stable`.
- **`--apply` changes repository settings: an agent asks the user first** ([Implementing a Feature](../../assistant/CLAUDE.md#implementing-a-feature); [DEVELOPER.md → Changing the pipeline](../../assistant/DEVELOPER.md#changing-the-pipeline) step 1). Every other repository probe this item makes is a read-only `gh api` GET.
- **Changing the pipeline's list** ([DEVELOPER.md → Changing the pipeline](../../assistant/DEVELOPER.md#changing-the-pipeline)). `pr-ci.sh` mirrors any changed pull-request job. A required job never gets a job-level `if:`. No job is added or renamed here, so the required check names do not move.
- **Two versions, one job each** (user decision, 2026-09-25). `lib/release/VERSION` names the release. The CLI's `project(... VERSION)` is the CLI's own. The app carries no third version; its version is stamped from the release.
- **`ci.yml` is the CLI pipeline, `darwin.yml` the app's** (2026-09-25, 2026-10-20). `version bump` is the release's check and stays the one job in `ci.yml` that answers for both deliverables. `darwin.yml` gains no release check of its own.
- **Development runs on the dev channel.** No binary is run here. The probes are the repo scripts over stand-in `gh`s and read-only API calls, and nothing touches `~/.apogee`.

**Seam + files.**
- `lib/scripts/version-check.sh`:
  - `--app true|false` beside `--cli`; worked out itself (`changed.sh gui-darwin` against the same latest-release commit) when absent, as `--cli` is at `:66-74`.
  - The rule: app changed and `v<VERSION>` released fails, naming the app and the bump.
  - The messages name both deliverables: "the CLI and the macOS app are unchanged", "with the app this run's `darwin.yml` builds".
  - The release lookup tells not-found from could-not-ask, through `gh api repos/<repo>/releases/tags/<tag>` (200 released, 404 not, anything else fails with `gh`'s own message). The header comment (`:31-34`) is updated to say so.
- `.github/workflows/ci.yml`:
  - the `what changed` job also runs `changed.sh gui-darwin "$commit" HEAD` and outputs `gui-darwin`;
  - `version bump` passes `--app`;
  - the header's gating paragraph (`:37-53`) and the `version` job's comment name the app.
  - Job names are unchanged.
- `lib/scripts/pr-ci.sh` (`:272`): passes `--app` the way CI does.
- `lib/scripts/required-checks.py`:
  - `--verify`, read-only: exit 1 when the ruleset is missing, not `active`, has any bypass actor, does not require up-to-date, or requires a list other than the chosen pull request's passing checks; exit 0, printing the list, when it matches.
  - The dry run's "no ruleset yet" line also says that nothing is enforced until `--apply` runs.
- Tests, POSIX only under `if(UNIX)` like `cli.install_parity` (`lib/src/cli/tests/CMakeLists.txt:680`, its `add_test` at `:701`), each in its own `sandbox_dir`:
  - `lib/src/cli/tests/scripts/sh/version_check.sh` (`cli.version_check`): the real `version-check.sh` in a throwaway git repository, with a stand-in `gh` on `PATH`.
  - `lib/src/cli/tests/scripts/py/required_checks_check.py` (`cli.required_checks`): the real `required-checks.py` against a stand-in `gh` serving recorded ruleset and run documents.
- Skills: `.claude/skills/apogee-maintenance-summarize-pull-request/SKILL.md` (`:62`, "Before merging"), `apogee-maintenance-update-documents/SKILL.md` (step 9) and `apogee-backlog-execute-item/SKILL.md` (`:58`). Each runs `required-checks.py --verify` on every release pull request, not only when a job changed, and reports what it printed. A missing ruleset is a "needs the user" line, never "re-apply".
- Docs:
  - CLI `DEVELOPER.md` → Required checks: the date the ruleset was created and `--verify`. → Changing the pipeline: the `version bump` rule for the app.
  - GUI `DEVELOPER.md:114`: the app's row in the version check.
  - The CLI `CLAUDE.md` checklist line: `--verify` before every release merge.

**Reference.**
- CLI MILESTONES 2026-10-03, "Required checks, read from the pipeline": the script and the ruleset design this item completes. Its "Not done" is the step this item finishes.
- GUI 1d (`MILESTONES.md:128`, the Darwin checks to join it) and GUI M17 (`MILESTONES.md:2737`, the app on every release): the second deliverable the version check now counts.
- `release-from-pr.sh`'s `api` helper (`:153-162`: a 404 is "not found", anything else fails with `gh`'s message) and its tag lookup (`:236-247`): the precedent for the lookup fix, in the script the check guards.
- `version-check.sh:67-69` and `:92-96`: the same rule, already in this file.

**Decisions made** (dated):
- 2026-10-31: Specced from the second CLI/GUI bug sweep (the user's call: every finding becomes a v0.2.1 item in its queue, CLI track 57, GUI track 6).
- 2026-10-31: One item because the three are one question: what stands between a pull request and a merge that releases nothing. Two of the findings span both queues (the ruleset covers `darwin.yml`'s jobs; the version check covers the app). The machinery is the repo-wide pipeline the CLI docs own ([documentation router](../../../CLAUDE.md)), so the item sits in the CLI queue and the GUI queue has no sibling.
- 2026-10-31: The cause of the missing ruleset is settled: it was never created, not deleted. The evidence is the 2026-10-03 entry's "Not done", 1d's "Not run, so the user's", "Stable" untouched since 2026-09-22, and GitHub's rule-suite record of every October merge. The 2026-10-03 entry is history and is not rewritten. This item's own record says when the ruleset came into force.

**Open calls:**
- [user] **Approve creating the ruleset**: `lib/scripts/required-checks.py --pr 11 --apply`. It creates "Stable: CI must pass" from #11's passing runs, with the 15 checks the dry run lists (`what changed`, `version bump`, `clone llama.cpp`, five `unit tests <target>`, five `build <target>`, `what changed (darwin)`, `build darwin`), active, with no bypass list and the branch required to be up to date. This is a repository setting. It also gates the user's own merges: no admin bypass past a red or unfinished run, and a branch behind `stable` must be updated (and re-run) first. Recommendation: apply it now from #11, so v0.2.1's own pull request is the first merge it gates. This item renames no job, so its pull request needs no re-apply. If anything else in v0.2.1 changes a job, the rule in Changing the pipeline re-applies from that pull request.
- [default: `version-check.sh` takes `--app true|false`; CI's `what changed` computes `gui-darwin` from the commit it already resolved and `version bump` passes it; no new job, so no required-check name moves.]
- [default: the release lookup is `gh api repos/<repo>/releases/tags/<tag>`, with 404 meaning not released and any other failure failing the check with `gh`'s message (`release-from-pr.sh:153-162`'s rule); the `ls-remote` path stays as it is.]
- [default: `required-checks.py --verify` is the standing proof. The PR-description and docs-pass skills run it on every release pull request, and `make pr-ci`'s summary reports it. No CI job reads the ruleset: a check on a missing ruleset gates nothing, and the job would itself become a required check.]
- [default: the "Stable" ruleset is left alone, its empty `required_status_checks` rule included. It requires nothing, and its review rules and admin bypass are the solo-maintainer path DEVELOPER.md describes.]
- [default: the 2026-10-03 stand-in-`gh` cases (create, update keeping other settings, up to date, the bypass and enforcement warnings, a failed or running run refused, open PR preferred, two workflows combined, wrong base refused), which were never committed, are committed with `--verify`'s in `required_checks_check.py`.]
- (consumed decision) Every pull-request job required, no bypass, up to date: the user's, 2026-09-19 and 2026-10-03. This item enforces it and does not reopen it.

**Guardrail(s).**
- `cli.version_check`, over a throwaway repository with a stand-in `gh` (POSIX, hermetic):
  - app changed and `VERSION` released fails, naming the app (the sweep's probe inverted);
  - app changed and unreleased passes, saying it publishes with the app;
  - neither changed and released passes, saying it publishes nothing;
  - a `gh` exiting 1 with `HTTP 502` fails "could not ask whether v0.2.0 is released", for `--cli true` and `--cli false` alike, never "merging publishes";
  - a 404 reads as not released;
  - the `ls-remote` path keeps its 0/2/other rule;
  - the self-computing path (no `--cli`/`--app`) asks `changed.sh` for both.
- `cli.required_checks`, over recorded ruleset and run documents: `--verify` exits 1 on each of missing, `disabled`, a bypass actor, `strict` false, and a check list differing from the run's; it exits 0 on a match. The committed 2026-10-03 cases keep `--apply`'s behavior pinned.
- `pr-ci.sh` and `ci.yml` pass `version-check.sh` the same arguments: rehearsed by `make pr-ci`.

**Acceptance criteria:**
- [ ] `gh api repos/QuantumCompiler/Apogee/rulesets` lists "Stable: CI must pass": `enforcement: active`, `bypass_actors: []`, `strict_required_status_checks_policy: true`, the dry run's 15 checks. `rules/branches/stable` lists its `required_status_checks`.
- [ ] The v0.2.1 merge's rule suite evaluates "Stable: CI must pass" and passes it, never bypassing it.
- [ ] `required-checks.py --verify` exits 0 on the live repository, and it exited 1 before `--apply`.
- [ ] With `VERSION` released and only `lib/src/darwin` changed, `version bump` fails, naming the macOS app and the bump.
- [ ] A `gh` that cannot answer fails `version bump`. A transient error never reads as "not released".
- [ ] The docs and skills say "Stable: CI must pass" is in force only once `--verify` says so, and a missing ruleset is reported as needing the user. The full suite is green.

**Scope note.** Earmarked for v0.2.1 (track 57); ungated. Out of scope: `release-from-pr.sh`'s steps, which already refuse correctly (the gates in front of them were missing); the "Stable" ruleset's review rules and admin bypass; requiring an approval; notarization; any GUI-queue change, since the app's pipeline and its GUI `DEVELOPER.md` row are updated here as repo-wide process.
