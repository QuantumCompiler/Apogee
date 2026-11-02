# `apogee acp`: Apogee as an agent over the Agent Client Protocol

`apogee acp` is Apogee as the agent of an **Agent Client Protocol** (ACP) host
-- Xcode 26.6 and later, through Settings › Intelligence › Agents › "Add an
Agent". The host starts `apogee acp` as its child and speaks JSON-RPC 2.0 to it
over stdin and stdout, one message a line. It is the CLI's fifth core surface
(SPEC.md → Core surfaces): **pipes, never a port** -- stdout carries JSON-RPC
and nothing else, every diagnostic goes to stderr, and nothing listens.

What the person gets in the host's coding assistant: Apogee's models in its
model menu, Apogee's own tools, Apogee's permission gate asking through the
host's permission sheet, a working Stop button, and every conversation saved
as an Apogee chat the host can list, reopen and continue (56c).

## Adding it to Xcode

In Xcode 26.6 or later: Settings › Intelligence › Agents › Add an Agent. Three
commands set it up from Apogee's side (56d); none of them reads or writes
anything of Xcode's -- Apogee prints, you type.

**1. `apogee check`** says what this Mac's Xcode can do, in its `Xcode` section
(after `MCP`), asking Xcode's own command-line tools (`xcode-select -p`, then
`xcrun`, each bounded at 5 seconds):

| Row | `ok` | `skipped` |
|---|---|---|
| `xcode` | `Xcode 27.1 (27A9275), /Applications/Xcode.app/Contents/Developer` | none installed (needed only for Xcode); only the Command Line Tools selected, with `sudo xcode-select -s /Applications/Xcode.app` when that app is there; a selected Xcode gone; the tools did not answer; off macOS, the section's one row |
| `mcpbridge` | Xcode's tools for Apogee's agents: `apogee mcp create xcode --from xcode`, or the server already running it -- and the "Allow External Agents to Use Xcode Tools" switch, which `check` cannot see | below Xcode 26.3, which has no bridge |
| `acp agents` | this Xcode hosts ACP agents: `apogee acp registration` prints the fields | below Xcode 26.6 |
| `approval` | shown beside either: Xcode approves an agent by its code signature and its binary's hash, and Apogee's macOS builds are ad-hoc signed, so Xcode asks again after every install or update (W3, below) | -- |

None is ever a warning or a failure: Xcode is optional, so a Mac without it
still checks green.

**2. `apogee acp registration`** prints the sheet's fields for the binary you
run it with and the install it runs under:

```
Add Apogee (dev) to Xcode 26.6 or later: Settings › Intelligence › Agents › Add an Agent
  Name:         Apogee (dev)
  Executable:   /Users/you/.local/bin/apogee-dev
  Interpreter:  (leave empty)
  Arguments:    acp
  Environment:  PATH=/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin
Arguments and Environment Variables take one entry a line. PATH is the one this apogee runs with, so the agent finds the subscription CLIs on it: print these fields again after it changes.
Xcode approves an agent by its code signature and its binary's hash. Apogee's macOS builds are ad-hoc signed, so Xcode asks again after every install or update.
```

- **Name** follows the build's channel: `Apogee`, `Apogee (dev)`, `Apogee (test)`.
- **Executable** is the running binary's absolute path, symlinks resolved --
  Xcode does no `~` or `PATH` lookup.
- **Arguments** are `acp`, after the root you printed it from -- a root flag
  (`--dev`, `--test`, `--release`, `--custom <config>`) or `--config`, any
  path made absolute -- and followed by any of `execute`'s flags given to `acp`
  before `registration` (`apogee acp --permission-mode accept-edits
  registration`), which every session then has.
- **Environment** carries `APOGEE_HOME` and `APOGEE_CONFIG` when they are set
  -- the agent Xcode starts opens the install you set it up from, whatever
  environment Xcode hands it -- and `PATH`, so a subscription CLI is found:
  an app opened from the Dock hands its children launchd's `PATH`. Print the
  fields again after your shell's `PATH` changes; a stale one costs a backend
  refused in M26's words, never a wrong one.

