# ACP sessions saved and resumed

**What / why.** Xcode's ACP client is built to list and reopen an agent's conversations. Apogee, as Xcode's agent ([56a](../../assistant/MILESTONES.md#milestone-as--apogee-in-xcode), shipped 2026-11-01), would keep nothing a host can reopen. Measured, not guessed (2026-10-31, `strings` on the installed Xcode 27.1, build 27A9275, in that day's Xcode research): the ACP client in `IDEIntelligenceACP.framework` has message types `LoadSessionRequest`, `ListSessionsRequest`, `ResumeSessionRequest` and `CloseSessionRequest` beside `NewSessionRequest` and `PromptRequest`, and none for fork or delete. Type names show what Xcode can send, not what it does send, or when (the research's verifier); the live pass below records that. The protocol says what each method asks of the agent (agentclientprotocol.com, the session-setup, session-list and initialization pages, read 2026-10-31):

- `session/load` exists only when the agent's `initialize` answer sets `agentCapabilities.loadSession`: "If `loadSession` is `false` or not present, the Agent does not support loading sessions." It takes `sessionId`, `cwd` and `mcpServers`. The agent MUST replay the entire conversation as `session/update` notifications (the examples show `user_message_chunk` and `agent_message_chunk`), and answer only after the replay.
- `session/list`, `session/resume` and `session/close` are each advertised under `agentCapabilities.sessionCapabilities` (`list`, `resume`, `close`, each an empty object; the schema page). A client MUST NOT call one its agent did not advertise. A list entry needs `sessionId` and an absolute `cwd`, and may carry `title` and `updatedAt` (ISO 8601). A list request may filter by `cwd` and page by `cursor`. `session/resume` restores a session and MUST NOT replay it. `session/close` cancels the session's work, frees it, and is idempotent.
- The agent mints the `sessionId`. The host SHOULD send `clientInfo` (`name` and `version`, `title` optional) on `initialize`.

Apogee speaks no ACP today (`git grep` for `agentclientprotocol`, `session/load` and `loadSession` over `lib/src` finds nothing). That is wall **W5**: an ACP session leaves nothing behind. 56a opens ACP sessions through the one session core. That core already rewrites a chat's file after every completed turn (`lib/src/cli/source/data/logger/session.h:16`). It writes a new chat only once a turn is answered (M28: `session.h:191-195`, `session.cpp:238-240`). But the record says nothing about who began a chat or in which folder, so no host's list can be answered. And an agent that does not advertise `loadSession` is one Xcode cannot reopen.

This item makes an ACP session an ordinary Apogee chat that a host can find, reopen and continue:
- It is saved in the one store, and its record gains `origin` (the host's `clientInfo.name`) and `cwd`.
- `chats list` and `chats info` say where it came from.
- The agent answers `session/load`, replaying the saved turns in the order and under the numbers `chats show` gives them (M27). It also answers `session/list`, `session/resume` and `session/close`.
- `initialize` advertises these methods only once they work.

It removes **W5**.

**Core constraint(s).**
- **One session store.** An ACP chat is a chat. `logger::save` writes it, `logger::list_sessions` lists it, `logger::load` reads it and `chats delete` removes it. It gets no store, file or index of its own ([SPEC.md](../../assistant/SPEC.md):70, "One source of truth per concern"). The same chat resumes in a terminal (`chat --resume <id>`), in the shell and in the macOS app's Sessions library, and `chats show` reads it (`SPEC.md:65`, "Parity is the product").
- **One opening** ([ADR 0011](../../adrs/one-session-opening.md)). `session/load` and `session/resume` reach the session core the way `execute --resume` (and `chat --resume`, the same `bind_session_flags` table) does: through `run_session`'s own resume path (`lib/src/cli/source/presentation/cli/chat_session.cpp:915-938`), with its notes and its fallback for a missing backend. They come in through 56a's door, which speaks the command's flag table. The ACP module never composes a second load.
- **M28 holds.** A session closed before any turn is answered writes nothing: no file, no list entry, no title, no attachment index.
- **One count** (55b). The replay walks `logger::saved_turns`, the same entries `chats show` prints. A resumed session's next turn is `logger::counted_turns + 1`, the number `chat_session.cpp:1508` gives a resumed machine session. ACP has no numbering of its own. ACP also shows no live turn numbers, so 55b's recorded drift after a failed live turn (MILESTONES, 55b's "Not done") cannot reach an ACP host. Numbers exist only in `chats show`.
- **Additive** ([ADR 0006](../../adrs/backwards-compatibility.md)). `origin` and `cwd` are optional fields, written only when set. Every chat begun anywhere else serializes byte for byte as today, and the schema stays at 2 (`kCurrentSchemaVersion`, `session.h:34`). `task` is the precedent (27h, `session.h:222-226`): empty "writes nothing, so it saves exactly as before". An older file reads with both fields empty. The JSON documents only gain keys. The app reads a `chats list` row by named key (`lib/src/darwin/source/business/reads/Chats.swift:28-38`), so it skips the new one. A binary older than this item that resumes such a chat rewrites it without the two fields, because `serialize` writes only what it knows. The chat then reads as one begun elsewhere. machine-mode.md says so; nothing prevents it.
- **Pipes only.** `apogee acp` is a child process on stdio, and nothing here listens (`SPEC.md:66`; CLAUDE.md "⚠ Interactive turns never open a listening socket", held by `cli.no_listen_symbols` and `cli.complete_opens_no_listening_socket`). HTTP's session plane (`GET /v1/sessions`, `presentation/httpserver/handler.cpp:899-908`) is not a door to any of this.
- **Nothing hidden is replayed.** The record never holds thinking or injected context (`session.h:159-160`), so a replay cannot send either.

**Seam + files.**
- `lib/src/cli/source/data/logger/session.h/.cpp`: `Session` gains `origin` and `cwd`, both strings and empty for a chat no host began. `serialize` writes each only when set, and `deserialize` reads each the way it reads `task`. No new function is needed. `list_sessions` (newest first: `session.cpp:698`, sorted at `:726-733`), `load`, `saved_turns` (`session.h:421`) and `counted_turns` (`session.h:314`) serve all four methods.
- 56a's ACP front end (`presentation/acp/`). Under 56a's translator shape each session is a captured `execute` child, so the session core that saves the chat runs in that child:
  - `initialize`: answers `agentCapabilities.loadSession: true` and `agentCapabilities.sessionCapabilities` with `list: {}`, `resume: {}` and `close: {}`, and keeps the host's `clientInfo.name` for the process.
  - `session/new`: the child's line drops the `--private` 56a runs it under, and gains `origin` through a hidden `execute` option (`group("")`, never completed). `cwd` is the folder 56a starts the child in. M28 writes both with the first answered turn.
  - `session/load`: the resume door (the child started with `--resume <id>`), then the replay, then the answer.
  - `session/resume`: the resume door, with no replay.
  - Both hand their request's `mcpServers` to [56b](../../assistant/MILESTONES.md#milestone-as--apogee-in-xcode)'s mapping, as `session/new` does (56b shipped 2026-11-01: `acp::host_servers`, then `Session::open`'s handover to the child). A host's servers are never saved with the chat.
  - `session/list`: `list_sessions`, filtered.
  - `session/close`: cancels the session's turn as `session/cancel` does and releases the session. Its file is already current.
- `lib/src/cli/source/presentation/cli/chat_history.cpp`: `session_row_view` (`:103-109`) gains `origin`, so `chats list` (`:352`) carries it, and so does `chats info`, whose `session_info_view` (`:111-131`) is built on the row. `session_info_view` also gains `cwd`. The text forms are in the open calls.
- `lib/documentation/cli/reference/machine-mode.md`: the `chats list` and `chats info` entries (`:1451`, `:1455`) document both fields. 56a's `lib/documentation/cli/reference/acp.md` gains the four methods.
- Tests: `lib/src/cli/tests/data/logger/session_test.cpp` (round-trip; absent fields write nothing; the M28 case at `:244` extended to an ACP-begun chat), `lib/src/cli/tests/presentation/cli/chat_history_test.cpp` (the row and `chat.info` pins), and 56a's scripted ACP client, which grows the cases under Guardrails.

This item adds no verb and no visible flag (the hidden `origin` option is plumbing, never completed). [ADR 0007](../../adrs/tab-completion.md)'s completion gets nothing new, and neither does [ADR 0010](../../adrs/the-shell-is-a-mode.md)'s parity table: `chats` keeps its row (`presentation/cli/tui_parity.cpp:23`).

**Reference.**
- In house:
  - M27: `chats show` and `saved_turns`, which read a resumed session's earlier turns and number them the way the session numbers on.
  - M28: the `session` event's `chat_id`, and "A chat is written once a turn is answered" ([machine-mode.md](../../reference/machine-mode.md), "Which chat: `chat_id`"). M27 and M28 are both in [Milestone AO](../../assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths).
  - 55b: the one turn count, and a store that is edited from outside too.
  - 52d: `chat --private`, which is never written.
  - 27h: `task`, the precedent for an additive field.
  - 26j: a resumed chat starts from its backend's saved state (`harness.resume_conversation`, `chat_session.cpp:1066`). An ACP resume gets this for free.
- External: the ACP pages above. Two deliberate divergences:
  - The spec sets no order for `session/list`. Apogee answers newest first, as `chats list` does.
  - The spec's replay examples show only message chunks. Apogee also replays a turn's tool calls, as finished calls without their results, because `chats show` lists the calls and never their results.

**Decisions made** (dated):
- 2026-10-31 — Specced from that day's Xcode research (the user's call: track 56, "Apogee in Xcode", in both the CLI's v0.2.1 queue and the GUI's). This document claims wall W5 alone.
- 2026-10-31 — `apogee acp` is the CLI's fifth core surface, with the same session core and agent loop as `execute` (the user's call; [SPEC.md](../../assistant/SPEC.md) line 32). So an ACP session is an Apogee chat in the one store, not a session in a store of the protocol's own.
- 2026-10-31 — Subscription backends are offered to Xcode through `apogee acp` (the user's call). A chat begun on `claude-cli`, `codex-cli`, `gemini-cli` or `ollama-cli` saves and resumes as `chat --resume` does on it. Apogee's transcript is the record, and the vendor's session id is only a resume shortcut (`session.h:164-167`): resumed through the vendor's own `--resume`, never by reading its files (SPEC, "A vendor CLI is spawned, never opened").
- 2026-10-31 — Marking chats begun in Xcode in the macOS app's Sessions library (the proposed GUI 5b) is not queued (the user's call). Such chats appear there like any other chat, and the `origin` field in `chats list` is ready for when marking is wanted. The GUI queue holds 5a only ([`xcode-in-settings.md`](../../../gui/backlog/v0.2.1/xcode-in-settings.md)).
- 2026-10-31 — This item advertises the four capabilities; 56a never does. If `initialize` promised `loadSession` before load worked, Xcode would reopen sessions into errors.

**Open calls:**
- [default: an ACP session is saved like any chat. Recall and titles work as `run_session` gives them to the mode 56a opens: the background title (`BackgroundTitle`, `chat_session.cpp:492`) and recall (`:1072-1073`). A private mode is not offered. Recommended: ACP has no "don't save" field, and `chats delete` removes the chat. If a private mode is wanted later, `apogee acp --private` in Xcode's Arguments field is its own item.]
- [default: `session/list` answers the chats an ACP host began (`origin` set), newest first, filtered to an exact `cwd` match when the request names one. Each entry carries: `sessionId`, the chat id; `cwd`; `title`, the name `chats list` shows (left out while that is the bare id); and `updatedAt`, the chat's `updated_at`, which is already ISO 8601 UTC (`session.cpp:22-34`). One page, with no `nextCursor`. Recommended: a chat begun in a terminal has no recorded folder, and the spec requires one for every entry.]
- [default: `origin` is the host's `clientInfo.name` as sent. The live pass records what Xcode sends. It is `acp` when the host sent no `clientInfo`. It is set when the chat begins and never rewritten by a later resume from anywhere, and the host's version is not kept. Recommended: `origin` answers "who began it", the way `started_at` answers "when".]
- [default: `cwd` is the `cwd` that `session/new` names. It is saved for ACP-begun chats only and shown in `chats info`. A load or resume runs in the `cwd` that request names and leaves the saved one as it is. Recommended: a host filters by the folder a chat began in.]
- [default: the `sessionId` is the chat's `chat_id`, the id that `chats list`, `chats show` and `--resume` take, so one id names the conversation everywhere. Recommended: no mapping between two ids to keep. If 56a's `session/new` already does this, the call is consumed.]
- [default: `session/load` and `session/resume` take any saved chat's id, since the store is one. `session/list` offers only ACP-begun chats. An id that no chat answers to is refused with an error naming it, as `chats show` refuses one; the JSON-RPC code is settled at build against the spec's error page (the session-setup page names only `-32002`, in `session/close`, linking "Unknown Sessions"). Closing an unknown or already-closed session answers success, as the spec asks. Recommended: an id from `chats list` works wherever it is pasted.]
- [default: the replay mapping. Each `saved_turns` entry is sent in order:
  - the user's words as one `user_message_chunk`, with attachments named in it as `chats show`'s text names them, never their bytes;
  - each answer part as an `agent_message_chunk`;
  - each tool call as a `tool_call` update with `status` `completed`, titled by its name and its one-line arguments (`chats show`'s `summary`), with no result;
  - a compaction the way 56a delivers one live. The schema's `compaction_update` may be sent only to a client that advertised the `session.compaction` capability (schema page, read 2026-10-31); otherwise an `agent_message_chunk` carries `chats show`'s "history compacted" line and the summary.
  Ids come from the entry's position in `saved_turns`, the index of `chats show --output-format json`'s `turns` array: `messageId` `entry-3-user`, `entry-3-agent-1`, …, and `toolCallId` `entry-3-tool-1`. Not the entry's `turn`: an unanswered turn and a compaction carry the count before them, so two entries can share a `turn`, and the spec asks each `messageId` to be unique. A replay and `chats show`'s JSON then line up entry by entry. A resume's notes (a backend gone, a field dropped) are said after the replay, the way 56a says a notice. They are never replayed as history and never dropped. Recommended: the host shows what `chats show` prints.]
- [default: the text forms. `chats list`'s text row ends in `  (from <origin>)` only when `origin` is set. `chats info`'s text gains `origin:` and `cwd:` lines only when they are set. Unchanged: the shell's Chats view (`presentation/cli/tui_workbench.cpp:104-115`), `chats show`'s document and HTTP's `/v1/sessions`. Recommended: every other chat's row stays byte for byte as today, and the shell's Chats view shows neither backend nor model either.]
- [default: no lock across processes. A chat open in Xcode and resumed in a terminal at the same time goes to the last writer, turn by turn, as two terminals do today. 55b's outside rename and delete are honored. `session/delete` is not advertised: Xcode 27.1's message types include none, and `chats delete` is the door. Recommended: the store behaves as it does today.]

**Guardrail(s).**
- Logger: `origin` and `cwd` round-trip. A chat with neither serializes byte-identical to today's golden. A file without them loads with both empty and no warning. An ACP-begun chat closed before any answered turn writes nothing (the M28 case).
- `chats list` and `chats info` JSON carry `origin` (`""` when unset), and `chats info` carries `cwd`. The text pins for a chat without an origin are unchanged.
- The scripted client. It is 56a's executable-tier ACP test, run with a temporary `APOGEE_HOME` and the `mock` backend, set up as `lib/src/cli/tests/scripts/sh/machine_mode_e2e.sh` does (`:22`, `:28-29`), and `clientInfo.name` `acp-test`:
  - `initialize` advertises `loadSession`, and `list`, `resume` and `close`.
  - new → prompt → prompt → close → list: the chat is listed with its `cwd`, title and `updatedAt`, and `chats list --output-format json` shows `origin` `acp-test`.
  - load: exactly the two turns are replayed, in order, before the `session/load` answer, with texts equal to `chats show --output-format json`'s turn by turn. A third prompt then becomes `chats show`'s turn 3.
  - resume: no `session/update` arrives before its answer, and the next prompt continues the numbering (`chats show` lists it after the earlier turns).
  - load replays a chat with a cancelled turn and a compaction (a fixture file) with every `messageId` distinct.
  - An empty session (new → close) saves nothing: no file, and no entry in `session/list` or `chats list`.
  - A `cwd` filter leaves out a chat begun in another folder. Closing twice succeeds. Loading an unknown id is refused, and the refusal names the id.
- The standing pair, `cli.no_listen_symbols` and `cli.complete_opens_no_listening_socket`, is unchanged. `cli.machine_mode`, the app's contract, stays green.

**Acceptance criteria:**
- [ ] The scripted client's cases pass. `chats list` and `chats info` carry `origin` (and `chats info` carries `cwd`) in text and JSON, and machine-mode.md documents both. The full suite is green.
- [ ] Live in Xcode, the user's run on the dev channel ([CLAUDE.md → Development runs on the dev channel](../../assistant/CLAUDE.md#development-runs-on-the-dev-channel)). `apogee-dev` is registered in "Add an Agent" by its absolute path, with the argument `acp` (the fields [56d](xcode-setup-from-apogee.md) prints, once it has landed). A conversation closed and reopened from Xcode's history continues: the next answer knows the earlier turns, and `apogee-dev chats show <id>` numbers the new turn after them. `apogee-dev chats list` lists the chat with its origin. The record notes which of load, list and resume Xcode called, the `clientInfo` name it sent, and the filesystem security layer's setting, as [56a](../../assistant/MILESTONES.md#milestone-as--apogee-in-xcode)'s pass recorded it.
- [ ] The same chat resumes in a terminal with `apogee-dev chat --resume <id>`.

**Scope note.** Status: earmarked for v0.2.1 (track 56, "Apogee in Xcode"). Gate: none, 🟢 -- [56a](../../assistant/MILESTONES.md#milestone-as--apogee-in-xcode), which owns the protocol, the door and the scripted client (`tests/scripts/py/acp_host_check.py`), shipped 2026-11-01. This item removes wall **W5**. Out of scope:
- Xcode's tools inside the session ([56b](../../assistant/MILESTONES.md#milestone-as--apogee-in-xcode), shipped 2026-11-01).
- Printing Xcode's setup and stating the signing limits ([56d](xcode-setup-from-apogee.md)).
- `session/fork` (Xcode 27.1 has none) and `session/delete`.
- A private ACP mode, and HTTP's session plane.
- Marking these chats in the app (the proposed 5b, not queued).

GUI sibling: [5a](../../../gui/backlog/v0.2.1/xcode-in-settings.md), Xcode in the app's Settings. It does not touch these chats; the Sessions library lists them like any other.
