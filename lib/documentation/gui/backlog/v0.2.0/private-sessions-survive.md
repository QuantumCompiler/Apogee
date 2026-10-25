# Private sessions survive navigation, stop and quit

**What / why.** The CLI/GUI audit (2026-10-22, three-lens verified; three independent hunters found the first defect) found that the app's session lifecycle breaks its own promises in three high-severity ways:

1. **Leaving the Sessions place destroys a private conversation.** Navigating anywhere else pauses the live session's child: `AppViewModel.swift:169` calls `SessionLibraryViewModel.leftView()`, which pauses unconditionally at `SessionLibraryViewModel.swift:187-194` (mid-turn sessions pause at the turn's end via `pauseWhenIdle`, `:473-475`). A true `--private` session never acquires a chat id by design. `identifyNewSession` skips `.nothingWritten` (`:274`) and `make(.privately)` sets `listedBeforeNew = nil` (`:407-411`). So on return, `resume()` (`SessionViewModel.swift:268-276`) hits `if chatID == nil { transcript = Transcript() }` and starts a fresh empty private child. The CLI cannot help: `--private` with `--resume` is refused (`chat_session.cpp:832-836`). A multi-turn private conversation vanishes from visiting Models and coming back, with no warning. The "Stop the turn and switch?" confirmation guards only switching conversations. The recorded "starts afresh" decision (GUI MILESTONES:504) covers the ambiguous-id case, not private sessions.
2. **App quit never completes the session shutdown.** On ⌘Q the process exits without awaiting the `onDisappear` task (`ApogeeApp.swift:83-85`), so the stop escalation and the deleted-on-close fallback's `chats delete` (which runs only inside `close()`, `SessionLibraryViewModel.swift:431-441`) never run. That breaks the fallback's stated promise (`SessionViewModel.swift:141-146`) and MILESTONES 1g's record ("quitting the app ended the child", :124). The close-then-reopen race fails the same way: `.task { await model.start() }` re-runs while the prior shutdown is inside its 5-second grace. No test pins any quit or close path.
3. **Stopping a session that is still connecting is a no-op, and the child it spawns moments later runs unsupervised.** `session` is assigned only at `SessionViewModel.swift:190`, after the `locateBinary` suspension (`:171-190`). A stop during that window (`:243-245`, `await session?.stop(grace:)` with `session` still nil; `:257-263` guards out) does nothing, and `switchTo` (`SessionLibraryViewModel.swift:390-395`) replaces `current` without cancelling its running task. The one-live-child law (`:13-17`, GUI MILESTONES:494) breaks in exactly the window no test covers: the suite emits `session` before every switch (`SessionLibraryTests.swift:81-105`).

**Core constraint(s).** **A private conversation is never silently destroyed.** Leaving the place does not pause a session that cannot resume. Either it stays live in the background (one live child is still one), or leaving asks first ("This private conversation can't be reopened. Keep it running, or end it?"). **Shutdown completes**: the app delays termination (`applicationShouldTerminate` with `.terminateLater`) until every session's stop escalation and close-time delete finish, bounded by the existing grace. **The one-live-child law holds in every window**: a launch in progress is cancellable, and a stop or switch during it cancels the launch and reaps any child it produced. ADR 0001/0002: the fix lives in the session view-models and the app scene, testable headlessly over scripted spawners.

**Seam + files.** `presentation/sessions/ViewModels/SessionLibraryViewModel.swift` (leftView's private carve-out; switchTo cancelling the in-flight launch), `presentation/session/ViewModels/SessionViewModel.swift` (a cancellable connect; stop during locating), `presentation/app/Views/ApogeeApp.swift` + an app delegate (termination waits on shutdown). Tests: a private session across leave/return keeps its transcript and child (or asks); quit with live sessions runs every stop and delete before terminating (a scripted spawner records the order); stop and switch during `locateBinary` leave zero live children.

**Reference.** GUI MILESTONES 1c/1f/1g: the lifecycle and the private fallback; the CLI's private law (52d) and its refusal of `--private --resume`; the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call: every issue becomes a v0.2.0 item in its queue). The private-wipe finding was reported by three hunters independently, so it is the strongest signal in the audit.

**Open calls:**
- [default: a private session stays live when its place is left (it is the one live child), and a switch away asks before ending it.]
- [default: termination waits at most the existing 5-second grace per session, then proceeds, said in the log.]

**Guardrail(s).** Each lifecycle path pinned over scripted spawners and a scripted clock; the one-live-child law asserted after every operation in the matrix.

**Acceptance criteria:**
- [ ] Visiting another place and returning never loses a private conversation without the person's say.
- [ ] Quitting the app ends every session child and completes the deleted-on-close fallback.
- [ ] Stop or switch during a session's launch leaves no live child behind; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 4, the CLI/GUI audit's GUI findings); ungated. The CLI's matching private fixes, [55a](../../../cli/assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings), shipped 2026-10-25; neither gated the other.
