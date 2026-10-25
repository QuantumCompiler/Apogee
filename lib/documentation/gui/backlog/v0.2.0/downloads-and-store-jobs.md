# Downloads and store jobs keep their identity: the right GGUF, honest resumes, delete's warnings

**What / why.** The Models place's Jobs and Stored-weights tabs (2h/2i) run pulls, conversions and quantizes as supervised jobs. The CLI/GUI audit (2026-10-22, three-lens verified) found four defects:

1. **Quantize's follow-up registration can bind the backend to the wrong GGUF** (high). `register()` picks `download.landed.first(where: { $0.contains("/gguf/") })` (`DownloadsViewModel.swift:260-262`) and runs `config add-backend <name> --type llamacpp --model-path <that set>`. But `landed` is `names.subtracting(download.before)`, sorted per read-back batch with no filter by the job's own model folder (`:286-293`). The Jobs tab runs jobs concurrently, so another job's GGUF that lands first is registered under the quantize's name, silently, since add-backend succeeds. The before-snapshot also collapses to `[]` when the first `models list --all` read fails (`await storeNames() ?? []`, `:222`), after which every pre-existing GGUF counts as landed.
2. **A successful chained pull shows a spurious "Run models convert…" resume button.** `JobProgress.resumeCommand` (`JobProgress.swift:143-153`) picks up the CLI's mid-run `from here on, if this stops, resume with: apogee models convert …`, which is printed even on success (`model_chain.cpp:49-53`). `DownloadsView.swift:86-88` shows it for any non-running job, `.done` included (only "Run again" is gated). Clicking it re-runs the chain as a no-op duplicate card.
3. **"Run again" and named resume re-enter store jobs as plain pulls.** `again()`/`resume()` (`DownloadsViewModel.swift:157-167`) route through `run(_:reference:)` (`:172-183`), which builds a `.pull` with no steps, no `registerAfter`, no before-snapshot, and follows with `follow()` instead of `followStore()`. A quantize with "Register it" that failed transiently and is re-run makes the GGUF but never registers it, with nothing saying so. Its `endedAt` is never set either, so elapsed runs forever.
4. **The app's delete swallows the stranded-backend warnings and their doors.** `StoreViewModel.run()` drops stdout on success whenever a `done` string is passed (`StoreViewModel.swift:710-725`, called at `:683-685`), so M14's narration is lost on both paths ("Delete" and "Delete it and the backends that run from it"). That narration is `backend 'X' points into this and will stop working until it is repointed`, plus the two doors for a kept backend. The confirm dialog is generic (`StoreView.swift:76-83`): it names no affected backend and offers the "and the backends" button even when none points in.

**Core constraint(s).** **A job knows what it made**: a registration binds to the set the job's own command produced, identified by the job's model folder and its own stream (52b's events name the stored entry), never by a global before/after diff. A failed before-read is a failed job step, said, never an empty set. **A job's identity survives re-runs**: again and resume rebuild the same kind with its steps and registration. **Resume buttons only on stops**: a resume command shows only on a job that did not finish. **The CLI's words are shown**: delete's narration (warnings, doors, removed backends) is drawn, and the confirm dialog names the backends a dry read says point in. ADR 0002; read-back after every mutation.

**Seam + files.** `presentation/models/ViewModels/DownloadsViewModel.swift` (registration from the job's own result; re-runs keep kind/steps/registerAfter; a failed snapshot fails the step), `business/jobs/JobProgress.swift` (resume only for non-done), `presentation/models/Views/DownloadsView.swift`, `presentation/models/ViewModels/StoreViewModel.swift` + `Views/StoreView.swift` (delete narration kept; the affected backends named before confirming, read from a dry run of the same command). Tests: two concurrent jobs, each registering its own set; a failed first read; a done chain with no resume button; again on a failed quantize re-registering; a delete's warnings and doors drawn.

**Reference.** GUI MILESTONES 2h/2i: the store screens and jobs; CLI M14 (the delete's warnings and doors) and 52b (stream events naming the stored entry); [CLI 55d](../../../cli/assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings) (shipped 2026-10-25: delete seeing every reference, `wrong kind`, MLX families renaming by name), referenced and not gated; the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call).

**Open calls:**
- [default: the confirm dialog's affected-backend list comes from running the delete without `--yes` (the CLI's own plan print) before asking, so the app never computes references itself.]

**Guardrail(s).** Each finding pinned over scripted job streams and documents; no live binary in tests.

**Acceptance criteria:**
- [ ] A quantize registers exactly the GGUF it made, even beside concurrent jobs.
- [ ] Done jobs never offer resume; re-runs keep the job's kind, steps and registration.
- [ ] Delete names the backends it affects before confirming and shows the CLI's warnings and doors after; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 4); ungated.
