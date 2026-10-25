# The media studio, finished: install consent, compose's voice model, honest toasts, captions up front

**What / why.** Video, Compose and Voice (1m–1o, upgraded by 2-wave items) share the media machinery. The CLI/GUI audit (2026-10-22, three-lens verified) found seven gaps:

1. **The consented runtime install runs with no UI, fails silently, and kills the retry.** `VideoViewModel.install()` (`:314-328`) and `VoiceViewModel.install()` (`:260-274`) publish `installing`, but no view reads it (grep: only the two view-models). The job is a local never retained, so there is no progress, no Stop, and `isRunning` stays false: a second generate mid-install is refused again and re-raises the sheet. On failure nothing is said (`installing = done ? nil : installing` keeps it forever), and `retryAfterInstall`/`retry` is never consumed. `dismissInstall()` has no caller.
2. **The compose sheet cannot name a voice model, so every new composition is refused once the store holds two.** `ComposeForm` (`Projects.swift:164-206`; `ComposeView.swift:284`) has no `--voice-model`. The CLI resolves it with no default (`video_compose.cpp:376-385` → `voice_say.cpp:69-86`: "the store holds N voice models -- name one with --model"). The refusal precedes the dry-run branch, so both Plan and Compose fail. `VoiceViewModel` already ships the multi-model picker (`:199-207`); Milestone D's not-done records compose as untouched.
3. **Compose lacks the install-consent flow its siblings have.** A runtime-not-installed refusal is an OK-only alert (`ComposeViewModel.swift:151-156`, `ComposeView.swift:71-75`), never consulting `MediaLibrary.installCommand(in:)`, and it reads only the failure message, not stderr. Compose needs both runtimes, so a fresh install composing first hits this.
4. **"Plan first" toasts "Composed <name>.mp4".** `compose(planFirst:)` appends `--dry-run` (`:115-120`), but `.done` says `Composed …` unconditionally (`:151-152`). The project's real state is `planned`, with a Render-it button.
5. **Preview-the-changes' dry-run child is unsupervised.** `previewEdit` (`:189-210`) keeps its job in a local, so `stop()` (`:174-176`) cannot reach it and `SupervisedJob` has no time limit. A hung planner leaves `previewing` true forever (Preview disabled, `ComposeView.swift:301-307`). It also bypasses the `job == nil` guard, so a preview can run beside a render.
6. **Compose discards the scan's skipped-record diagnostics.** `refresh` keeps only `.records` (`ComposeViewModel.swift:66-68`), so an unparseable project or one whose output moved vanishes silently. Clips and Voice show each skipped record with its reason (`VideoViewModel.swift:121-126`, `VoiceViewModel.swift:81-85`).
7. **Captions' burn ability is still learned after the first composition**, though the CLI shipped G14 (52e: `check`'s `ffmpeg captions`/`ffmpeg h264` rows, `skipped` with the `ffmpeg-full` remedy). The toggle (`ComposeView.swift:283`) never says up front that captions will be kept as `.srt`; the page says so only after rendering (`:158-161`); and Settings' Health shows only fail/warn rows (`SettingsView.swift:207`), so the skipped rows and their remedy appear nowhere. Homebrew's default ffmpeg lacks libass, which is the common case on macOS and on the user's own machine (52e's record).

**Core constraint(s).** **Every child is supervised**: installs and previews are jobs with progress, Stop and a card, registered with the one children registry (GUI 4c). **Every refusal has its door in the app**: install consent and a voice-model picker wherever a media verb can refuse for them. **Words match deeds**: a plan-only run says "planned", never "composed". **Every diagnostic is shown**: skipped records, and `check`'s skipped rows with their remedies (Health draws skipped rows that carry a remedy). **The CLI's facts up front**: the compose sheet reads the captions row and says burn vs keep before the run. ADR 0002, read-back after runs.

**Seam + files.** `presentation/video/ViewModels/VideoViewModel.swift`, `presentation/voice/ViewModels/VoiceViewModel.swift` (installs as supervised jobs, drawn, retry consumed, failure said), a shared install view in `presentation/styles` or `media`, `business/media/Projects.swift` + `presentation/compose/` (the voice-model picker, consent flow, planned toast, supervised preview, skipped diagnostics, captions row read), `presentation/settings/Views/SettingsView.swift` (skipped rows with remedies). Tests: install job lifecycle (progress, stop, failure toast, retry); compose with two voice models; the plan-first toast; preview stop and timeout; skipped records listed; the captions wording over a check fixture with the row skipped.

**Reference.** GUI MILESTONES 1m–1o and Milestone D; CLI 52c/52e (dry runs; the ffmpeg rows); [CLI 55c](../../../cli/assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings) (shipped 2026-10-25: a resumed composition applies or refuses `--edit`, a dry run never says "rendering"), referenced and not gated; the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call). G14's board entry flips with [`docs-and-board-catch-up.md`](docs-and-board-catch-up.md) (4l).

**Open calls:**
- [default: the voice-model picker appears only when the store holds two or more voice models (the Voice place's rule).]

**Guardrail(s).** Each finding pinned over scripted jobs, documents and a check fixture; no live binary in tests.

**Acceptance criteria:**
- [ ] Runtime installs show progress, can be stopped, say their failure, and retry the refused run on success.
- [ ] Compose works with several voice models, offers install consent, never says "composed" after a plan, supervises its preview, and lists skipped projects.
- [ ] Whether captions burn is said before a composition runs, and Health shows skipped rows with their remedy; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 4); ungated.
