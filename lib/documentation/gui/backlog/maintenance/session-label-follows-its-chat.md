# A session's window says the chat and the model it is running

**What / why.** The user, 2026-10-22, with a screenshot: "I am running a local model and it is not displaying it correctly. It just says claude, instead of the local model name." Their window drew:
- **The header:** `claude`.
- **The inspector:** Name `20261022-214642-9a66`, Turns 0, Model `claude`, Backend `claude`, Runs subscription.
- **The transcript:** a Gemma conversation's 8th turn streaming.

The saved records (2026-10-22) explain it:
- `9a66` is `claude` with **0 turns**.
- The conversation is `20261022-214648-d3e2`, on `Gemma4-26B-Instruct-mixed_4_8`, with 8 turns.

Two defects in the app, measured, not guessed (the source read; the installed `4defc35e` driven over a sandbox with two scripted mocks):

1. **The label follows only the opening event.**
   - `Transcript.model` is set from the `session` event and nowhere else (`lib/src/darwin/source/presentation/session/ViewModels/Transcript.swift:224`).
   - The header draws it (`session/Views/SessionWindow.swift:93`), and so does the inspector's Model (`sessions/ViewModels/SessionLibraryViewModel.swift:591`).
   - The settings sheet's model change goes out as a `set` line on a 52d binary (`SessionViewModel.apply`, `:343–347`), which switches in the same session. The probe: `notice: switched to Gemma4-26B-Instruct -- from the next turn`.
   - Each answer then reports `result.model = 'Gemma4-26B-Instruct'`, which the transcript decodes and ignores (`Transcript.swift:290`). So the header keeps the launch default for the whole session.
2. **A new session is pinned to the wrong chat.**
   - The library names a new session by "the one id that appeared since it opened" (`identifyNewSession`, `SessionLibraryViewModel.swift:270–283`).
   - `switchTo` closes the session it leaves, then snapshots "what was listed" from its **cached** list (`:390–404`). That cache was read before the closed session's save landed.
   - The CLI saves even a chat with no turn when its stdin closes (the CLI's M28), so leaving a new, unsent session for another new one makes the left one's empty chat "appear". The very next refresh (`:394`) pins the new session to it.
   - That is what happened: `9a66`, opened and left unsent, became the name of the session that then ran as `d3e2`.

**Blast radius.**
- **Every model switched from the settings sheet** leaves the header and inspector on the launch model.
- **Every new session opened from an unsent new one** takes the empty chat's identity. The inspector shows it, and the sidebar selects it. The window's **Rename** and **Delete** act on it, not on the conversation shown. A relaunch reopens it (`preferences.openChat`), empty, with M7's read finding nothing.

**Core constraint(s).**
- **The binary's word decides:**
  - The label is the model the CLI says answered (`result.model`), else the session's opening `model`.
  - The chat is the one the CLI names (the `session` event's `chat_id`, the CLI's M28) where the binary says it, else the app's guess, made right.
- **An older binary still works.** Without `chat_id`, the guess takes its snapshot from a fresh `chats list` read after the left session has closed, never the cache. It skips a chat with no turns, which can't be a session that has run a turn.
- **ADR 0004:** the new states have previews over recorded streams.

**Seam + files.**
- **`presentation/session/ViewModels/Transcript.swift`:** `.result`'s `model` moves `model` when present. A switch said by a `set` notice is not guessed from the notice's words; the next answer says it.
- **`presentation/sessions/ViewModels/SessionLibraryViewModel.swift`:**
  - a session that names its chat (`chat_id`) is identified by it at once;
  - otherwise `listedBeforeNew` comes from a fresh read after `close`, and `identifyNewSession` passes over chats with 0 turns.
- **`data/protocol/ProtocolEvent.swift`:** the `session` event's optional `chatID`, an unknown field tolerated as today.
- **Tests:**
  - the transcript over a recorded stream (session `claude`, the `set` notice, a result naming `Gemma4-26B-Instruct`), the label then the local model's;
  - the library over a scripted sequence (a new session left unsent, its empty chat appearing, another new session), the second identified correctly by `chat_id`, and by the fallback without it;
  - **Rename** and **Delete** acting on the right chat.
- **Fixtures:** the probe's stream and `chats list`, recorded from the binary that ships the CLI's M28 and from one before it.

**Decisions made** (dated):
- 2026-10-24 — **What the CLI's M28 gives this item** (recorded when M28 shipped): the `session` event carries `chat_id` (a new chat's id, a resumed chat's own, none for a `--private` session that writes nothing; `task run` names its chat too), and a new chat with no answered turn is never written, so an unsent session leaves no empty chat behind for the guess to take. Older binaries still write empty chats, which the fallback's 0-turn skip covers.
- 2026-10-22 — **Diagnosed from the user's run.** The local model was answering; the window was labelled with an abandoned empty chat, and its model label never followed the switch. The CLI's sibling M28 names the chat in the session event and stops saving empty chats.
- 2026-10-22 — **The label follows `result.model`, the CLI's word for the model that answered,** not the settings sheet's choice. The sheet's choice takes effect only "from the next turn", and only the answer proves it ran.

**Open calls:**
- [default: between a switch and the next answer, the header keeps the model that last answered, and the CLI's own notice ("switched to … -- from the next turn") stands in the transcript.]
- [default: the inspector's Backend reads the session's own chat row, refreshed after each turn as today. Once the chat is the right one, it says the backend the CLI saved.]

**Guardrail(s).** The user's case is the fixture.
- **The `set` switch:** the recorded stream's label reads `Gemma4-26B-Instruct` after the answer, never `claude`.
- **The unsent session:** a new session opened from an unsent new one is identified as its own chat, by `chat_id`, and, against an older binary, by the fresh snapshot that passes over 0-turn chats.
- **Rename and Delete** from the window name the conversation shown.

**Acceptance criteria:**
- [ ] After switching to a local model in a session's settings, the header and inspector say the local model once it answers.
- [ ] A session opened from an unsent new one is labelled with its own chat, and Rename, Delete and a relaunch act on it.
- [ ] Previews, `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** GUI Maintenance, claimable by name, no gate. It reads the CLI's [M28](../../../cli/assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths) `chat_id` (shipped 2026-10-24) where the binary has it, and is whole without it. Existing empty chats (`9a66`, `7e1e`) stay listed until the user deletes them.
