# `chats show` reads a saved conversation's turns

**What / why.** The user, 2026-10-22, reopening a saved chat in the macOS app: "When we reload a chat, I want the previous turns to show." The app showed only:

`resumed: 5 earlier turns of this conversation aren't shown here -- apogee keeps them, and the model still has them`

The app says that because the CLI gives a driver nothing to show (`lib/src/darwin/source/presentation/session/ViewModels/Transcript.swift:153–159`: "the CLI hands no read of its saved turns to a driver"). Read in the source and the binary's help (2026-10-22):
- **No verb reads the turns.** `chats` has `list`, `info`, `title`, `compact`, `recall` and `delete`. `machine-mode.md` documents a machine read for `chats list` alone.
- **The turns are on disk.** A saved conversation keeps its whole history (`logger::Session::messages`, `source/data/logger/session.h:137–162`) in the neutral IR: user and assistant messages, the assistant's tool calls, tool results, and a compaction's summary. By construction it holds no thinking and no injected context (the cleanliness test).
- **The terminal doesn't replay them either.** `chat --resume` loads them (`logger::load`, `source/presentation/cli/chat_session.cpp:837–860`) and prints only its warnings.

**The fix.** `apogee chats show <id|name>` prints a saved conversation's turns:
- **The text view:** each turn as the chat drew it, plainly, with the user's words, the answer, and the tools called.
- **`--output-format json`:** the same turns typed, for the app's GUI M7 and any driver.

**Core constraint(s).**
- **One reader.** The saved session is read through `logger::load` (or its read-only twin), never a second parser of the session file (ADR 0002). An older session file reads as `chat --resume` reads it (ADR 0006).
- **Nothing the history doesn't hold:**
  - no thinking and no injected context, as the cleanliness property guarantees;
  - an attachment named, never its bytes, so no image base64 is ever printed.
- **Turns numbered as the machine session numbers them.** A resumed session's next turn follows the last one shown here.
- **The JSON is a new document,** added to `machine-mode.md` and `cli.machine_schema_conformance`. Every existing document is unchanged (ADR 0006).
- **Completion** of the conversation's id or name, from the live store (ADR 0007).
- **Its place** in the TUI's parity table (`cli/tui_parity.cpp`, ADR 0010), and the GUI's ParityTable classification, which belongs to GUI M7.

**Seam + files.**
- **`source/presentation/cli/chat_history.h/.cpp`:** the `chats show` verb, beside `info`, `title` and `delete`.
- **`source/data/logger/session.h/.cpp`:** a read of a saved session's turns, grouped as the session numbers them. The read is shared with `transcript_text` where it can be.
- **`lib/documentation/cli/reference/machine-mode.md`:** the document `{id, name, title, backend, model, turns: [{turn, user: {text, attachments}, answer: {text, tools: [{name, summary}]}, compacted?}]}`.
- **Tests:**
  - `tests/presentation/cli/chat_history_test.cpp`: the text view and the JSON;
  - `tests/data/logger/`: the grouping;
  - over a saved-session fixture with a plain turn, a turn with a tool call and its result, an attachment and a compaction.

**Reference.** 52a (`chats list`'s typed rows, `backend` and `model`), 52d (a private chat is never written, so there is nothing to show), the logger's cleanliness test, and `transcript_text` (`session.h:296–298`), the existing text reading of a history.

**Decisions made** (dated):
- 2026-10-22 — **Opened from the user's request** ("When we reload a chat, I want the previous turns to show"), with the app's notice as the evidence.
- 2026-10-22 — **A read, not a protocol change.** The turns come from their own read, not from the machine session's opening event. The session protocol stays as it is, an older app is untouched, and the same read can show a saved chat without resuming it.

**Open calls:**
- [default: a turn is a user message and everything after it up to the next user message. Tool calls are listed by name with a one-line summary of their arguments, and tool results are not printed, only their size said. A compaction stands as its own entry with the summary text, where it happened.]
- [default: `chats show` prints the whole conversation. A driver that wants fewer turns reads the end of the array.]
- [default: `chat --resume` in the terminal stays as it is. Replaying there is a separate ask.]

**Guardrail(s).**
- **The saved-session fixture,** read through the real loader, gives the expected turns:
  - text and JSON golden, the turn numbers matching what a resumed machine session announces;
  - an attachment named, its bytes absent;
  - a compaction where it happened;
  - no thinking or injected context.
- **An unknown id or name** is refused as `chats info` refuses it.

**Acceptance criteria:**
- [ ] `apogee chats show <id>` prints the conversation's turns, and `--output-format json` gives the same turns typed, documented in `machine-mode.md`.
- [ ] A resumed session's announced next turn follows the last turn `chats show` lists.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** Maintenance, claimable by name, no gate. The app's half, showing them on resume, is the GUI's [M7](../../../gui/backlog/maintenance/resumed-chat-shows-its-turns.md), gated on this.
