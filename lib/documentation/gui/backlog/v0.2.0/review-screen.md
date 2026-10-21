# The review screen: merge requests, targets and skills

**What / why.** The review agent (the CLI's track 48) has no GUI place — the TUI classified it runner-covered with "a Review view is a recorded later item"; the parity mandate (2026-10-20) makes the GUI's view now. The surface over the shipped verbs: **a review run** — `review mr <ref>` as a supervised job, `--dry-run` first-class (the findings document rendered: each finding's file, line, claim, suggested fix), posting behind the same explicit consent the CLI's gate asks (`--yes` only on the user's click, the batch said before it goes); **targets and sweeps** — the `review:` unit's targets listed, a sweep launched supervised, watermarks and the posted/documents-only honesty shown; **skills** — the seven lenses and the user's own, listed and shown from `review skills list|show`. Temperament renders as the CLI's law: **silence is correct** — a clean review's "nothing to post" is a first-class result, never an empty-looking error.

**Core constraint(s).** ADR 0001/0002; every run the person's command (argv-pinned), documents re-read after; **posting is consented per run** — the GUI never passes `--yes` without the explicit click on a shown batch (the 27i ceiling's spirit client-side); forge refusals (missing `gh`, logged out) shown verbatim with their fix commands; dry-run is the default posture. **A design board lands with it.**

**Seam + files.** `source/presentation/review/` (runs, targets, skills; view-models), `source/business/reads/` (+ the review documents), the jobs core; `lib/design/spec/` — the board and contract rows.

**Reference.** The CLI's 48a–48d — the verbs, documents, temperament and gate consumed whole; the TUI's recorded-later note — superseded for the GUI by the mandate; the finding schema (the `code-review` agent's) — the rendered shape.

**Decisions made** (dated):
- 2026-10-20 — From the parity audit (the user's mandate); dry-run-first and click-consented posting are the screen's law.

**Open calls:**
- [default: the findings list's layout per the new board; details at build.]

**Guardrail(s).** View-model matrices over fixture review documents (findings, the clean-silence result, sweep states); the consent pin (no `--yes` without the click, the batch shown); forge-refusal renders verbatim; hermetic — no live forge in any test.

**Acceptance criteria:**
- [ ] A dry review runs supervised and renders its findings; posting requires the shown-batch click and passes exactly the CLI's flags; silence renders as the valid pass it is.
- [ ] Targets and sweeps list, launch and read back; skills list and show; the board exists; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 2); gated on 2a, the parity law (shipped 2026-10-20: [MILESTONES → Milestone C](../../assistant/MILESTONES.md#milestone-c--cli-parity)). Out of scope: webhooks (the CLI's recorded later), skill editing beyond the files (2e's editor pattern applies later if wanted), any forge change.
