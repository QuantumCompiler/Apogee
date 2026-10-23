# The session's lines and transcript stay honest

**What / why.** The app drives a session through the protocol's lines and draws what its events say. The CLI/GUI audit (2026-10-22, three-lens verified) found eight places where the app's view of a session drifts from the session itself:

1. **`set`/`detach` sent mid-turn are trusted as applied, and a pending question silently eats them.** `apply()` (`SessionViewModel.swift:343-357`) merges the change into `settings`, and `detach()` (`:328-338`) removes the chip, before the CLI answers, guarded only by `canSet`/`canDetach`, never by `transcript.status`. While a question or permission card waits, the CLI drops every non-answer line with no notice (`json_reporter.cpp:685-688`, `permissions.cpp:379-383`; 52d: "a host sends set between turns"). Probe: user line, then `{"type":"set","temperature":0.9}`, then answer `no`. The stream carried zero notices and the set vanished, while the sheet and chips claimed it took effect. (`/compact` is not user-reachable mid-turn; the composer is disabled.)
2. **Pause/resume relaunches with stale launch flags.** In the `canSet` path, `apply()` (`:343-348`) never updates `launch.settings`, and `connect()` rebuilds from `self.launch` (`:181-189`). A session launched with explicit settings (the Models library's "Use in a new session", or the execute door) therefore relaunches on every pause/resume with the original `-m`, which overrides the model switched with `set`. Probe: `chat --resume <id>` reopened on the switched model; with the stale `-m`, the flag won. Distinct from M10, which fixes the label.
3. **A question card orphaned by a cancelled or failed turn stays answerable.** The `.result`/`.error` handlers (`Transcript.swift:290-304`, `:313-318`) never collapse an unanswered card. `answered(_:with:)` (`:79-95`) accepts any card with `answers == nil`, so the user can answer a dead question and the app writes stray `answer` lines.
4. **Temperature can never be set at launch.** `ChatLaunch.swift:36`'s comment says temperature has no launch flag; the CLI has `-t/--temperature` on `chat` and `execute` (verified on the branch binary). On a pre-`set` binary the field is hidden (`SessionExtras.swift:215-217`), leaving no way at all.
5. **The transcript matches live events to earlier rows by colliding turn numbers.** Earlier rows keep the saved numbers (`Transcript.swift:144-172`), and live handlers match on `item.turn == turn`. When the CLI's `next_turn` collides (CLI 55b's finding), `finishAnswers` (`:384-393`) stamps live usage onto the earlier row, and the result handler (`:292-295`) drops a result-only live answer entirely.
6. **A resumed chat's standing attachments get no chips.** `attachments` starts empty (`SessionViewModel.swift:41`), so `/detach` stays disabled (`:416`), though the CLI keeps them live and accepts `detach`. `SavedChat.swift:26-27` already holds the names.
7. **The resumed-turns read drops the loader's warnings.** `chats show` writes `FieldDropped` warnings to stderr (`chat_history.cpp:368-373`). `readEarlier` (`SessionViewModel.swift:219-224`) keeps only the document, and `ReadRunner` drops stderr on success, so a chat with a dropped message looks unbroken.
8. **Rename on the open session is undone by its next turn** (completeness pass, with probe). The app's Rename (`SessionLibraryViewModel.swift:486-497`) runs `chats title` on the open session's chat without ending it, and the CLI's running session overwrites the name on its next save (CLI 55b). Delete ends the session first (`:506-512`); Rename doesn't.

**Core constraint(s).** **The transcript is the protocol's truth** (1c's law): a change is shown applied when the protocol confirms it, or sent only when the protocol will take it. Lines queue until `status == .ready`, and settings roll back if no confirming notice arrives. **Relaunch carries the session's current settings**, never the launch-time ones. **Earlier rows are a separate range**: identity is (range, turn), never turn alone. **What the CLI says, the app shows**: warnings and notices that ride the protocol (CLI 55b adds them) are drawn, and until then the read's stderr tail is surfaced. ADR 0002: view-models SwiftUI-free, scripted-event tests.

**Seam + files.** `presentation/session/ViewModels/SessionViewModel.swift` (queued lines; launch settings updated with every `set`; chips seeded from `SavedChat`; warnings read), `presentation/session/ViewModels/Transcript.swift` (earlier/live identity; orphaned cards collapse on result/error), `business/session/ChatLaunch.swift` (`-t`), `business/reads/ReadRunner.swift` (stderr tail on success), `presentation/sessions/ViewModels/SessionLibraryViewModel.swift` (rename coordinated with the live session). Tests: a scripted question-then-set stream (the set queued and applied after the answer); pause/resume after `set model` relaunching on the switched model; orphaned-card refusal; colliding-number fixtures; resumed chips; warnings drawn; rename surviving the next turn.

**Reference.** GUI MILESTONES 1c/1g and M7: the transcript, settings and resume laws; CLI 52d's between-turns rule; [`session-record-truth.md`](../../../cli/backlog/v0.2.0/session-record-truth.md) (CLI 55b), the CLI half (notices, numbering, outside renames), referenced and not gated; GUI M10 (`maintenance/session-label-follows-its-chat.md`, the session's label), adjacent; the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call). The rename finding came from the completeness pass, reproduced first.

**Open calls:**
- [default: lines queue until the turn ends and the card closes, rather than optimistic-apply-then-rollback (simpler and never wrong).]
- [default: Rename on the open session sends nothing to the live child; it pauses-renames-resumes (an ordinary chat resumes by id) until CLI 55b makes the live save respect outside renames.]

**Guardrail(s).** Each finding pinned over scripted event streams and recorded documents; no live binary in tests.

**Acceptance criteria:**
- [ ] No setting, detach or answer is shown as applied unless the session took it.
- [ ] Pause/resume keeps the session's current model and settings; temperature can be set at launch.
- [ ] Earlier rows are never matched to live events; resumed chips and warnings are drawn; a rename survives the session's next turn; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 4); ungated.
