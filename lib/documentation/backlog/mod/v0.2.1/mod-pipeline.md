# MOD-1d · The mod pipeline: gated, honest about its gates

| Field | Value |
|---|---|
| **Type** | 🛠️ Task |
| **Project** | Claude plugin (mod) |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.1 |
| **Component** | CI/CD pipeline (`mod.yml`) |
| **Labels** | `ci` `changed-gate` `honest-skips` |
| **Epic** | Track 1 · the mod is born |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The mod spike (2026-10-21) |
| **Created** | 2026-10-21 |

---

## Summary

The mod has no place in CI. This item gives it **its own workflow** beside `ci.yml` and `darwin.yml`, gated by `changed.sh mod`, that gates on `tsc` everywhere, runs `claude plugin validate` where the `claude` CLI installs headlessly, says the skip where it cannot, and produces the plugin folder, zipped, as the artifact.

## Description

### User story

**As** a contributor to Apogee,
**I want** a PR that touches the mod to build and check the plugin in CI, and one that does not to skip under the required name,
**so that** the mod is proven on every change to it, and every gate it cannot run on a runner is said rather than silently missing.

### Background

The third application joins CI the way the darwin app did: **its own workflow** beside `ci.yml` and `darwin.yml`, gated by `changed.sh mod` (a docs-only merge skips under the required name — the house pattern), building and checking the plugin.

The job runs validate **where the CLI installs headlessly** (probed at build; if it cannot, the job says the skip and the dev-host gates stand), and `claude plugin test` stays a dev-host gate either way, stated in the contributor docs. The artifact: the plugin folder, zipped, proven loadable at least by validate wherever validate ran.

### Evidence

The honesty the mod spike recorded (2026-10-21):

1. **Runners can always gate on `tsc`.**
2. **`claude plugin validate`/`test` need the `claude` CLI**, which is not on the runner images.

## Acceptance criteria

- [ ] **Given** a PR touching `lib/src/mod/`, **when** CI runs, **then** the mod job runs (`tsc`, validate where possible, the zipped artifact).
- [ ] **Given** a PR not touching `lib/src/mod/`, **when** CI runs, **then** the mod job skips under the required name.
- [ ] Required checks updated with the user's approval; the dev-host gates stated in CLAUDE/DEVELOPER.

## Constraints

- **One build path.** The workflow calls the same entries a developer runs.
- **Required checks re-derived with the user's go-ahead**, per the repo's pipeline checklist.
- **Nothing else's pipeline changes.** No CLI or darwin pipeline job, name or gate changes.
- **Skips said, never silent.**

## Technical approach

| File | Change |
|---|---|
| `.github/workflows/mod.yml` | the mod's own workflow, gated by `changed.sh mod`: `tsc`, validate where the `claude` CLI installs headlessly (the skip said otherwise), the zipped artifact |
| `lib/scripts/changed.sh` | the `mod` entry 1a added, consumed |
| the mod's own `package`-ish step | a zip of the plugin folder |

**Reference.** The darwin pipeline (the GUI's 1d) — the gated-sibling-workflow pattern mirrored; the mod spike (2026-10-21) — the toolchain facts.

**Docs when it ships:** the contributor docs (CLAUDE/DEVELOPER) state the dev-host gates (`claude plugin test`, and `claude plugin validate` wherever the runner cannot run it).

## Testing

- **The gate both ways.** Proven with a mod-only diff (the job runs) and a docs-only diff (it skips under the required name).
- **YAML parse.** The workflow YAML-parsed.
- **The stated-skip wording golden.** The skip the job says when validate cannot run.
- **Nothing else's pipeline touched.**
- **Required checks.** The first green PR's `required-checks.py --apply` asked of the user.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Does the runner install the `claude` CLI? | `[default]` | probed at build; the honest skip otherwise |
| What is the artifact named? | `[default]` | the artifact name in the house style |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-10-21 | The user's call (with the spike's placement answers): the mod gets its own specced CI/CD; `tsc` is the unconditional runner gate, validate conditional on the CLI installing headlessly, test dev-host. | the user |
| 2026-11-21 | 1a shipped ([Milestone A](../../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born)), and this item inherits its shape: `changed.sh mod` already declares `lib/src/mod/`, `lib/scripts/changed.sh` and `.github/workflows/mod.yml`; the one build path is `lib/src/mod`'s npm scripts (`npm ci`, then `npm run typecheck`, which needs no engine because the 2.1.326 API declarations are vendored at `vendor/claude-code/index.d.ts`; `validate` and `test:plugin` run `$CLAUDE`); the plugin's engine floor is Claude Code 2.1.326, so a runner's `claude`, where it installs, must be that or newer; plugin.json states version 0.2.1, and whether `version bump` holds it is this item's call; the mod is not yet in `cicd.sh`'s `APPS`. Flagged for veto with 1a's decisions | 1a (the run) |

## Out of scope

- Distribution (a marketplace entry — the recorded later question)
- Release attachment
- Other pipelines

## Links

- **Blocked by:** — (1a shipped 2026-11-21, [Milestone A](../../../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born)) · **Blocks:** —
- **Relates to:** the GUI's 1d (the darwin pipeline, the pattern mirrored); the mod spike (2026-10-21)
