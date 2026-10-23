# A driven session names its chat, and an empty one is never saved

**What / why.** The user's run in the macOS app, 2026-10-22: "I am running a local model and it is not displaying it correctly. It just says claude, instead of the local model name." The window was labelled with the wrong chat:
- **The window's chat:** `20261022-214642-9a66`, recorded on `claude` with **0 turns**.
- **The conversation in it:** `20261022-214648-d3e2`, recorded on `Gemma4-26B-Instruct-mixed_4_8` with 8 turns, from "Hello, how are you today?" to "Can apogee create videos?".

The app's half of the cause is the GUI's [M10](../../../gui/backlog/maintenance/session-label-follows-its-chat.md). The CLI's half is two facts, measured, not guessed. The installed `0.1.9 (4defc35e)` was driven over a sandbox `APOGEE_HOME` with two scripted mock backends, `claude` the default (2026-10-22):

- **The session never says which chat it is.** The opening event is `session.model = 'claude'`, with keys `capabilities, model, next_turn, protocol_version, type` (`source/presentation/machine/protocol.cpp:30–50`). The chat id is minted at start (`logger::new_chat_id()`, `source/presentation/cli/chat_session.cpp:862`) and never told to the driver. So the app guesses: the one id that appeared in `chats list` since the session opened.
- **An empty chat is saved.** A driver that closes stdin before any turn leaves one behind: `appeared: ('…-ac9f', 'claude', 0)`. The close saves unconditionally ("stdin closed: the driver is done", `logger::save(session)`, `chat_session.cpp:1682–1684`). In the user's store that left `9a66` and `20261022-173852-7e1e`, both `claude` with 0 turns, as rows in the app's sidebar. `9a66` is the id the app's guess then took for the next session's.

**The fix.**
- **The `session` event names its chat:** `chat_id`, the id `chats show`, `--resume` and `chats list` use. It is absent for a `--private` session that writes nothing (52d). It is added to the event, never reshaping it (ADR 0006).
- **A chat with no turn is never written.** Closing a session that was never sent a turn writes nothing, so no empty row appears in `chats list`. A chat that has a turn saves as today.

**Core constraint(s).**
- **The protocol grows additively** (ADR 0006): `chat_id` joins the `session` event, documented in `machine-mode.md` and the schema, and an older driver ignores it.
- **A resumed session names the chat it resumed,** the id it was given.
- **Nothing that was saved changes.** Existing empty chats stay listed until deleted. This item stops new ones; it does not sweep.
- **`set`'s behaviour is unchanged:** a model switch stays in the same chat (`switched to … -- from the next turn`), and each `result` already says the model that answered (`protocol.cpp:108–117`). The probe showed `result.model = 'Gemma4-26B-Instruct'` after the switch.

**Seam + files.**
- **`source/presentation/machine/protocol.cpp`, `json_reporter.cpp`:** the `session` event's `chat_id`.
- **`source/presentation/cli/chat_session.cpp`:** the id handed to the reporter at start, and the close's save skipped for a session with no turn. The same rule applies to any other unconditional save of an untouched session.
- **`lib/documentation/cli/reference/machine-mode.md`, `machine-schema.json`:** the field.
- **Tests:**
  - `cli.machine_mode`: the event's `chat_id` equal to the chat `chats list` then lists, a resumed session's equal to the resumed id, and a private one's absent;
  - a session closed before any turn leaving `chats list` unchanged;
  - `cli.machine_schema_conformance`.

**Reference.** 52d (private chats, the `set` line), M27 (`chats show`, and the turn numbering a resumed session announces), and the user's run's records above.

**Decisions made** (dated):
- 2026-10-22 — **Diagnosed from the user's run** (the user's bug report, with a screenshot of session `9a66` labelled `claude` while Gemma answered). The CLI's half is the missing id and the empty save. The app's half (its guess, its stale snapshot, and the label ignoring `result.model`) is the GUI's M10.

**Open calls:**
- [default: the field is `chat_id`, matching `chats list`'s `id` value and the CLI's own `chat_id` name.]
- [default: a session closed with no turn writes nothing at all, even its title or settings. There is nothing to resume in it.]

**Guardrail(s).** The probe is the fixture: two scripted mocks, `claude` the default.
- **The opening event** carries `chat_id`, and after a turn `chats list` lists exactly that id.
- **A session closed before any turn** adds no row.
- **A resumed session** names the resumed id.
- **A private session** names none.

**Acceptance criteria:**
- [ ] A driven `chat` says its chat id in its `session` event, documented and held by the schema test.
- [ ] Opening and closing a session without a turn leaves `chats list` as it was.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** Maintenance, claimable by name, no gate. The app reads the id in the GUI's [M10](../../../gui/backlog/maintenance/session-label-follows-its-chat.md), which falls back to its own fix for an older binary.