`--output-format json` gives the same as one document, `acp.registration`
([machine-mode.md](machine-mode.md)) -- what the macOS app's Settings › Xcode
reads. The shell's exec line refuses it with `acp` itself: the fields are
pasted into Xcode, never used inside the shell.

**3. `apogee mcp create xcode --from xcode`** (optional) gives every session
Xcode's own tools from the config, whether or not Xcode names its server in
`session/new` ([tools.md](tools.md#mcp-presets-the-servers-apogee-ships)).

The fields, one by one:

| Field | What to give it |
|---|---|
| Executable | The binary's **absolute path**, written out in full -- Xcode does no `~` or `PATH` lookup. For development, `apogee-dev` (`make -C lib/src/cli install MODE=dev`); a release install's `apogee` otherwise. |
| Arguments | `acp`, then any of `execute`'s flags every session should have (below) -- `-m <backend>`, `--suite <suite>`, `--permission-mode accept-edits`, `--allow-bypass`. |
| Environment Variables | `PATH`, as your shell has it, when a subscription CLI (`claude`, `codex`, `gemini`, `ollama`) answers: an app started from the Dock hands its children launchd's `PATH`, on which none of them is found. Apogee resolves nothing itself; a backend whose CLI is missing is refused in M26's words when chosen -- named by the `acp` line's `-m`, as `session/new`'s error; from the model menu, as a notice (`set model: backend 'claude' is configured but could not be started: …`), the menu set back and the session left on the backend it was on. |

`apogee acp --help` prints the same; `apogee acp registration` prints the
values for this binary.

## One session

Each ACP session is a **captured child of the binary's own `execute`**, in
machine mode ([machine-mode.md](machine-mode.md)), started in the session's
folder:

```
apogee [--config …] execute --input-format stream-json --output-format stream-json --tools <the acp line's flags> \
       [--acp-host-servers <handover>] (--acp-origin <host> --acp-cwd <folder> | --resume <id>)
```

-- a new session's child told who began it and the folder its `session/new`
named, a loaded or resumed one's started with `--resume` on its chat (below).
`--acp-host-servers`, `--acp-origin` and `--acp-cwd` are `execute`'s hidden
plumbing: no help, no completion.

So a prompt is **one turn of execute's loop**, opened through execute's own
path (ADR 0011): the project's `AGENTS.md` (else `CLAUDE.md`) read into every
request, the folder attached, the coder persona -- ACP never gets an opening of
its own. The child inherits the root `apogee acp` was started under (`--config`,
and the data directory as `APOGEE_HOME`, each made absolute against `acp`'s own
folder, so a relative `--config` names the file `acp` read) and nothing else of
its descriptors; its stderr is captured, never inherited: drained into an 8 KiB
tail whose last line is folded into a failure, and copied raw to `acp`'s own
stderr under `--verbose`. A fatal error in a local model ends one session,
which the host is told, never the agent. The `acp` line's other flags reach the
child as given, and it runs in the project folder, so a relative `--attach` or
`--image` is found there -- each project's own, which suits a host that starts
`acp` from a folder of its choosing.

- **One live session per process.** A `session/new` ends the session before it
  (its child's stdin closed, a turn in flight cancelled), so one model is in
  memory; a prompt to it is answered `-32602`, "a newer session replaced it".
- **Every session is a saved chat** (56c), in the one store `chats list`,
  `chats show` and `chat --resume` read -- written by its child once a turn is
  answered, so a session closed before any writes nothing. Its id is the
  chat's. [Saved sessions](#saved-sessions) has the four methods.
- **The host's MCP servers** in `session/new` are that session's (56b): its
  child connects the stdio ones beside the config's, for its life alone, through
  the same gate -- [below](#sessionnew).
- When the host closes `acp`'s stdin, each session's turn is cancelled and its
  child's stdin closed; `acp` exits 0 once every child has ended.

## `initialize`

```json
{"protocolVersion":1,
 "agentCapabilities":{"loadSession":true,
                      "sessionCapabilities":{"list":{},"resume":{},"close":{}},
                      "promptCapabilities":{"image":false,"audio":false,"embeddedContext":false},
                      "mcpCapabilities":{"http":false,"sse":false}},
 "agentInfo":{"name":"apogee","title":"Apogee","version":"<apogee version>"},
 "authMethods":[]}
```

- **Protocol version 1**, pinned in one constant with the day it was last
  checked against the protocol's schema (`acp::kProtocolReviewed`,
  2026-11-01: the v1 schema 1.28.0) -- re-checked whenever ACP work is
  touched and at every release roll, as the carried model lists are (ADR
  0009). A host asking for another version is answered 1, as the protocol
  says.
- **Saved sessions** (56c): `loadSession`, and `session/list`,
  `session/resume` and `session/close` under `sessionCapabilities`, each
  advertised because it works. `session/delete` is not: `chats delete` is the
  door, and Xcode 27.1's ACP client has no delete message.
- **No sign-in.** `authMethods` is empty: a vendor CLI signs itself in
  (SPEC.md → Principles), and an API key lives in Apogee's own store.
- What `acp` reads of the host's capabilities: `session.notices` (it shows ACP's
  `notice` update) and `elicitation.form` (it takes a form). `fs/*` and
  `terminal/*` are never used: Apogee's tools run in its own process, under its
  own gate.

## `session/new`

`cwd` must be an absolute path to a folder that exists, else `-32602` naming it.
The answer, once the child says it is open (its `session` event):

- `sessionId` -- the chat's id (56c), the one `chats list` lists it by and
  `chats show` and `--resume` take, so one id names the conversation
  everywhere. It is the child's `session` event's `chat_id`.
- `modes` -- **manual**, **accept-edits** and **plan**; **bypass** too when the
  `acp` line carries `--allow-bypass` (or `--permission-mode bypass`).
  Dont-ask is not offered: a host is always there to ask -- but a session
  launched in it shows it. The current mode is the line's `--permission-mode`,
  else manual.
- `configOptions` -- the session's config options, each a `select`:
  - `model` (category `model`): **every configured backend that chats**, the
    subscription CLIs included, as the macOS app offers them -- an MLX entry
    over a model no chat runs (an embedder, a drafter) left out -- each
    described by its type and model. The roles chain's default is first (`-m`
    on the `acp` line over it); the current value is what the child says it
    runs. A backend's roster models are not listed separately.
  - `mode` (category `mode`): the modes above. Modes "will be removed in a
    future version" of ACP in favour of config options, and the protocol
    advises offering both until then; both move together.

The session's opening -- the persona, the project's context, the folder
attached -- is said as **notices**: to a host that shows them (`session.notices`
advertised), as ACP's `notice` update at once; otherwise they lead the next
turn's `agent_thought_chunk` stream, where the person can unfold them, never
inside the answer. Every notice also goes to stderr.

**The host's MCP servers** (`mcpServers`, 56b) -- Xcode's own build, test and
preview tools, should it send them -- join **that session** for its life: its
child connects every stdio entry beside the config's servers, before
`session/new` answers, and its tools reach the model as `mcp__<server>__<tool>`,
asked about through the permission sheet as any MCP tool's are. An http or sse
entry is never dialled (`initialize` advertises neither), and a server that
will not start is skipped; what was set aside is said in **one** notice. The
values of an entry's `env` -- credentials, by the protocol's word -- reach the
server alone: `acp`'s stderr names each server, its command and its env
variables' names (`the host's MCP servers: xcode-tools (/usr/bin/xcrun; env
MCP_XCODE_SESSION_ID)`), never a value. The config file is never written, and
the next session sees none of them. What is taken and skipped, the gate, Xcode's
own consent prompt and the `xcode` preset to fall back on are in
[tools.md → Servers an ACP host hands a session](tools.md#servers-an-acp-host-hands-a-session).

## Saved sessions

*(56c.)* An ACP session is an ordinary Apogee chat, saved in the one session
store -- never a store of the protocol's own. It is written by its child after
every answered turn, as any chat is, and **names its origin**: the host's
`clientInfo.name` as sent (`acp` when it sent none) and the `cwd` its
`session/new` named, as named. Both are set when the chat begins and never
rewritten by a resume, from anywhere. `chats list` ends its row
`(from <origin>)`, `chats info` gains `origin:` and `cwd:` lines, and their
JSON documents carry `origin` and `cwd`
([machine-mode.md](machine-mode.md#everything-else-is-a-cli-command)). A host's MCP servers
are never saved with the chat.

**`session/list`** -- the chats an ACP host began (an `origin` and a folder),
newest first, as `chats list` orders them; with `cwd`, only those begun in
exactly that folder (a relative `cwd` is `-32602`). Each entry is
`sessionId`, `cwd`, `title` -- the name `chats list` shows, left out while
that is the bare id -- and `updatedAt`, the chat's `updated_at` (ISO 8601
UTC). One page, with no `nextCursor`; a page asked for after it is empty. A
chat begun in a terminal has no folder, which every entry needs, so it is not
listed -- though it opens.

**`session/load`** -- any saved chat's id, since the store is one: a chat begun
anywhere opens. Its id alone: a chat's name or title, which `--resume` takes,
is no `sessionId` here, and neither is a path. Its child is started with `--resume <id>` -- `execute`'s own
resume, its notes and its fallback for a backend gone (ADR 0011) -- in the
request's `cwd`, with the request's `mcpServers` handed over as
`session/new`'s are. Once the child is open, the chat's turns are **replayed**
as `chats show` reads them (`logger::saved_turns`), and only then is the
request answered (`modes`, `configOptions`):

| Saved | Replayed as |
|---|---|
| the user's words | one `user_message_chunk`, what rode them named as `chats show`'s text names it (`attached: report.pdf (1 file, 9 KiB)`), never their bytes |
| each tool call | a `tool_call`, `completed`, titled by its name and its one-line arguments, its kind from the table below -- never its result, which `chats show` never shows |
| each part of the answer | an `agent_message_chunk` |
| a compaction | an `agent_message_chunk`: `history compacted; what came before is this summary:` and the summary |

Every update carries an id from the entry's index in `chats show
--output-format json`'s `turns` -- `messageId` `entry-<i>-user`,
`entry-<i>-agent-<n>`, `entry-<i>-compaction`, and `toolCallId`
`entry-<i>-tool-<n>` -- never its turn number, which an unanswered turn and a
compaction share with the turn before them. So a replay and `chats show`'s
JSON line up entry by entry, and every id is distinct. Nothing hidden is
replayed: a saved chat holds no thinking and no injected context. The
resume's own notes -- a backend gone, a field dropped -- follow the replay as
notices, never as history.

**`session/resume`** -- the same opening, with nothing replayed: the answer
comes once the child is open. A resumed session's next turn is numbered on
from the chat's count, as a resumed machine session numbers (`chats show`
lists it after the earlier turns).

An id no chat has is refused **`-32002`** (the protocol's "resource not
found"), its message naming it as `chats show` does -- `no chat named
'<id>'` -- and `data.sessionId` the id asked for; nothing ends. A chat's name
or title, or anything holding a `/` or a `\`, is refused the same way, as
`no chat has the id '<id>': a saved chat opens by its id, as session/list
gives it, never by its name` -- a path never reaches the store. A load or
resume of a chat this process still holds -- live, or still ending -- ends
that session and waits for its child first (30 s, then it is terminated), so
one chat has one writer here -- a session's id is its chat's, so one spelling
names it; across processes there is no lock, and a chat open in Xcode and
resumed in a terminal goes to the last writer, turn by turn, as two terminals
do. 55b's outside rename and delete are honoured.

**`session/close`** -- cancels the session's turn, as `session/cancel` does,
and ends its child; its chat is already saved after its last answered turn. A
prompt to it afterwards is refused, "the host closed it". Closing an unknown
or already closed session answers success.

## The mapping

### Out: machine events → ACP

Every machine event type is mapped or deliberately dropped; the unit suite
fails a type with no rule (`acp/translate.cpp`'s `event_rules`).

| Machine event | ACP |
|---|---|
| `session` | answers `session/new`, `session/load` (after its replay) or `session/resume` |
| `thinking_delta` | `agent_thought_chunk` |
| `answer_delta` | `agent_message_chunk` |
| `tool_call` | `tool_call` on its first report (`pending`), then `tool_call_update` as it moves: `running` → `in_progress`, `done` → `completed`, `failed` → `failed` -- every card ends `completed` or `failed`, a call stopped at its sheet included. Each call is its own card, keyed on the model's id -- and where that id repeats (Gemini's API uses the tool's name), a later call opens a card of its own under a fresh id (`write_file-2`), never the earlier call's again. A call a `delegate` sub-session makes is a card of its own too, under its own id, drawn while the delegation's card is `in_progress`: its sheet names that card, and it ends as any call does |
| `todo` | `plan` -- each item an entry, `pending` → `pending`, `active` → `in_progress`, `done` → `completed`, priority `medium` |
| `tool_status` | a side call's line (`utility — …`, `consult — …`) as a thought; a tool's own `[tool] <name>` line is dropped -- its `tool_call` draws it |
| `sources`, `memory` | a thought line naming the excerpts sent, or what was recalled |
| `notice` | `notice` to a host that shows them; else the turn's thought stream -- the next turn's when none runs |
| `question` (permission) | `session/request_permission` (below) |
| `question` (`ask_user`) | `elicitation/create` in form mode, to a host that advertised `elicitation.form`; otherwise answered with a line saying nobody can be asked here, so the model goes on and says what it chose -- never a pick made silently |
| `result` | the `session/prompt` answer: `finish_reason` `stop` → `end_turn`, `length` → `max_tokens`, `content_filter` → `refusal`, `cancelled` → `cancelled`, anything else → `end_turn` |
| `error` | a JSON-RPC error (`-32603`) on that prompt, in Apogee's words |
| `thinking`, `answer_start`, `answer_end` | dropped: markers |
| a task's and a long job's events | dropped: an execute session never writes them |

A tool call is drawn with its **kind** from a table of Apogee's native tools,
held to the native registry by the unit suite (a new native tool is classified
or the suite fails):

| Kind | Tools |
|---|---|
| `read` | `read_file`, `list_directory`, `git_status`, `git_log`, `git_diff`, `git_show`, `read_note`, `list_notes`, `list_collections`, `collection_info`, `graph_explain`, `graph_neighbors` |
| `edit` | `write_file`, `edit_file`, `write_note` |
| `delete` | `delete_file`, `delete_note` |
| `search` | `search_files`, `grep_files`, `search_documents`, `graph_query`, `graph_path` |
| `execute` | `run_command` |
| `fetch` | `fetch_url`, `web_search` |
| `think` | `todo_write`, `consult` |
| `other` | `delegate`; every MCP tool, a symphony played as a tool, anything no row names |

Its title is the tool and its target (`read_file src/main.swift`). A file
tool's target -- `read_file`, `write_file`, `edit_file`, `delete_file`,
`list_directory` -- is its `locations`, decided per tool, never by kind: a
relative path made absolute under the session's folder, and `~` or `~/...`
under the home folder, as the file tools expand it (no location when no home
is known). Every other target is the title alone: a note tool's key (the note
lives in the data directory, not the project), a command, a host.

### In: ACP → machine lines

| ACP | Machine line |
|---|---|
| `session/prompt` | one `user` line: the text blocks as written, each `resource_link` to a file as an `@"path"` mention -- attached through the session's own mention path, as a terminal's `@path` is. A `resource` naming a `file://` URI is taken the same way; an image, audio, other embedded content or an unknown block is refused `-32602` (`promptCapabilities` says none is taken). One prompt at a time: a second while one runs is refused. |
| `session/cancel` (and `$/cancel_request` naming the prompt) | `{"type":"cancel"}`, which trips the turn's own cancellation token: the turn ends `cancelled`, never an error, and the session takes the next prompt |
| `session/set_config_option` `model` | `{"type":"set","model":…}` -- from the next turn; a value the menu did not offer is `-32602`, and a backend the session cannot switch to -- configured, but it did not start (its CLI missing) -- is refused in M26's words as a notice (`set model: backend 'claude' is configured but could not be started: …`), the menu set back to the backend that runs, and the next turn that backend's |
| `session/set_config_option` `mode`, `session/set_mode` | `{"type":"set","permission_mode":…}` -- from the next turn, as the model; the gate reads it at each call after |
| the host's answer to the permission sheet | one `answer` line |

The menu answers a setting at once; the child takes it between turns. One
chosen while a turn runs -- a permission sheet or a form open among it -- is
held and written as the turn ends, ahead of the next prompt: written mid-turn,
it would wait in the child's queue, where a question the turn asks reads, and
drops, every line that is not its answer. Several chosen in one turn: the last
of each is written.

## The permission sheet

Every call Apogee's gate would ask about is put to the host, and **the host's
choice is the answer**. The sheet's `toolCall` is the call's card -- its
`toolCallId` the one its `tool_call` drew -- titled, kinded and located by the
question's own tool and target, what is being asked about, with the question's
`detail` (a unified diff for `write_file` and `edit_file`, the whole URL for
`fetch_url`) as its text content. Four options, each the `answer` line it
sends, so the one path (`answer_permission`) applies it:

| Option | Kind | Answer |
|---|---|---|
| Allow once | `allow_once` | `yes` |
| Allow for this session | `allow_always` | `session` |
| Always allow (remembered in the config) | `allow_always` | `always` -- written to the running channel's config through the one editor (`permissions.<tool>: allow`, or the host into `tools.allowed_hosts`) |
| Deny | `reject_once` | `no` |

A `cancelled` outcome, an error, an option no sheet offered, or a host gone is
`no` -- as `ask` with nobody to ask is deny. Plan mode refuses a write in
plan's words and asks nobody; dont-ask refuses whatever would ask.

A `session/cancel` while a sheet is open ends the turn `cancelled`, as ACP
asks, and the call the sheet was about never runs: its card ends `failed`
(machine mode reports the call `failed` before the turn's `result`), so no
card is left `pending` -- ACP has no `cancelled` status for a tool call, and
the spec leaves marking unfinished calls to the client, which here finds none.
The host's late answer to the sheet, `cancelled` as ACP's cancellation asks,
reaches nobody and changes nothing.

## Errors

| Code | When |
|---|---|
| `-32700` | a line that is not JSON |
| `-32600` | JSON that is no message: a batch, a bare value, a bad id or method |
| `-32601` | a method this agent does not take (`session/delete`, `session/fork`, `authenticate`, `logout`, `session/set_model` among them) |
| `-32602` | bad params: a `cwd` that is not an absolute folder, an unknown session, a replaced or closed session, a prompt it cannot carry, a model or mode not offered |
| `-32002` | `session/load` or `session/resume` naming no chat's id: an id no chat has (`no chat named '<id>'`), or a chat's name or title, or a path (`no chat has the id '<id>': ...`); `data.sessionId` the id asked for |
| `-32603` | the turn failed, in Apogee's words; or the session's apogee ended -- `apogee's session ended (exit N): <its last stderr line>` -- after which a new session is needed |

An unknown field is ignored, and after any of these the process lives on.

## Flags

`apogee acp` binds `execute`'s own flag table, so `-m`, `--suite`,
`--permission-mode`, `--allow-bypass`, `--allow`/`--deny`, `--allow-host`,
`--think`, `--effort`, `--search`, `-v` and the rest complete as execute's do and
reach every session's child as given. The flags the protocol owns --
`--output-format`, `--input-format`, `--resume`, `-c`, `--private`,
`--no-color`, `--raw` -- are refused at launch, in the command's words, and
hidden from its help and completion: `--resume` and `-c` because the host
reopens a chat itself (`session/load`, `session/resume`), and `--private`
because every session is saved -- a private mode is not offered here, and
`chats delete` removes a chat. A launch with no backend to chat with is
refused before the protocol starts.

## The fixtures

`cli.acp_conformance` (`tests/scripts/py/acp_host_check.py`) drives the real
binary as a host would, with the mock backend. **The schema's own examples
stand in for a captured Xcode transcript** until one is recorded: the
protocol's example `initialize`, `session/new` and `session/prompt`
(`tests/fixtures/acp/schema_examples.jsonl`) are replayed against it. To capture
what Xcode 27.1 sends, register the logging stub
`lib/src/cli/tests/scripts/py/acp_capture_stub.py` in Add an Agent by its
absolute path with the arguments `--out <absolute path>`, start one
conversation and send one prompt, then copy the capture to
`tests/fixtures/acp/xcode-27.1.jsonl` (env and header values and home-folder
paths are redacted as it is written) and remove the stub from Xcode's list. The
suite replays that file whenever it is there. The capture changes Xcode's
settings, so it is the person's to run.

The saved sessions (56c) are driven by the same check, in a home of their own
with `clientInfo.name` `acp-test`: new, two prompts, close, then list, load,
resume, a `cwd` filter, an empty session, an unknown id, a chat's name and a
path to its file -- and
`tests/fixtures/acp/saved_chat.json`, a saved chat with a compaction and a
cancelled turn, loaded with every replayed id distinct.

## Limits

- **Windows.** A session is a child process, which the Windows build cannot
  start yet (`platform::start_child`): `apogee acp` refuses there, a recorded
  skip as for the vendor CLIs. Xcode runs only on macOS, so this costs Xcode
  nothing; it bounds other ACP hosts on Windows.
- **Signing (W3).** The binary is ad-hoc signed (`codesign -dv`:
  `flags=0x20002(adhoc,linker-signed)`, no team). Xcode's own binaries suggest
  an agent's approval is tied to its signature and its hash, and that an
  unsigned agent's approval is time-limited ("Unsigned agent attached",
  `unsignedTTL`) -- strings, not observed behaviour. The likely cost: approving
  again after every `make install`, and approving `apogee-dev` apart from
  `apogee`. `check`'s `approval` row, the registration's last line and the
  `xcode` preset's note say so where Xcode is set up (56d); the user's live
  pass records the prompts seen.
- **Xcode 27's filesystem security layer (W4).** Opt-in; it "monitors and
  controls filesystem access by coding agents and any processes they spawn".
  What it does to an agent that reads its data directory, loads models from its
  store and drives Metal is unmeasured until the live pass runs with the layer
  off and then on.
- **A managed Mac** with `CodingAssistantAllowExternalIntegrations=false` has
  the coding assistant's external agents turned off.
- **Xcode's own tools** reach a session only if Xcode names its server in
  `session/new`: the strings in Xcode 27.1 suggest it does (`mcpServers`,
  `xcode-tools`, `MCP_XCODE_SESSION_ID`), and the user's live pass records what
  it sent. If it sends none, the `xcode` preset (`apogee mcp create xcode
  --from xcode`, 56d) registers `xcrun mcpbridge` for every session, from the
  config.
- **Saved sessions** (56c): no `session/delete` (`chats delete` is the door)
  and no `session/fork`, neither of which Xcode 27.1's client sends; no
  private mode; a `compaction_update` is never sent, a compaction replayed as
  words; and no lock across processes (above).
- **Not here:** `fs/*` and `terminal/*`; image, audio and embedded prompt content; slash
  commands and `available_commands_update`.
