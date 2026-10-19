# The review sweep: repos watched without a daemon

**What / why.** One MR on demand is [`review-core.md`](review-core.md)'s; this item is the watch — a repo, a list of repos, or an owner's whole set reviewed as new merge requests appear — built inside the house's standing laws instead of against them: **interactive turns never listen, and nothing here is a daemon** (the cycle's and the task runner's recorded stance). The shape is the **one-shot sweep over a watermark** (the training cycle's precedent): `apogee review sweep` reads the configured targets, asks each forge through the seam for merge requests opened or updated since the stored watermark, runs the core on each, posts, advances the watermark, and **exits** — the user's cron or CI provides the cadence. Targets live in config as a `review:` unit: repos by name, or an owner/organization whose repo list is expanded through the seam at sweep time (the user's three pointing modes, 2026-10-18: a single MR — the core's verb — a repo, or a list per organization/individual); each target may name its skills, else the plan stage decides per MR. Webhook ingestion — push instead of poll — is the **recorded later item**, and it belongs under `serve` (the one listener) with signature verification, never under an interactive command.

**Core constraint(s).** **One shot, always**: no loop, no timer, no PID held past the run — two overlapping sweeps are excluded by the one-lock idiom (`platform/pid_lock`), the second exiting saying so. **The watermark is the cycle's discipline**: per target, advanced only after that target's MRs are handled, stored under a layout row (same-commit rule), a crash resuming exactly where it stopped; an MR mid-review on a crash is re-reviewed safely because the core's re-review injection and incremental filter make the second pass idempotent (`(consumed decision)` — the core's). **Unattended consent is 27i's ceiling**: a sweep posts with nobody at the terminal, so posting is granted at launch (`--allow-post`, or the config target's declared grant), recorded per use, never widened by the run, and absent a grant the sweep reviews into documents and posts nothing — said, never silent. The `review:` config unit is written through the one editor; reads have twins (`review targets` listing with its JSON document); every run's record is the core's document per MR plus the sweep's own summary document. Spend honesty: a sweep is model work — the summary says what ran and on which backend, and a metered backend is simply whatever the user configured (their call, like any session).

**Seam + files.**
- `data/contracts/` — the `review:` config unit (targets: forge, repo or owner, optional skills, the post grant), parsed and edited through the one editor.
- `business/review/sweep.h/.cpp` — the sweep: target expansion (an owner → repos through the seam), the since-watermark MR listing, the per-MR core runs over closures, the watermark store (its layout row same-commit), the summary document.
- `presentation/cli/review_cmd.cpp` — `review sweep [--target …] [--allow-post] [--dry-run]`, `review targets` (+ JSON), the lock, the gate composition (27i's `seed_approvals`/`make_permission_checker`, never a second checker); the parity rows.
- Tests: the sweep over scripted forge fakes (two targets, the watermark advancing per target, a crash-resume case, the lock's second-run refusal, owner expansion, grants declared vs. absent — posts vs. documents-only, each said); config-unit goldens; document conformance; `cli.install_parity` with the new row.

**Reference.** The training cycle (the watermark, the PID lock, the circuit-breaker temperament — the one-shot-over-state shape copied); 27i — the policy ceiling composed, never re-implemented; [`forge-seam.md`](forge-seam.md) and [`review-core.md`](review-core.md) — consumed whole; the review-agent spike (2026-10-18) for the no-daemon reasoning.

**Decisions made** (dated):
- 2026-10-18 — The set lands in v0.1.9 as track 48 (the user's placement call); the watch is **one-shot sweeps over a watermark**, cadence the user's scheduler — never a daemon, never a listener (the spike's design, standing).
- 2026-10-18 — **Webhooks are the recorded later item**, under `serve` with signature verification — push belongs to the one listener, not to an interactive command.
- 2026-10-18 — The three pointing modes are the surface: `review mr <ref>` (the core's), `review sweep` over configured repos, and owner/organization targets expanded at sweep time.
- 2026-10-18 — No grant, no post: an ungran­ted sweep still reviews and writes documents, saying it posted nothing — honesty over convenience.
- 2026-10-18 — The watermark is per target, keyed by forge+repo, storing the newest MR update-time handled; the store's exact shape at build under its layout row (the default, confirmed 2026-10-18).
- 2026-10-18 — A sweep caps MRs per run (a named constant, deferrals counted and said); `--since <date>` seeds a fresh watermark, so history stays unreviewed unless asked (the default, confirmed 2026-10-18).
- 2026-10-18 — Draft/WIP merge requests skip by default; a per-target switch includes them (the default, confirmed 2026-10-18).


**Guardrail(s).** The scripted-forge sweep matrix above (watermark, resume, lock, expansion, grants); the ungranted run's documents-only honesty; the cap's deferral said; config and document conformance; the lock's takeover rules exactly `pid_lock`'s.

**Acceptance criteria:**
- [ ] `review sweep` over configured targets reviews only what is new since each watermark, posts under a declared grant, advances watermarks per target, and exits; a second concurrent run refuses.
- [ ] An owner target expands to its repos through the seam; per-target skills override the plan; drafts skip by default.
- [ ] Ungranted sweeps review into documents and post nothing, said; a crash resumes without double-posting.
- [ ] The config unit, listings and documents are conformant; the full suite is green.

**Scope note.** Earmarked for v0.1.9; gated on [48c](review-core.md). Out of scope: webhooks under `serve` (the recorded later item), stale-thread resolution, learning from feedback, any scheduler of Apogee's own.
