# The docs at the release boundary: v0.1.9 shipped, v0.2.0 is the branch

**What / why.** The v0.1.9 → v0.2.0 boundary pass never ran. The CLI/GUI audit (2026-10-22) found the release pointers and the docs that read from them still describing the last release as in development:

- **`lib/release/VERSION` reads `0.1.9`**, and `lib/src/cli/CMakeLists.txt:35` says `VERSION 0.1.9`, on the v0.2.0 branch, while `v0.1.9` is tagged at the 2026-10-20 merge of PR #10. The CLI changed by ~40.7k insertions across 362 files since. Per the Versioning rule (CLAUDE.md), the first commit on a new release branch bumps the file, or the pull request's `version bump` check fails, and a merge naming an already-released version publishes nothing. The branch binary prints `apogee 0.1.9 (88720bbf, …)`.
- **ROADMAP.md**: `:5` says the live branch is `v0.1.9`. v0.1.9 (tagged, every box checked) still sits under **In progress** at `:244` instead of **Shipped releases** (which ends at v0.1.8). `:327`'s "*Nothing is queued past v0.1.9 yet*" sits directly under the `### v0.2.1` section. The Unspecced-ideas entry "GUI sibling applications" (`:331`) still says the GUI "needs its own planning pass" and calls `lib/src/darwin` a planned home, though the GUI has its own docs home and Milestones A–D.
- **Root README.md:98** lists v0.1.8 as newest and v0.1.9 as in development.
- **CLAUDE.md**: `:24` calls the repo "brand-new", with docs "from which the first code will be built". `:236`'s CI row says 1,411 Catch2 cases, 34 `add_test()` entries and 18 that run the executable; today `tests/CMakeLists.txt` has 60 `add_test()` (~41 running `$<TARGET_FILE:apogee>`) and ~3,800 test-case macros. `:232`'s reference row omits `training.md`.
- **DEVELOPER.md:25** says "the one workflow". Two exist (`ci.yml`, `darwin.yml` since 2026-10-20).

This item is the boundary pass done properly. The release pointer moves through the repo's own skill (`/apogee-maintenance-update-release`): VERSION and the CLI's CMake version to `0.2.0`, ROADMAP's states swapped, README's status line. Then the remaining stale sentences are corrected, each against the repo's facts.

**Core constraint(s).** **The release pointer moves only through its procedure** (the update-release skill: VERSION and the CLI version together, never one alone; never publishing, never rewriting history). **Docs state facts as of the change**: counts are measured, not estimated; shipped history stays as history. **No content beyond the drift**: these are corrections, not rewrites.

**Seam + files.** `lib/release/VERSION`, `lib/src/cli/CMakeLists.txt` (via the skill); `lib/documentation/cli/assistant/ROADMAP.md`, `CLAUDE.md`, `DEVELOPER.md`; root `README.md`.

**Reference.** CLAUDE.md → Versioning and Release and Install Infrastructure; the `apogee-maintenance-update-release` skill; the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call). The VERSION bump is the most consequential line, because without it the v0.2.0 merge publishes nothing.

**Open calls:**
- [default: v0.2.0 is the version (the branch's name and every doc's intent). The skill's preflight confirms nothing else claims it.]

**Guardrail(s).** The skill's own validation; `lib/scripts/version-check.sh` answering green for the branch; a link sweep after the ROADMAP moves.

**Acceptance criteria:**
- [ ] VERSION and the CLI's CMake version read `0.2.0`; the `version bump` check passes for the branch.
- [ ] ROADMAP shows v0.1.9 shipped, the live branch v0.2.0, no "nothing queued past v0.1.9"; the GUI idea points at its docs home.
- [ ] README, CLAUDE.md and DEVELOPER.md state today's facts.

**Scope note.** Earmarked for v0.2.0 (track 55); ungated, small, worth taking early. The GUI docs' drift is [`docs-and-board-catch-up.md`](../../../gui/backlog/v0.2.0/docs-and-board-catch-up.md) (GUI 4l).
