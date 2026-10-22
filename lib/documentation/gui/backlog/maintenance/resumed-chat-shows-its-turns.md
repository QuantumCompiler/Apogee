# A resumed chat shows its earlier turns

**What / why.** The user, 2026-10-22: "When we reload a chat, I want the previous turns to show." Reopening a saved conversation draws only a notice:

`resumed: 5 earlier turns of this conversation aren't shown here -- apogee keeps them, and the model still has them`

The notice comes from the transcript (`lib/src/darwin/source/presentation/session/ViewModels/Transcript.swift:153–159`). When a session's opening event says its next turn is past 1 and nothing is drawn yet, the transcript says the turns exist rather than show a conversation that seems to start here, because "the CLI hands no read of its saved turns to a driver". That was true when written. The CLI's [M27](../../../cli/assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths) adds the read: `chats show <id> --output-format json` (shipped 2026-10-22).

The app already knows which conversation it resumes. `SessionViewModel` keeps the id it passes as `--resume` (`SessionViewModel.swift:90`, `ChatLaunch.swift:75–76`). A session the app paused and resumed itself keeps its rows, since the notice is only drawn into an empty transcript. So only a conversation reopened from Sessions, or after a relaunch, lacks its turns.

**Core constraint(s).**
- **The app stays a machine-mode client.** The turns come from the binary's read, never from the session file read by the app.
- **One transcript.** The earlier turns are rows of the same `Transcript`, drawn as live turns are (the answer as markdown, tools as the activity lines). The live session's turns follow them, numbered on from the last.
- **An older binary keeps today's notice:** a binary without `chats show` (its refusal, or no document) draws the notice as now.
- **ADR 0003:** `chats show` is classified in `ParityTable`, its place the Sessions screen and the session window, and `scripts/refresh-parity-fixture.sh` is rerun.
- **ADR 0004:** the resumed states have previews over recorded documents.

**Seam + files.**
- **`lib/src/darwin/source/business/reads/`:** a typed `SavedChat` read of `chats show`'s document (its turns: the user's text and attachments named, the answer's text, the tools), tolerant of fields it does not know.
- **`lib/src/darwin/source/presentation/session/ViewModels/SessionViewModel.swift`:** a session that resumes reads `chats show <its id>` beside starting the child, and hands the turns to the transcript before the first live event.
- **`ViewModels/Transcript.swift`:** the earlier turns folded in as rows, and the notice kept only for a binary that has no read.
- **`Views/SessionWindow`:** a thin divider, "earlier in this conversation", above the turns where the live session takes over.
- **`presentation/shell/ViewModels/Parity.swift`:** `chats show` classified.
- **Fixtures:** `chats show` of a saved conversation recorded from the binary that ships the CLI's M27, over a sandbox home (the home written `/Users/someone/.apogee`), with a plain turn, a tool call, an attachment and a compaction.

**Decisions made** (dated):
- 2026-10-22 — **What the CLI's M27 gives this item** (recorded when M27 shipped).
  - `chats show <id> --output-format json` is `{object:"chat", id, name, title, backend, model, attachments:[{name,files,bytes}], turns:[{turn, user:{text,attachments}, answer:{text,tools:[{name,summary,result_bytes}]}, answered, compacted?:{summary}}]}`.
  - Its last listed `turn` is one less than the `next_turn` a resumed session announces.
  - A compaction is an entry with a blank `user`, the answer it kept and its `summary`.
  - `answered: false` marks a turn that was cancelled or failed.
  - `answer.text` joins all the turn's text, and tools are `{name, summary, result_bytes}` with no results.
  - A chat-level `attachments` lists every attachment, those that rode no message included.
- 2026-10-22 — **Opened from the user's request**, beside its CLI sibling M27, which it is gated on.

**Open calls:**
- [default: the earlier turns are drawn exactly as live ones, under the divider. Tools show as their activity lines, folded, and a compaction as a notice where it happened.]
- [default: every earlier turn is drawn. A very long conversation scrolls, and nothing is paged away.]

**Guardrail(s).**
- **The transcript reducer over the recorded document:** the earlier turns as rows, then a live turn numbered next, with no notice.
- **A binary without `chats show`:** the notice as today, over 1b's recordings.
- **A conversation the app paused and resumed itself** keeps its own rows and reads nothing again.

**Acceptance criteria:**
- [ ] Reopening a saved conversation shows its earlier turns, then carries on from them.
- [ ] Against a binary before the CLI's M27, the notice reads as today.
- [ ] Previews, `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** GUI Maintenance, claimable by name. Its gate, the CLI's [M27](../../../cli/assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths), shipped 2026-10-22. A saved chat's transcript shown in the Sessions inspector without resuming it is a later want the same read makes cheap, unasked.
