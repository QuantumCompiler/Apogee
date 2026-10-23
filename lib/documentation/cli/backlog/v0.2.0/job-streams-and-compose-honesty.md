# Job streams and the composer keep their word: cancellable pulls, honest compose reruns

**What / why.** 52b and 52c shipped long jobs as event streams and the composer's plan documents. The CLI/GUI audit (2026-10-22, three-lens verified) found four places where those promises break:

1. **A plain `models pull` cannot be cancelled, so its stream never ends on an error** (high). `models_pull.cpp:3458` builds a default `CancellationToken`, which "can never be cancelled" (`contracts/cancellation.h:24-26`), and feeds it to `models::http_source` (`:3507`) and `models::acquire` (`:3565-3570`) with no `InterruptScope` anywhere in `pull_one` or the callback (`:3633-3649`). The `--safetensors` path passes a fresh `CancellationToken{}` (`:3441-3443`) into `pull_snapshot`, so its cancel check (`:1560`) never fires. The register chain's own snapshot download (`pull_snapshot` at `:2830-2831`) is equally uninterruptible. Only pipeline, voice and mlx pulls (`:3482`, `:3491`, `:3497`), the chain's conversion stages (`:2005-2051`) and the hash (`:191`) install the SIGINT scope (`interrupt.cpp:24-28`). This contradicts machine-mode.md:285-287 and Milestone AO ("Failure and Ctrl-C end the stream on an error"). The GUI's `SupervisedJob.stop()` interrupts exactly this way.
2. **A resumed composition silently swallows `--edit` and `--redo-scene`.** At `video_compose.cpp:417`, `if (resumed.has_value())` runs before the edit pass at `:439`, and `ignored_flags` (`:71-95`) lists seven flags but not those two. Probe: with a resumable edit project, `video compose --from tides.mp4 --edit "make it about storms instead" -o edited.mp4 --dry-run` exited 0 with no plan call, saying "a dry run: the plan kept at … nothing was planned again". The instruction vanished without a word. Also observed: `--model not applied` fires even when `-m` names the kept plan's own model.
3. **Rendering a kept plan overwrites an existing `<output>.srt`.** The never-write-over guard for unburnable captions (`video_compose.cpp:397-403`) is skipped under `--dry-run` and on every resumed project (`!resumed.has_value() && !options.dry_run`). The 52c flow (plan on paper, then render the kept plan) therefore never checks it, and assembly then runs `copy_file(srt, beside, overwrite_existing)` (`business/video/compose.cpp:478-486`). A fresh non-dry run is refused (pinned by `video_compose_test.cpp:377-385`), so the overwrite violates the intended invariant.
4. **A second `--dry-run` announces `rendering the plan…`.** The `project.status == "planned"` branch (`video_compose.cpp:424-427`) says it unconditionally, as stderr text and as a stream-json notice, though nothing renders. machine-mode.md (~:881-886) ties that wording to the real render pass, so a driver keying on it takes a plan-on-paper run for a render.

The doc half: **machine-mode.md:280-284** (repeated at Milestone AO) implies a failed or cancelled media run's document is "the record beside the output". `video generate` and `voice say` return before writing on failure (`video_generate.cpp:746-755` vs the write at `:758-762`; `voice_say.cpp:308-340` vs `:341-342`). Only `video compose` saves its failed project (`compose.cpp:269-274`). The streamed document's `sidecar: null` is honest, and the reference wording should match it.

**Core constraint(s).** **Cancellation is real on every pull path**: one `InterruptScope` around each pull's work, the 52b stream ending on its error event exactly as the docs say. Never a second signal mechanism. **No flag is silently dropped**: a resumed project either applies `--edit`/`--redo-scene` (a fresh edit pass over the kept project) or refuses them by name, never ignores them. **Never write over** holds on every path that writes beside the output, the dry-run → render chain included. **Words match deeds**: a dry run never says "rendering". **Default output unchanged** where the fix isn't about it (52b's byte-identity law).

**Seam + files.** `presentation/cli/models_pull.cpp` (the interrupt scope across `pull_one`, the `--safetensors` path and the chain's download), `presentation/cli/video_compose.cpp` (the resumed path's handling of `--edit`/`--redo-scene`, the captions guard on every rendering path, the dry-run wording), `lib/documentation/cli/reference/machine-mode.md` (failed media documents in-stream; `sidecar: null`). Tests: an interrupted plain pull and `--safetensors` pull over a scripted slow source ending the stream on its error; the resumed-project flag matrix (applied or refused, never ignored); the plan→render chain refusing an existing `.srt`; the second dry run's notice wording golden.

**Reference.** 52b/52c (shipped, Milestone AO): the streams and documents whose promises these restore; the CLI/GUI audit (2026-10-22). Related GUI work: [`media-studio-finished.md`](../../../gui/backlog/v0.2.0/media-studio-finished.md) and [`downloads-and-store-jobs.md`](../../../gui/backlog/v0.2.0/downloads-and-store-jobs.md) (GUI 4f, 4e). Neither gates this.

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call).

**Open calls:**
- [default: a resumed project given `--edit`/`--redo-scene` runs the edit over the kept project (the user asked for a change; the kept plan is its parent). A mismatch the edit pass cannot reconcile is refused by name.]
- [default: `--model not applied` is said only when `-m` differs from the kept plan's model.]

**Guardrail(s).** Each finding's probe pinned as a regression test; the 52b default-output byte-identity pins kept green.

**Acceptance criteria:**
- [ ] Every `models pull` path is cancellable and ends its stream on an error event when interrupted.
- [ ] A resumed composition applies or refuses `--edit`/`--redo-scene`, never drops them; rendering a kept plan never overwrites an existing `.srt`; a dry run never says "rendering".
- [ ] machine-mode.md describes failed media documents as they are; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 55); ungated.
