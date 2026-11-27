# CLI-60c · An app's own tools, on every door

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🔒 Blocked (60a, 59b, 59f) |
| **Fix version** | v0.2.4 |
| **Component** | Client tools: the agent loop's hand-back, the machine protocol (`machine/`) and the OpenAI door (`httpserver/`) |
| **Labels** | `client-tools` `apps-as-clients` `openai-compat` `machine-mode` |
| **Epic** | Track 60 · one tool definition and device tools |
| **Blocked by** | [60a](one-tool-definition.md), [59b](../v0.2.3/machine-mode-over-the-network.md), [59f](../v0.2.3/paired-apps-with-scopes.md) |
| **Blocks** | [60d](building-an-app-on-apogee.md) |
| **Reporter** | The apps-as-clients spike (2026-11-14), from the user's goal that other apps use Apogee as their AI back end |
| **Created** | 2026-11-14 |

---

## Summary

A client app cannot offer Apogee tools it implements itself: the OpenAI door refuses `tools`, Apogee's protocol has no line for them, and a model that calls one is answered by the server's own registry. This item lets an app declare its own tools in [60a](one-tool-definition.md)'s one definition on both doors (a `tools` line on stdio and over [59b](../v0.2.3/machine-mode-over-the-network.md), and standard OpenAI function-calling), held to the app's [59f](../v0.2.3/paired-apps-with-scopes.md) scope, and **never run by the server**.

## Description

### User story

