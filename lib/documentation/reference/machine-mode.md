# Machine mode: the JSONL protocol

Apogee's contract with a front-end. A GUI powers Apogee by **spawning the
executable and speaking over its pipes** — never over localhost. `serve` remains
the surface for genuine server deployments, where a remote client makes REST
calls to Apogee running on a host; a local front-end uses this instead.

This document is the reference for that protocol. The worked example is
[`reference_driver.py`](../../src/cli/tests/scripts/py/reference_driver.py), which is also
the test that keeps this document true.

## The two flags

```bash
apogee complete --output-format stream-json "what is 2+2?"
```

```bash
apogee chat --output-format stream-json --input-format stream-json
```

```bash
apogee task run "Write the report" --require-file report.txt --output-format stream-json
```

`--output-format` selects the event stream on stdout. `--input-format` selects
JSONL user turns on stdin, which is what makes one `chat` child serve a whole
conversation. It defaults to whatever `--output-format` is, so a driver may pass
one flag; on `chat` the two directions **must agree**, and disagreeing is an
error rather than a silently ignored flag. `task run` and `task resume` take
`--output-format` alone: a task reads nothing from its driver (see
[A task's run](#a-tasks-run)).

## Reading the stream

One JSON object per line on stdout. Every object has a `type`.

```jsonl
{"type":"session","protocol_version":1,"model":"claude-sonnet-5"}
{"type":"thinking"}
{"type":"thinking_delta","text":"…"}
{"type":"memory","chats":2,"decisions":1}
{"type":"tool_status","text":"fetch_url https://…"}
{"type":"answer_start"}
{"type":"answer_delta","text":"…"}
{"type":"answer_end"}
{"type":"result","text":"…","model":"…","finish_reason":"stop",
 "usage":{"input_tokens":12,"output_tokens":3}}
{"type":"question","questions":[…]}
{"type":"error","message":"…"}
```

| Event | Meaning |
|---|---|
| `session` | Once, first. Names the protocol version and the model. |
| `thinking` | The model began reasoning. No text. Sent again with `"budget_reached": true` when the reasoning reached its thinking budget and was ended there (`--think-budget`, or the backend's `thinking_budget`). |
| `thinking_delta` | A chunk of reasoning. **Droppable** — see below. |
| `memory` | `chat` only: what a turn was handed from earlier conversations -- `chats`, past chats' summaries, and `decisions`, recorded knowledge records -- injected for this turn and never into the transcript (26l). Sent before the turn, only when it recalled something. |
| `tool_status` | A tool is running, described in `text` for display -- or another model call the turn makes besides the chat model's own (the embedder, the utility model, the rerank judge, the knowledge clerk, a suite member the model consults), as `<role> — <what it is doing>` (26n). A `consult` (27f) is an ordinary tool call: `[tool] consult`, then `consult — asking utility (<backend>): <the question's first words>` while the member answers, and the answer reaches the model as the tool's result. A suite's verifier checking a tool call or an answer (27g) is said the same way: `validate — asking utility (<backend>): Check …`. |
| `notice` | A line for the user in `text` that is neither progress nor an error — a local model answering without the tools it was given because its chat template cannot take them, or a reply kept as text because it did not match the template's format, or a request trimmed to fit the model's window (`context budget: 2 earlier exchanges not sent`), or what a suite's verifier said (27g): an objection to a tool call returned to the model, a dispute the call runs over, the two positions on an answer under `answers: always` (`validate: …`), a check that could not be made. Show it and keep it; it never ends the turn. |
| `answer_start` / `answer_end` | Bracket one answer's deltas. |
| `answer_delta` | A chunk of answer text. Concatenate in order. |
| `result` | Ends a turn. Carries the whole answer, so a driver that dropped every delta still has it. `usage` is **absent** when the provider reported none — absent is not zero. |
| `question` | `ask_user`. Expects a reply; see below. |
| `error` | A turn failed. The machine-readable half of a diagnostic. |

### Three rules a driver must follow

**1. Ignore unknown types.** New event types are added *without* a version bump,
because drivers are required to tolerate them — that is what lets the schema
grow. A driver that treats an unfamiliar `type` as an error breaks on upgrade.

**2. Drop every `thinking*` event to get what a terminal user saw.** Reasoning
is distinctly typed precisely so it can be discarded. It never appears in
`result.text`, in the answer deltas, or in persisted history.

**3. Read stdout as lines, and read stderr separately.** stdout carries only
JSONL; every diagnostic, warning, and progress note goes to stderr. Merging them
puts prose in the middle of your parser's input.

### `protocol_version`

Currently `1`. It is bumped **only when an existing event's meaning changes** —
a field changing sense under a name a driver already reads. Additions are
compatible by construction under rule 1.

## A task's run

`apogee task run "<goal>" … --output-format stream-json` (and `task resume …
--output-format stream-json`) drives a task as it always does — a plan turn,
then rounds, each an ordinary chat turn checked against the acceptance the task
was given — and streams it: each turn's ordinary events, from `answer_start` to
its `result`, with the task's own lifecycle around them. A front-end that wants a
progress panel for a long-running goal spawns the task this way; its events
arrive on the stdout of the `task run` it invoked, never from anywhere else.

```jsonl
{"type":"session","protocol_version":1,"model":"local"}
{"type":"task_started","task_id":"task-20261004-120000","resumed":false,"transition":{"at":"2026-10-04T12:00:00Z","event":"started","status":"planning"},"history":[{"at":"2026-10-04T12:00:00Z","detail":"Write the report","event":"created","status":"planning"}],"task":{…}}
{"type":"task_plan","task_id":"task-20261004-120000","transition":{"at":"…","detail":"plan","event":"plan_started","status":"planning"},"round":{"round":0,"kind":"plan","outcome":"in_flight",…}}
{"type":"answer_start"}
…
{"type":"result","text":"1. Write it.\n2. Check it.","model":"local","finish_reason":"stop"}
{"type":"task_plan","task_id":"…","transition":{"event":"plan_recorded","status":"running",…},"round":{…},"plan":"1. Write it.\n2. Check it."}
{"type":"task_round","task_id":"…","transition":{"event":"round_started","round":1,"status":"running","detail":"execute",…},"round":{"round":1,"kind":"execute","outcome":"in_flight",…}}
…
{"type":"task_grant","task_id":"…","round":1,"tool":"write_file","target":"report.txt","by":"grant"}
{"type":"task_round","task_id":"…","transition":{"event":"round_ended","round":1,"status":"done",…},"round":{…},"checks":[{"kind":"require_file","value":"/work/report.txt","description":"the file /work/report.txt exists and is not empty","ran":true,"passed":true,"detail":"10 bytes"}],"rounds_used":1}
{"type":"task_finished","task_id":"…","transition":{"event":"finished","status":"done",…},"status":"done","reason":"every check passed and the model reported the task done, in 1 of 8 rounds","task":{…}}
```

| Event | Meaning |
|---|---|
| `task_started` | The run began: `started`, or `resumed` (`"resumed": true`) for `task resume`. Carries `history` — every transition the ledger held before this one — and `task`, the task's whole view as it stands, so a front-end joining at a resume needs no other source. |
| `task_plan` | The plan turn began (`plan_started`) or its plan was recorded (`plan_recorded`, with `plan`); `round` is the plan turn. |
| `task_round` | A round began (`round_started`) or ended (`round_ended`); `round` is that round — its outcome, the checks it passed, the model's self-report, what it let through, refused and answered, its tokens. Once it has ended, `checks` is each acceptance check's state and `rounds_used` the budget spent. |
| `task_grant` | A tool call the task's own grant (`task run --allow`) let through: `round`, `tool`, `target`, `by: "grant"`. Sent after the round's turn, before its `round_ended`. |
| `task_finished` | The run stopped: `status` (`done`, `exhausted`, `stalled`, `failed`, `halted`, `cancelled`), `reason` naming what did not pass, and `task`, the final view. |

**One event per ledger transition, carrying it.** Every event but `task_grant`
has `transition` — the transition exactly as the task's ledger wrote it (`at`,
`event`, `status`, and `round` and `detail` when they say something) — and is
sent only once the ledger holds it. So `task_started`'s `history`, followed by
each later event's `transition`, *is* the ledger's transition sequence, one for
one; `created`, written before the run began, arrives in that history. A
`task_*` event carries `task_id`.

**The view is the read's.** `task` (on `task_started` and `task_finished`) is
the same document `apogee task status <id> --output-format json` prints and
`GET /v1/admin/tasks/{id}` serves: `id`, `status`, `process` (the process running
it, or `null`) and `interrupted`, `goal`, `conversation` (the chat's id),
`folder`, `tools`, `policy` (`agent`, `grants`, `on_question`; `null` without
tools), `rounds_used`, `rounds_budget`, `created_at`, `updated_at`, `reason`,
`checks`, `self_report`, `plan` and `turns`. A driver that applies each event to
the view `task_started` gave it — the transition's `status` and `at`, the turn
each event carries, the `plan`, the `checks` — holds, at `task_finished`, exactly
the final view. **A declared answer (`--on-question answer:…`) is in no event:**
`policy.on_question` says `answer` and an answered question says `by:
"declared"`, never what the answer said; `task status` at the terminal shows it.

**Nothing is asked.** A task in machine mode reads no input — not even when a
terminal is attached — so `ask` with no grant denies (the denial is the tool's
result, recorded with `by: "nobody"`), and a question with no declared answer
ends the task. A turn that does not finish ends in an `error` event instead of
its `result` — the provider's message, `cancelled`, or the question nobody was
present to answer — and `task_finished` follows. Every `[task]` line a person
would read goes to stderr, with the outcome; the exit code is the task's (`0`
done, `130` cancelled, `2` a provider's failure, `1` anything else short of
done).

## Writing to the child

One JSON object per line on stdin.

```jsonl
{"type":"user","text":"what is 2+2?"}
{"type":"answer","text":"Yes"}
{"type":"attach","path":"report.pdf"}
```

An unrecognised line is ignored rather than fatal — the tolerance this protocol
asks of drivers, honoured in the other direction.

### Attaching files

`attach` attaches a file, a folder or a glob to the chat, as `/attach` does at a
terminal: `path` is relative to the child's working directory. It is indexed in
the background and settles before the next `user` message is answered. Each
outcome arrives as a `notice`: what was attached, and how it will reach the model
(inlined whole, or its excerpts retrieved each turn); a file skipped, and why. A
folder over 500 files or 50 MB is refused, since there is no terminal to ask on;
attach a narrower folder or a glob. A `user` message that mentions `@path` attaches
that path the same way, and is answered as typed.

A folder whose files include code a bundled grammar parses (C, C++, Python,
JavaScript, TypeScript, Go, Rust, Java, C#, Ruby, Bash) is also parsed into the
chat's code graph, after its chunks and with no model: one more `notice` follows
the attach line, `graph: 412 nodes, 1820 edges (supported: cpp 30, python 2;
skipped: .md 3)` -- the folder's part of the graph, the files used by language and
what was left out. Attached again, only the files that changed are parsed again.
Should the attach be interrupted, the notice says `graph: not built -- cancelled;
attach it again to build it`. A single file, a glob, or a folder with no such code
gets no graph and no such notice.

An image, a recording or a video is attached the same way. A chat model that can
read it is sent it as it is with the next `user` message; from the turn after, it
reaches the model as text: an image's description, a recording's transcript, or
a video's timeline of what was on screen and what was said, by the time. The
`vision` and `transcription` helper roles write that text, or the chat model when
no helper is set and it can. Audio and video need `ffmpeg` on the child's `PATH`.
Something nothing configured can read is refused in a `notice` that names the
role to set. More than twelve descriptions by a model billed per call are refused
too, since there is no terminal to ask on. A `notice` also reports what could not
be read, such as a video's sound with no model to hear it.

Closing stdin ends the session: the child drains its queued output, persists the
conversation, and exits cleanly.

### Answering a question

When the model calls `ask_user`, the child emits a `question` event and **blocks
until answered**:

```jsonl
{"type":"question","questions":[{"header":"Overwrite","question":"The file exists. Overwrite it?","multi_select":false,"options":[{"label":"Yes","description":"Replace the contents"},{"label":"No","description":"Leave it alone"}]}]}
```

Reply with one `{"type":"answer","text":"…"}` line per question, in order. The
offered options are a convenience, not a constraint — free text is always
accepted. Closing stdin with a question outstanding fails the turn, and the
half-turn is rolled out of history rather than persisted half-finished.

`ask_user` is offered only to a driver reading structured input. With plain-line
stdin an answer would be indistinguishable from the next user turn, and the
harness rule is that the tool is advertised **if and only if** there is someone
to answer it.

### Answering a permission prompt

The same event carries the permission gate's question. When the model calls a
destructive tool — `write_file`, `edit_file`, `delete_file`, `run_command`,
`write_note`, `delete_note` — whose level in `permissions:` is `ask`, the child
emits a `question` with `"kind": "permission"`, the `tool` and its `target`
(the path, the command), and **blocks until answered**:

```jsonl
{"type":"question","kind":"permission","tool":"write_file","target":"notes/todo.md","questions":[{"header":"Permission","question":"Allow write_file on notes/todo.md?","multi_select":false,"options":[{"label":"yes","description":"Allow this once"},{"label":"no","description":"Deny"},{"label":"always","description":"Allow, and remember it in the config"},{"label":"session","description":"Allow for the rest of this session"}]}]}
```

Reply with one `{"type":"answer","text":"…"}` line: `yes` allows this once,
`session` allows the tool for the rest of the run, `always` allows it and
writes `permissions.<tool>: allow` to the config through the same editor the
CLI uses, and anything else denies. A denial is a **tool result** the model
reads, not an error; the turn continues. A driver that closes stdin with a
prompt outstanding fails the turn, exactly as with an unanswered question.

Where there is no driver — `complete --output-format stream-json` is one-shot
and cannot be asked — `ask` resolves to deny, and only `allow` in the config
lets a destructive tool run.

`fetch_url` is asked about **per website** rather than per tool, because a URL
can carry out anything the model has read. The first time a run reaches a host
not in `tools.allowed_hosts`, the event's `target` is that host, `outbound` is
`true`, and `detail` is the whole URL — show it, since what would leave the
machine is in it. A redirect to a new host asks again, before anything is
fetched from it:

```jsonl
{"type":"question","kind":"permission","tool":"fetch_url","target":"docs.python.org","outbound":true,"detail":"https://docs.python.org/3/library/os.html","questions":[{"header":"Permission","question":"Allow fetch_url to reach docs.python.org?","multi_select":false,"options":[{"label":"yes","description":"Allow this once"},{"label":"no","description":"Deny"},{"label":"always","description":"Allow, and add the website to tools.allowed_hosts"},{"label":"session","description":"Allow this website for the rest of this session"}]}]}
```

The answers are the same words: `session` allows that host for the rest of the
run, and `always` adds it to `tools.allowed_hosts`. With no driver, only the
listed hosts are reached.

## Everything else is a CLI command

There is no second protocol for mutations. A driving GUI shells out to the same
commands a person uses:

```bash
apogee config add-backend work --type anthropic --model claude-sonnet-5
apogee models
apogee check
```

The CLI **is** the parity contract, which is why machine mode replaces any
localhost control plane for local front-ends. Because the GUI performs its own
mutations, it already knows when it changed something and can re-read; there is
no push channel in v1.

A read a host renders takes `--output-format json` and prints **one JSON
document** of the same facts as the human view — never a stream, and stdout
carries nothing else (diagnostics to stderr, exit codes as ever). Today that is
the tasks:

```bash
apogee task status task-20261004-120000 --output-format json
apogee task list --output-format json        # {"object":"list","data":[…],"total":N}; --all for every one
```

`task status`'s document is the view above, byte for byte the body of `GET
/v1/admin/tasks/{id}`; `task list`'s is `GET /v1/admin/tasks`'s — `data` the
newest 50 (`id`, `status`, `rounds_used`, `rounds_budget`, `goal`), `total` every
task. A read given `stream-json`, or any word but `text` and `json`, is refused.

Since 27l the graph navigation verbs are reads of the same kind:

```bash
apogee graph path <from> <to> [--directed] [--relation calls] --output-format json   # {"object":"graph.path",…}
apogee graph explain <node> --output-format json                                       # {"object":"graph.node",…}
apogee graph neighbors <node> [--relation R] [--direction in|out] --output-format json # {"object":"graph.neighbors",…}
apogee graph query "<question>" --output-format json                                   # {"object":"graph.query",…}
```

Each takes `--graph <name>` or `--collection <name>` (neither: the one graph
built). Each document is byte for byte what the `graph` tools return — to a
model in a turn, and to any MCP client of `apogee __mcp-tools` — and what `GET
/v1/admin/graph/{id}/path|explain|neighbors|query` serves; the shapes are in
[http-api.md](http-api.md#get-v1admingraphidpath). A node that cannot be
resolved, or a name several nodes answer to, exits `1` with the message — the
candidates named — on stderr and nothing on stdout.

Since 27m `graph report` is one more, with the same selection:

```bash
apogee graph report --output-format json                                              # {"object":"graph.report",…}
```

The document holds the facts the Markdown report renders, assembled from the
store with no model call: `overview` (entity and relation counts, the kinds,
the members, the parsed files per language, the unresolved names), `origin`
(`extracted`/`inferred`), `hubs` (`metric: "degree"`, how many were `ranked`, and
the `shown` ten, each a node, its `degree` and its relation groups as `graph
neighbors` carries them), `communities` (`total`, `unsummarised`, the ten largest —
`summary` absent on one clustered with no model), `links` (only for a named graph
of two or more members: member `pairs`, relations `crossings` between them,
entities `shared` by several), `decisions` and `orphans` (each a `total` and the
`shown` ten), and `human_summary` — one paragraph of the same facts. `--out <file>`
writes the document there instead.

## What machine mode does not do

- **It opens no sockets.** Not one, ever — asserted in CI with `lsof`. Only
  `apogee serve` owns a port.
- **It does not push.** Events arrive in response to turns, never unprompted —
  and a task's events only on the stdout of the `task run` or `task resume`
  that runs it: a `chat` child never narrates a task it did not start.
- **It does not represent slash commands.** `/model`, `/compact` and the rest are
  terminal-REPL affordances; a driver uses the CLI commands and its own UI.
