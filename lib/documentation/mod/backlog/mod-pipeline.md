# The mod pipeline: gated, honest about its gates

**What / why.** The third application joins CI the way the darwin app did: **its own workflow** beside `ci.yml` and `darwin.yml`, gated by `changed.sh mod` (a docs-only merge skips under the required name — the house pattern), building and checking the plugin. The honesty the spike recorded (2026-10-21): runners can always gate on **`tsc`**; `claude plugin validate`/`test` need the `claude` CLI, which is not on the runner images — so the job runs validate **where the CLI installs headlessly** (probed at build; if it cannot, the job says the skip and the dev-host gates stand), and `claude plugin test` stays a dev-host gate either way, stated in the contributor docs. The artifact: the plugin folder, zipped, proven loadable at least by validate wherever validate ran.

**Core constraint(s).** One build path (the workflow calls the same entries a developer runs); required-checks re-derived with the user's go-ahead per the repo's pipeline checklist; no CLI or darwin pipeline job, name or gate changes; skips said, never silent.

**Seam + files.** `.github/workflows/mod.yml`; `lib/scripts/changed.sh` (the entry 1a added, consumed); the mod's own `package`-ish step (a zip of the folder). Tests/verification: the gate proven both ways (mod-only vs docs-only diffs); the workflow YAML-parsed; the first green PR's `required-checks.py --apply` asked of the user.

**Reference.** The darwin pipeline (the GUI's 1d) — the gated-sibling-workflow pattern mirrored; the mod spike (2026-10-21) — the toolchain facts.

**Decisions made** (dated):
- 2026-10-21 — The user's call (with the spike's placement answers): the mod gets its own specced CI/CD; `tsc` is the unconditional runner gate, validate conditional on the CLI installing headlessly, test dev-host.

**Open calls:**
- [default: whether the runner installs the `claude` CLI (probed at build; the honest skip otherwise); the artifact name in the house style.]

**Guardrail(s).** The gate both ways; YAML parse; the stated-skip wording golden; nothing else's pipeline touched.

**Acceptance criteria:**
- [ ] A PR touching `lib/src/mod/` runs the mod job (`tsc`, validate where possible, the zipped artifact); one not touching it skips under the required name.
- [ ] Required checks updated with the user's approval; the dev-host gates stated in CLAUDE/DEVELOPER.

**Scope note.** Earmarked for v0.2.1; gated on [`mod-scaffold.md`](mod-scaffold.md) (1a). Out of scope: distribution (a marketplace entry — the recorded later question), release attachment, other pipelines.