**As** an app developer using Apogee as my app's AI back end (Taylor'd Portfolio, the user's job hunter, first),
**I want** to declare tools my app implements (search its own job store, read a posting it saved, look up the person's profile) and answer the model's calls to them,
**so that** a model working for my app can ask my app to do the things only it can do, through the full harness and not just model calls, on whichever door I use.

### Background

The user's goal, 2026-11-14: other apps use Apogee as their AI back end, Taylor'd Portfolio (the user's job hunter) first, and they use the full harness, not just model calls. An app has things only it can do: search its own job store, read a posting it saved, look up the person's profile. Today a model working for that app cannot ask the app to do any of them.

This item lets a client app declare **tools it implements itself**, in [60a](one-tool-definition.md)'s one definition, on both of Apogee's doors. On Apogee's own protocol (a declare line on stdio, and on [59b](../v0.2.3/machine-mode-over-the-network.md)'s paired connection), the agent loop calls them by name and the app answers each call. On the OpenAI-compatible door, they work the standard way: the request carries `tools`, the response carries `tool_calls`, the app sends the results back, and the turn continues. Every call is held to the app's scope ([59f](../v0.2.3/paired-apps-with-scopes.md)) and to the app's own consent. **A client tool is never run by the server.** A task ([59e](../v0.2.3/tasks-outlive-the-connection.md)) can use a connected app's tools while the app stays connected.

This is not [60b](device-tools.md): device tools are the CLI's **own** toolsets (files, shell, git), run by the CLI's own code on a desktop (`apogee __device-tools`). A client tool is the app's code, which Apogee never sees.

This item delivers both doors:
- **Apogee's protocol, on stdio and on 59b.** The app sends a `tools` line declaring its tools in 60a's definition, each marked as the client's. The session registers them under the app's own names. When the model calls one, the session sends a `tool_request` and waits; the app runs its own code and answers with a `tool_result`. A cancelled turn sends `tool_cancel`. A dropped connection fails the call with the outcome said to be unknown. These are 60b's lines, reused, not a second set.
- **The OpenAI door.** A request may carry `tools`. When the model calls one, the turn stops and the response carries the calls in OpenAI's shape, with `finish_reason: "tool_calls"`, streamed or not. The app sends them back as `tool` messages, and the turn continues.

A stateless request offers the app's tools alone, exactly as a stock client expects. The server's own tools join them only on a session, which keeps the server-run half of a paused turn. The app's scope decides whether it may offer tools at all, and which of the server's own tools sit beside them. The server never runs, approves or answers a client's call on the client's behalf.

### Evidence

**W5 — an app cannot offer its own tools.** Re-measured 2026-11-14 on the branch binary (`apogee 0.2.0 (b37b0a9f)`), a scratch `APOGEE_HOME` under the scratchpad and the `mock` backend, with `serve` on loopback `--port 0` only and stopped after. Source lines are at HEAD `231fb049`.

1. **The OpenAI door refuses `tools`.** `chat_completions` throws on any non-empty `tools` before anything else runs (`presentation/httpserver/handler.cpp:666-671`; the spike recorded `:648-652`, and later commits moved it). The probe got `400` with `client-side tools are not supported: the server runs its own tool loop and returns only the final answer (tool_mode selects it)`. The reference says the same three times: the error table ([http-api.md](../../../reference/cli/http-api.md):103), the `tool_mode` row ("**A client cannot send `tools`**", :143) and "What this server does not do" ("It does not run tools the client defines. The loop is the server's; a request with `tools` is refused rather than silently ignored", :1626-1627). A unit test pins the refusal (`tests/presentation/httpserver/handler_test.cpp:594-608`).
2. **Other spellings slip through silently.** Only `tools` is checked. The probe sent each of these with no `tools`, and each was answered `200` with `"mock response"`: the legacy `functions` list, `tool_choice: "required"` and `parallel_tool_calls: false`. So the reference's "refused rather than silently ignored" holds for one field of four. The door also has no word for a call handed back: `openai_finish_reason` folds `FinishReason::ToolCalls` into `"stop"` (`handler.cpp:177-188`).
3. **What already works: the second half of the exchange.** The IR reads OpenAI's tool messages: `ChatMessage`'s parser takes `tool_calls` and `tool_call_id` (`data/contracts/types.cpp:457-478`), and `ToolCall`'s takes arguments as a string or an object (`:428-442`). The probe sent a stateless request whose history held an assistant message calling `search_postings` (`call_1`) and a `tool` message answering it, with `tool_mode: none`. A mock scripted as `saw={{last_tool_result}} ids={{tool_call_ids}} tools={{tool_count}}` answered `saw=[3 postings] ids=call_1 tools=0`. The app's result reached the model intact. What is missing is the first half: offering the app's tool, and handing the model's call back instead of running it.
4. **A model that calls an app's tool is answered by the server.** A mock scripted to call `search_postings`, served with `--tools --all-backends`, came back `200` with the answer `after: Error: no tool named 'search_postings'. Available tools: collection_info, delete_file, …, write_note.` The server's loop dispatched the call to its own registry (`business/agent/tool.cpp:198-213`), fed the error to the model, and finished. The client never saw the call.
5. **Apogee's protocol has no line for it.** A `chat --output-format stream-json --input-format stream-json` child on the same mock announced `capabilities.accepts` as eight lines: `user`, `answer`, `attach`, `hello`, `cancel`, `detach`, `set`, `compact` (schema `2026-11-01`, `protocol_version` 1). A `{"type":"tools",…}` line sent before the `user` line was dropped as an unrecognised line ([machine-mode.md](../../../reference/cli/machine-mode.md):622-623). A `hello` with `wants: ["tools"]` "changes nothing in this release" (:651-660). The model's call to `search_postings` went `tool_call pending`, then `failed`, and the `result` carried the same "no tool named" text. 60b's lines carry the CLI's own toolsets from a device, not an app's code. [59e](../v0.2.3/tasks-outlive-the-connection.md) records that a task runs on the server's tools only ([`tasks-outlive-the-connection.md`](../v0.2.3/tasks-outlive-the-connection.md), Open calls: "a task runs on the server's tools only, never a device's").
6. **The loop cannot hand a call back.** `agentloop::run` dispatches every call the model makes and returns only a final answer. The two calls it answers without the registry, `ask_user` and `find_tools`, are intercepted before dispatch (`business/agentloop/loop.cpp:602-620`, `:622-638`). `RunResult` has no slot for calls left for someone else (`business/agentloop/loop.h:160-195`). A served turn runs the loop to its end inside the one request (`handler.cpp:550`).

## Acceptance criteria

- [ ] **Given** a scratch home with the mock scripted to call `search_postings`, **when** `POST /v1/chat/completions` carries that tool in `tools`, **then** it answers `finish_reason: "tool_calls"`, with one call carrying its `id`, `function.name` and `function.arguments`; nothing ran on the server, and there is no `apogee_tool_calls`. **When** the history is re-sent with a `tool` message for that id, **then** the answer carries the result in the text.
- [ ] **Given** the same request with `"stream": true`, **when** it is sent, **then** it streams `delta.tool_calls` and a final chunk with `finish_reason: "tool_calls"`, then `[DONE]`.
- [ ] **Given** a session with `--tools`, **when** a step calls a server tool and `search_postings`, **then** the server's runs and only `search_postings` is handed back; **when** a continuation carries only the `tool` message, **then** the turn completes, and `apogee chats show <id>` lists both calls and both results.
- [ ] **Given** the OpenAI door, **when** a request sends `functions`, `tool_choice: "required"`, a clashing name, a reserved name, or stateless `tool_mode: "all"` with `tools`, **then** each is a `400` that says why.
- [ ] **Given** no server is running, **when** a `chat` child runs in machine mode, **then** it advertises `capabilities.client_tools`; a driver's `tools` line declaring `search_postings` is followed by a `tool_request` when the model calls it, and the driver's `tool_result` reaches the answer; **when** a `cancel` arrives while it waits, **then** it sends `tool_cancel` and ends the call `failed`.
- [ ] **Given** a paired app within its scope over 59b, **when** the same exchange runs, **then** it works; **when** the app is killed mid-call, **then** the call fails with the unknown-outcome wording, and the session's next turn runs without the app's tools.
- [ ] **Given** a task started by an app, **when** the app is connected, **then** the task calls that app's tool; **when** the app disconnects, **then** the next round finds the tool `unavailable` and the task continues on the server's tools.
- [ ] [http-api.md](../../../reference/cli/http-api.md) carries the dated revision and the "Client tools" section, and machine-mode.md carries "Your own tools". The full suite is green on all five targets.
- [ ] Optional, and said when skipped: where Python's `openai` package is already installed, its standard tool-calling round trip runs unmodified against `apogee-dev serve --port 0` on loopback on the dev channel.

## Constraints

- **The anti-goal: nothing here makes standalone use need a server** (the user's call, 2026-11-07, restated 2026-11-14; GUI [SPEC.md](../../../assistant/gui/SPEC.md) → No server requirement). The stdio door is local use: an app that spawns `apogee chat` in machine mode offers its tools with no server, no pairing and no network. A session with no declared client tools, and a served request with no `tools`, behave byte for byte as they do today. `chat`, `execute`, `complete`, `task`, `acp` and the Mac app over pipes register, gate and run exactly what they run now.
- **A client tool is never run by the server.** The server-side entry for a client tool has no body that does anything. On a live connection it sends a request and waits for the answer. On the OpenAI door it is never dispatched: the loop hands the call back. 60a's definition has no slot for a command, a URL or a script, so nothing the app declares can be executed on Apogee's side.
- **The scope narrows, and the app consents** ([59f](../v0.2.3/paired-apps-with-scopes.md)). The app's scope decides whether it may offer tools and which server tools sit beside them. The server can **narrow**: it can refuse a declaration or withhold its own tools. It never **answers** a client call itself. Whether a call runs is the app's own decision, made in its own code, with its own user if it asks one. A server session's `bypass` or accept-edits mode reaches no client call. Results are data the model reads, never instructions. Any server tool a result prompts the model to call meets the server's gate and the app's scope exactly as any other call does. By 59f's rule a granted class is a standing grant that the config's `deny` overrides. With no app credential there is no confirm function on a served request, so an `ask` resolves to deny (`handler.cpp:479-481`).
- **Only the app's own sessions.** A client's tools are offered only to the session or request that client makes. Another app's session, a device's session and the person's terminal never see them. On the OpenAI door they live for one request, or for one session the app holds.
- **Secrets are unleakable** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). A declaration, request, result, event, transcript and log line carry tool names, arguments and results. They never carry the app's credential (59f's token), anything from `secrets/`, or a server tool's configuration. The request logger still prints method, path and status only. A private chat logs nothing of itself, its client calls included (57a).
- **UTF-8 where it enters, and bounded** (CLI CLAUDE.md). A declared name, description and schema, and every result, are mended to UTF-8 where they arrive: a result on a live connection passes through the same mend as any tool's at `dispatch` (`tool.cpp:297`), and on the OpenAI door it arrives as a history `tool` message and is mended where that is read. A declaration is bounded where it is taken (Open questions: the name rule, 128 tools, 64 KB a definition).
- **Additive protocol** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The new lines, events and `capabilities` field join `protocol_version` 1. An older driver that sends no `tools` line sees no change. On the OpenAI door, a request that was refused now works; no request that worked changes. The reversed sentence in [http-api.md](../../../reference/cli/http-api.md) is revised with a date, and the release notes say so.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)). One definition (60a) and one remote adapter serve both doors and both transports. The loop learns one new thing, handing a call back, and every surface reads it from the same `RunResult`.
- **Interactive = pipes, serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). The app listens on nothing. On stdio it is the parent of the child. Remotely it dialled `serve` (59b), and calls come back over that connection. `cli.no_listen_symbols` and `cli.complete_opens_no_listening_socket` stay green, with no new allow-list entry.
- **The four layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). Handing a call back is the loop's (`agentloop/`, Business). The remote adapter is Business, beside 60b's. The wire lines are Presentation's (`machine/`, `httpserver/`). `agent/` learns nothing about connections or HTTP.
- **The dev channel** ([CLAUDE.md → Development runs on the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Tests are hermetic. A live pass is `apogee-dev` over `~/.apogee-dev`, never the release `apogee` or `~/.apogee`.

## Technical approach

Code paths under `lib/src/cli/source/`; test paths (`tests/…`) under `lib/src/cli/`.

| File | Change |
|---|---|
| `business/agent/tool_definition.h/.cpp` (60a's) | `runs` gains `client`, `origin` gains `client` (the app's paired name, or `stdio` for a local driver), and `permission` gains the class `client`. Each is one more word, which is additive. |
| 59f's scope (`AppScope`) | `AppScope`'s `tools` takes the class `client` (`--allow tools:client`, `--deny tools:client`), checked where each door takes a declaration. A refusal is 59f's `403 scope_refused`. The approval line says it plainly: "It may offer its own tools; Apogee never runs them." `validate` refuses a client definition that names a reserved word or prefix (Open questions). |
| `business/agentloop/loop.h/.cpp` | **Handing back.** A tool marked `runs: client` and set to hand back (the OpenAI door's mode) is intercepted before dispatch, as `ask_user` is (`loop.cpp:602-620`). Its calls are collected, and the step's other calls run as today. If any were collected, `run` returns without calling the model again, with `RunResult::pending_calls` (the calls, in the model's order) and `finish_reason = ToolCalls`. The history ends with the assistant message and any server-run results. A run started on a history whose last messages are `tool` results continues the same turn; `turn_start` already reads that turn as starting at its last user message (`loop.h:102-107`). A tool marked `runs: client` and not set to hand back is dispatched like any other (the live-connection mode). |
| `business/devicetools/remote.h/.cpp` (60b's remote adapter) | Takes an origin. For `client`, the model sees the app's own name (no `device__` prefix), there is no grant and no host, and the tool is gated by no server checker (60b's device-gated flag). Its `run` sends a `tool_request` over the `Link` and waits for the `tool_result`. A dropped link fails the call, as 60b says. Whichever of 60b and this item builds first creates the adapter and the lines; the other reuses them unchanged. |
| `presentation/machine/protocol.h/.cpp`, `json_reporter.cpp`, `driver_input.cpp` | Inbound: 60b's `tools` line, its definitions marked `runs: client`, and `tool_result` / `tool_progress`. Outbound: `tool_request` and `tool_cancel` events. The session's `capabilities.client_tools: true`, and `tools` and `tool_result` in `accepts`. A `notice` for each declared tool that was set aside, and why. `kMachineSchemaVersion` moves on. |
| `presentation/httpserver/handler.h/.cpp` | The refusal at `:666-671` goes. `tools` is parsed as OpenAI function tools (`type: "function"`, `function.name`, `function.description`, `function.parameters`), each converted to a 60a definition, `runs: client`, set to hand back. `tool_choice` takes `auto` and `none`; `parallel_tool_calls` is read (Open questions). The legacy `functions` and `function_call` fields are refused with a `400` naming `tools`. `pending_calls` become the response's `message.tool_calls`, with `finish_reason: "tool_calls"`; `openai_finish_reason` says `"tool_calls"` only when calls are handed back. On a session, a paused turn is held with the session, and the next request must answer it with `tool` messages. |
| `presentation/httpserver/sse_reporter.cpp` / `sse_writer.cpp` | The streamed shape: `delta.tool_calls` chunks (`index`, `id`, `type`, `function.name`, `function.arguments`), then a final chunk with `finish_reason: "tool_calls"` and `[DONE]`. |
| `presentation/httpserver/session.h/.cpp` | A session's pending calls (ids, names, the server-run part of the step), which are not persisted until the turn completes. While a session waits on them, it is listed as waiting on client tools on whatever plane 59f lets the app read. |
| `business/tasks/` (59e's runner) | A task started by an app over Apogee's protocol offers that app's declared tools while the app's link lives. After a disconnect they are `unavailable` (`tool.h:29-34`), and a re-declaration returns them at the next round. |

`chat` and `execute` in machine mode host the adapter over their own stdio as the `Link`, so the stdio door needs no server. 59b's relay carries the same lines on the paired connection.

**Reference.**
- Siblings:
  - [60a](one-tool-definition.md), the definition an app declares in. Its scope note leaves serve's refusal of client tools as it is; this item is where that changes.
  - [60b](device-tools.md), the CLI's own toolsets offered by a device, which shares the lines and the remote adapter. The difference is who runs the code and whose gate decides.
  - [59b](../v0.2.3/machine-mode-over-the-network.md), the connection and its liveness.
  - [59c](../v0.2.3/many-sessions-at-once.md), the lanes. A hand-back ends the request, so no lane is held while the app works.
  - [59e](../v0.2.3/tasks-outlive-the-connection.md), the task runner.
  - [59f](../v0.2.3/paired-apps-with-scopes.md), the app's identity and scope on both doors.
  - [59g](../v0.2.3/capability-discovery.md), which says whether this credential may offer tools.
  - [59h](../v0.2.3/structured-output-on-every-door.md), structured output.
  - [60d](building-an-app-on-apogee.md), the integrator's guide, which documents this.
  - The integration's standing law, [maintaining-the-app-integration.md](maintaining-the-app-integration.md), whose census classifies the client-tool door.
  - Darwin [7d](../../darwin/v0.2.4/apogeekit-swift-package.md), ApogeeKit, where a Swift app declares and answers tools.
  - Darwin [7b](../../darwin/macos/v0.2.4/mac-offers-its-files-and-shell.md), the Mac's device tools, which this item does not touch.
- In-house precedents:
  - `ask_user` and `find_tools`, calls the loop answers without dispatch (`loop.cpp:602-638`).
  - The MCP client as a remotely run tool set, namespaced (`business/mcp/registry.cpp:96-120`), with a server that fails to connect skipped with a warning and never fatal (`:41-69`). Divergence: an app's names stay its own, because its code expects them back.
  - 56b, the tools an ACP host hands a session, with their credentials kept out of every line.
  - `ToolOutcome::unavailable` for a source gone for the turn (`tool.h:29-34`).
  - The served session as a saved chat ([http-api.md](../../../reference/cli/http-api.md) → Sessions): a known id appends the request's messages, so a continuation sends only its `tool` messages.
- External prior art, OpenAI's function calling, which stock clients speak:
  - request `tools` (`type: "function"`, `name`, `description`, `parameters`), `tool_choice` and `parallel_tool_calls`;
  - a response message with `tool_calls` (`id`, `type`, `function.name`, `function.arguments` as a JSON string) and `finish_reason: "tool_calls"`;
  - streamed `delta.tool_calls` keyed by `index`;
  - the client's `role: "tool"` messages, each naming its `tool_call_id`.

  Anthropic's `tool_use` / `tool_result` is the same exchange, and Apogee's backends already translate it into the IR. Divergences:
  - `tool_choice` forcing (`required`, or a named function) is refused rather than faked, because the IR carries no forcing (llama.cpp's path pins `auto` and one call per step, `data/backends/llama_chat.cpp:120-122`).
  - Server tools can sit beside an app's only on a session.

**Docs when it ships:**
- [http-api.md](../../../reference/cli/http-api.md): a "Client tools" section (the request, the hand-back streamed and not, the continuation, sessions, the errors). The `tool_mode` row and the error table are revised, and "It does not run tools the client defines" becomes a dated revision: "It never runs a client's tools; it hands their calls back".
- [machine-mode.md](../../../reference/cli/machine-mode.md): a "Your own tools" chapter beside 60b's "Device tools".
- [tools.md](../../../reference/cli/tools.md): `runs: client`.
- `machine-schema.json`, regenerated.

## Testing

Hermetic: a temporary `APOGEE_HOME`, the `mock` backend's scripted tool calls (`data/backends/mock.cpp:132-183`).

- **`tests/business/agentloop/loop_test.cpp`** — a call to a hand-back tool is never dispatched (its `run` fails the test if called); a mixed step runs the server call and hands back the client call; a continuation on `tool` results finishes the turn; a repeated hand-back is held by `kRepeatedCallLimit` as any call is.
- **`tests/presentation/httpserver/handler_test.cpp`** — the refusal case at `:594-608` is rewritten into the client-tools cases: the hand-back, non-streamed and streamed; the continuation, stateless and on a session; a stateless request with `tool_mode: "all"` is refused; the name clash, the reserved names and the bounds; `functions` and `tool_choice: "required"` are refused; a request without `tools` is byte-identical to before.
- **`tests/business/devicetools/remote_test.cpp`** — the `client` origin (bare names, no grant, no server checker consulted); a disconnect mid-call; a late result is dropped.
- **`tests/presentation/machine/machine_mode_test.cpp`** — the lines against the schema, `capabilities.client_tools`, the set-aside notice.
- **`cli.client_tools`** (`tests/scripts/py/client_tools_check.py`, stdlib only) — the real binary as a stdio child and under `serve --port 0` on loopback; on stdio, a scripted app declares `search_postings` and answers its `tool_request`, and the turn's answer carries the result; over HTTP, the same round trip, streamed and not; a planted marker file proves nothing ran on the server's side.
- **`cli.reference_driver`** gains a client-tools scenario on both of 59b's transports. **`cli.http_api_conformance`** and **`cli.machine_schema_conformance`** cover the new fields.

Guardrails:

- **Never run here.** The loop and handler tests give every client tool a `run` that fails the test if called. The e2e plants a marker file that a server-side run would touch, and checks it is untouched.
- **Stock clients unchanged.** A golden pins that a request without `tools` returns byte-identical bodies and streams before and after the change. `finish_reason` is `"tool_calls"` only when calls are handed back.
- **Nothing silently ignored.** A table test sends every OpenAI tool field this door does not honor (`functions`, `function_call`, `tool_choice: "required"`, a named `tool_choice`) and requires a `400` for each.
- **Scoped and separate.** A second app's session, and a device's, never list or call the first app's tools. A scope that forbids client tools refuses the declaration on both doors (with 59f).
- **Leaks.** `leak_test.cpp` gains an app token and a planted `secrets/` key, and searches every declaration, `tool_request`, result, meta-frame, transcript and `-v` log line for both.
- **Clean failure.** A disconnect mid-call, a late result and a cancel each end the call `failed`, and the session lives on.
- **The anti-goal.** The machine-mode, ACP and serve conformance suites pass unchanged with no client tools declared. `chat` and `complete` run with no pairing files present.
- `cli.no_listen_symbols`, `cli.machine_schema_conformance`, `cli.http_api_conformance` and `harness.layering` stay green.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What does a stateless request with `tools` offer, and when do the server's own tools join? | `[default]` | A **stateless** request with `tools` is offered the app's tools alone, as if `tool_mode: "none"` were set for the server's own. A stock client then gets exactly OpenAI's exchange. The server's own tools (those `--tools` enabled and the app's scope allows) join on a **session** (`session_id`), which holds the server-run half of a paused step. `tool_mode: "all"` with `tools` and no session is a `400` that names `session_id`. Without a session, the results of server tools run in a step would be lost to the client's next request, and the model would ask again. |
| How does a mixed step on a session run, and what must the continuation send? | `[default]` | In a mixed step on a session, the server runs its own calls first, through its gate and the app's scope as today. Then it returns only the client's calls, the step's text as `content`. The session stores the whole assistant message and the server's results. The continuation must answer every pending client call id with a `tool` message, and nothing else may come first. A request that does otherwise is a `400` naming the pending ids. A session evicted while a turn waits drops that turn, and the next request gets today's `404` `session_not_found`. |
| What names does the model see, and what happens on a name clash? | `[default]` | On the OpenAI door the model sees the app's own names unchanged, because the app's code matches on them. A name that clashes with a tool the request already offers (the server's, on a session) is a `400` naming the clash and `tool_mode: "none"` as the way to drop the server's. On Apogee's protocol the clashing tool is set aside with a `notice`, and the rest are registered. |
| Which names are reserved, and how is a declaration bounded? | `[default]` | Reserved and bounded. A client tool's name follows OpenAI's rule (`^[A-Za-z0-9_-]{1,64}$`). It may not be `ask_user` or `find_tools`, or start with `mcp__`, `device__` or `app__`. A request or a `tools` line holds at most 128 tools (OpenAI's own limit), and a definition at most 64 KB. A refusal names the bound. 60a's `validate` checks each definition. |
| Which `tool_choice`, `parallel_tool_calls` and legacy-field values does the OpenAI door take? | `[default]` | `tool_choice` takes `auto` (the default) and `none` (the app's tools are not offered this request). `required` and a named function are a `400` naming the accepted values, until the IR carries a forcing that every backend honors. `parallel_tool_calls` is accepted: `true` is the default, and `false` hands back only the first client call of a step and drops the rest with nothing run, said in a `notice` meta-frame when `apogee_events` is on. `functions` and `function_call` are a `400` naming `tools`. None of them is ever silently ignored. |
| Are client tools ranked or embedded like the server's? | `[default]` | Client tools are always offered and never ranked. They join 26g's core beside `find_tools`, because the app chose them for this request. They are never embedded and never enter the tool-vector cache. |
| How long may a client call on a live connection take, and what ends it? | `[default]` | A client call on a live connection has no server-side clock while the link lives, as 60b's device calls have none. The app's own code bounds its work. The turn's `cancel` sends `tool_cancel`. 59b's liveness decides "disconnected", which fails the call as "`<app>` disconnected before answering; whether its call took effect is unknown". The app's tools are `unavailable` for the rest of the turn, and a reconnect re-declares them. |
| Who may declare client tools on stdio, and which commands advertise them? | `[default]` | On stdio, any driver may declare client tools. There is no pairing and no scope locally: the child runs as the person, as the Mac app's does, and its own server tools follow the local config as they do today. `capabilities.client_tools` is advertised on `chat` and `execute`. `complete` stays one-shot and takes no `tools` line, so an app wanting a one-shot with its tools uses the OpenAI door or a `chat` child. |
| May a request with no app credential send `tools` on the open inference plane? | `[default]` | On the open inference plane, a request with no app credential may still send `tools`. The server runs none of them, so it gains no new reach, and the stock-client promise (SPEC → Success criteria) covers function calling. Server tools beside them follow `--tools` as today. With an app's credential, the app's scope applies on top. |
| How does a served session's transcript record client calls, and what does a resume do with them? | `[default]` | A served session's transcript records client calls and results like any tool's, with the origin `client:<app>`, so `chats show` and a terminal `--resume` read them. A resumed terminal session does not offer the app's tools. A replayed call to one is history, never re-run. |
| What do `apogee_tool_calls` and `X-Apogee-Tools-Used` name? | `[default]` | `apogee_tool_calls` and `X-Apogee-Tools-Used` keep naming the tools the server ran and nothing else. Client calls are in `tool_calls`. |
| How do an app's own tools sit in its scope? | `[default]` | An app's own tools sit in its scope as one more tool class, `client`. 59f hands this call to this item ("An app's own tools on either door, and how they sit beside the scope's classes"). **The default scope grants `tools:client`**, unlike the server's classes, which a default scope leaves out. A client tool reaches nothing of the server's, and its arguments carry nothing the app could not already read in the answer. The person can withhold it with `--deny tools:client`, and then a declaration is refused with `403 scope_refused` on both doors. Which server tools sit beside an app's own is 59f's narrowing (`narrow_tools`), unchanged. |
| The app's identity on each door, the narrowing and `scope_refused` | `(consumed decision)` | [59f](../v0.2.3/paired-apps-with-scopes.md)'s: an `apa_` bearer resolved on the open plane and under `--devices-only`, the narrowing and `scope_refused`. |
| Whether this credential may offer tools; `tools` with `response_format` | `(consumed decision)` | Saying whether this credential may offer tools is [59g](../v0.2.3/capability-discovery.md)'s. A request that sends both `tools` and `response_format` follows [59h](../v0.2.3/structured-output-on-every-door.md): the schema governs the final answer, and a response that ends in `tool_calls` has none to check. |
| The lines, the adapter, the disconnect wording; the connection; a task's grants | `(consumed decision)` | The lines, the adapter and the disconnect wording are shared with [60b](device-tools.md). The connection, framing and liveness are [59b](../v0.2.3/machine-mode-over-the-network.md)'s. A task's grants and its behavior across a disconnect are [59e](../v0.2.3/tasks-outlive-the-connection.md)'s, revised (Decision log) only to admit the starting app's own tools. |
| The Swift API, the guide and compatibility promise, the census row | `(consumed decision)` | The Swift API that declares a tool and answers its calls is darwin [7d](../../darwin/v0.2.4/apogeekit-swift-package.md)'s. The guide and the compatibility promise are [60d](building-an-app-on-apogee.md)'s. The census row is [maintaining-the-app-integration.md](maintaining-the-app-integration.md)'s. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | The anti-goal: nothing makes standalone use need a server (restated 2026-11-14; GUI SPEC → No server requirement) | the user |
| 2026-11-14 | Specced from the apps-as-clients spike. The user's calls: (1) Taylor'd Portfolio uses the full harness: models, retrieval over its documents and autonomous tasks on the server. (2) Apps reach Apogee through both doors: the OpenAI-compatible one for plain model calls, and Apogee's own protocol (stdio locally, the paired connection remotely) for the harness. (3) Each paired app is held to a scope the person approves; it never reaches files, the shell or other apps' data unless granted. (4) On the OpenAI door, an app's own tools work the standard way: Apogee returns `tool_calls`, the app runs them, and the turn continues, within the app's scope. **This reverses serve's recorded rule**, "It does not run tools the client defines … a request with `tools` is refused rather than silently ignored" ([http-api.md](../../../reference/cli/http-api.md):1626-1627, with :143 and :103). The reference changes with this item as a dated revision, and the refusal test is rewritten. The spirit of the old rule holds: what the door cannot honor (`functions`, forced `tool_choice`) is still refused, never ignored. (5) Apogee ships a Swift package other apps embed, factored from the Mac app's client core, and the Mac app depends on it too: one implementation, tested by Apogee. A Swift app declares and answers its tools through it (darwin [7d](../../darwin/v0.2.4/apogeekit-swift-package.md)), so this item's lines are that package's contract. (6) The CLI SPEC amendment is approved and written ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4, revised 2026-11-14): the OpenAI door "grows standard client-side function-calling for an app's own tools within its scope". (7) Remote use is an add-on; the CLI alone and the Mac app over pipes stay complete and need no server. (8) Taylor'd Portfolio's own side is not specced now. | the user |
| 2026-11-14 | Placed in v0.2.4, track 60, as 60c (accepted with "execute the spike"). Gated on 60a, for the definition; 59b, for the paired connection, Apogee's protocol beyond stdio; 59f, because every call is held to an app's scope and there is no app without it. It sits beside 60b because the two share lines and an adapter. It is not gated on 60b: whichever builds first lays them down. [60d](building-an-app-on-apogee.md) is gated on it. | the user |
| 2026-11-14 | Two mechanisms, one concept. On a live connection (stdio, 59b), a client call blocks the turn and is answered over the link, as a device tool is, so a session's turn and its events keep their shape. On the OpenAI door, request and response, the loop hands the call back and the turn resumes on the next request, so no request is held open while an app works and no 59c lane is spent waiting. Both read the same definition and the same `RunResult`. | the spec |
| 2026-11-14 | Tasks may use a connected app's tools. The plan's call ("tasks (59e) can use a connected client's tools while it is connected") revises 59e's recorded default, "a task runs on the server's tools only, never a device's" ([`tasks-outlive-the-connection.md`](../v0.2.3/tasks-outlive-the-connection.md), Open calls), **for an app's own tools only**. Device tools stay outside tasks, as 59e records. | the spec |

## Out of scope

- Taylor'd Portfolio's own side: its provider, its tools and its migration (the user's call, 2026-11-14: not specced now).
- The CLI's own toolsets offered by a device ([60b](device-tools.md)), and device tools inside a task (59e's rule stands).
- Forced `tool_choice` (`required`, or a named function) until the IR carries forcing.
- Tools on `POST /v1/completions` and on `complete`'s stdio one-shot.
- An ACP host's own tools beyond 56b's MCP servers, and anything `mcp serve` offers.
- One app's tools offered to another app's or a device's session.
- Any server-side execution of a client tool, in any form, ever.

## Links

- **Blocked by:** [60a](one-tool-definition.md) · [59b](../v0.2.3/machine-mode-over-the-network.md) · [59f](../v0.2.3/paired-apps-with-scopes.md) · **Blocks:** [60d](building-an-app-on-apogee.md)
- **Sibling:** [60b](device-tools.md) (shares the lines and the remote adapter; not gated either way, whichever builds first lays them down)
- **Relates to:** [59c](../v0.2.3/many-sessions-at-once.md), [59e](../v0.2.3/tasks-outlive-the-connection.md), [59g](../v0.2.3/capability-discovery.md), [59h](../v0.2.3/structured-output-on-every-door.md), [maintaining-the-app-integration.md](maintaining-the-app-integration.md), darwin [7d](../../darwin/v0.2.4/apogeekit-swift-package.md), darwin [7b](../../darwin/macos/v0.2.4/mac-offers-its-files-and-shell.md), 56b, 57a, 26g, [ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0006](../../../adrs/cli/backwards-compatibility.md), [ADR 0008](../../../adrs/cli/module-map.md)
- **Reverses:** serve's recorded refusal of client tools ([http-api.md](../../../reference/cli/http-api.md) → What this server does not do); revises 59e's recorded default for an app's own tools only
