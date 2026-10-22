# Machine mode: the JSONL protocol

Apogee's contract with a front-end. A GUI powers Apogee by **spawning the
executable and speaking over its pipes** — never over localhost. `serve` remains
the surface for genuine server deployments, where a remote client makes REST
calls to Apogee running on a host; a local front-end uses this instead.

This document is the reference for that protocol. The worked example is
[`reference_driver.py`](../../../src/cli/tests/scripts/py/reference_driver.py), which is also
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

```bash
apogee execute --suite research --output-format stream-json
```

`--output-format` selects the event stream on stdout. `--input-format` selects
JSONL user turns on stdin, which is what makes one `chat` child serve a whole
conversation. It defaults to whatever `--output-format` is, so a driver may pass
one flag; on `chat` the two directions **must agree**, and disagreeing is an
error rather than a silently ignored flag. `execute` (27s) is chat's session
opened with a suite -- or, since 41a, bare on one model -- and takes both flags
as chat does (see
[Playing a symphony](#playing-a-symphony-execute)). `task run` and `task resume` take
`--output-format` alone: a task reads nothing from its driver (see
[A task's run](#a-tasks-run)).
Since 52b `models pull`, `video generate`, `video compose` and `voice say`
take `--output-format stream-json` too: their progress as events, their
document last (see [A long job's run](#a-long-jobs-run)).

## Reading the stream

One JSON object per line on stdout. Every object has a `type`.

```jsonl
{"type":"session","protocol_version":1,"model":"claude-sonnet-5","next_turn":1,"capabilities":{"events":["session","thinking","…"],"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"tools":true,"ask":true,"schema":"2026-10-20"}}
{"type":"thinking"}
{"type":"thinking_delta","text":"…"}
{"type":"memory","chats":1,"decisions":1,"sessions":[{"id":"20261019-142233-a1b2c3","name":"asyncio timeouts in the scraper","updated":"2026-10-19T14:40:02Z"}]}
{"type":"sources","sources":[{"file":"report.pdf","pages":[41,42],"score":0.81,"retriever":"vector"},{"file":"notes.md","lines":[12,40],"score":0.4,"retriever":"lexical"}]}
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
| `session` | Once, first, unprompted. Names the protocol version, the model and the session's `capabilities` (below); `"private": true` for a chat that writes nothing (52d, [below](#a-private-chat-chat---private)). |
| `thinking` | The model began reasoning. No text. Sent again with `"budget_reached": true` when the reasoning reached its thinking budget and was ended there (`--think-budget`, an `--effort` level's budget, or the backend's `thinking_budget`). Machine mode carries the reasoning as it streams whatever `ui.thinking_display` says: the terminal's and the shell's one-line summary (43) is theirs alone, and is never asked for here. |
| `thinking_delta` | A chunk of reasoning. **Droppable** — see below. |
| `memory` | `chat` only: what a turn was handed from earlier conversations -- `chats`, past chats' summaries, and `decisions`, recorded knowledge records -- injected for this turn and never into the transcript (26l). Sent before the turn, only when it recalled something. Since 52a `sessions` names each past chat recalled, in the order sent: its `id` (what `chats list` and `--resume` take), and the `name` and `updated` time the recall index recorded when it summarised the chat -- what the model was handed, so a later rename shows only once the chat is summarised again. Absent when `chats` is 0. |
| `sources` | The excerpts retrieval sent the model this turn (52a), before the answer: `sources`, each `{file, pages?, lines?, times?, score, retriever}` in the order sent -- the chat's attachments' excerpts first, then a collection's. `file` is the file as its index names it (an attachment's file name, a collection's source path); `pages`, `lines` and `times` (seconds, for a recording) are `[first, last]` when the excerpt's chunks record them. **What was sent, never what was found**: an excerpt the context budget trimmed is not here, and with nothing sent there is no event. `score` is on its `retriever`'s scale -- `lexical`, `vector` or `hybrid` scores do not compare. Number them in order to draw citations. Past chats' recall is `memory`'s, not this. |
| `tool_status` | A tool is running, described in `text` for display -- or another model call the turn makes besides the chat model's own (the embedder, the utility model, the rerank judge, the knowledge clerk, a suite member the model consults), as `<role> — <what it is doing>` (26n). A `consult` (27f) is an ordinary tool call: `[tool] consult`, then `consult — asking utility (<backend>): <the question's first words>` while the member answers, and the answer reaches the model as the tool's result. A suite's verifier checking a tool call or an answer (27g) is said the same way: `validate — asking utility (<backend>): Check …`. A symphony the root model plays on its own (27t, an orchestrating `execute` session) is an ordinary tool call too: `[tool] play_<symphony>`, then `play — the model chose <symphony>: <n> member calls, <roles>`, then a line per stage. A tool the **vendor** ran on its own servers (47a: an entry's `server_tools:`, or the run's `--search`) is said here too, never dispatched: `<tool>, run by <vendor>: <what>` -- `web_search, run by Anthropic: "apogee harness"`, then `web_search, run by Anthropic: 5 results` -- and since it can fall between two parts of an answer, the answer's events end before it and start again after. |
| `notice` | A line for the user in `text` that is neither progress nor an error — a local model answering without the tools it was given because its chat template cannot take them, or a reply kept as text because it did not match the template's format, or a request trimmed to fit the model's window (`context budget: 2 earlier exchanges not sent`), or what a suite's verifier said (27g): an objection to a tool call returned to the model, a dispute the call runs over, the two positions on an answer under `answers: always` (`validate: …`), a check that could not be made. Show it and keep it; it never ends the turn. |
| `todo` | The session's todo, whole (41d): `items`, an array of `{text, state}` -- `state` `pending`, `active` (being done now) or `done` -- in the model's order, empty when it cleared it. Sent each time the model rewrites it with `todo_write` (a session with tools), and once as a resumed session opens, right after `session`, when it was left holding one. Draw it in place of the last: it is the plan, not a log. |
| `answer_start` / `answer_end` | Bracket one answer's deltas. |
| `answer_delta` | A chunk of answer text. Concatenate in order. |
| `result` | Ends a turn. Carries the whole answer, so a driver that dropped every delta still has it. `usage` is **absent** when the provider reported none — absent is not zero. A turn ended by a `cancel` ends here too, with `finish_reason: "cancelled"` and the text streamed before it. |
| `question` | `ask_user`. Expects a reply; see below. |
| `error` | A turn failed — it ends the turn in place of a `result`. The machine-readable half of a diagnostic. |

### `capabilities`

What this session can do, on its `session` event before the first turn — so a
host need not wait for a turn, or guess from a version number, to know what
it is talking to:

| Field | Meaning |
|---|---|
| `capabilities.events` | Every event type this build can write. A stream never carries one that is not here. |
| `capabilities.accepts` | The line types this session reads on stdin: a driven `chat` or `execute` takes `user`, `answer`, `attach`, `hello` and `cancel`, and since 52d `detach`, `set` and `compact`; `complete` reading its prompt on stdin takes only a `hello` before it; a `task run` and an agent's run read nothing. |
| `capabilities.tools` | Whether the model can call tools in this session. |
| `capabilities.ask` | Whether `ask_user` and the permission prompt reach the driver as `question` events. Without it nothing is asked: `ask` resolves to deny and the tool is never offered. |
| `capabilities.schema` | The vocabulary's version, a date: moved when a release grows the vocabulary (a new type, a new field, a new inbound line). Two builds with the same `schema` speak the same vocabulary. |

It names what the binary can do and nothing about this machine — never a key,
a path into the data directory or a config value. Like every field, more may
be added.

### Turns: `turn` and `next_turn`

In a driven `chat` or `execute`, each accepted `user` line is a **turn**,
numbered from 1, and every event that belongs to it carries `"turn": N` —
`memory`, `thinking*`, `tool_status`, `notice`, the answer's events,
`question`, and the `result` or `error` that ends it — so a host can attribute
any event to the line that caused it by number alone, after a race or reading a
log. The `session` event carries `next_turn`: the number the next `user` line
will get — past the turns a resumed conversation (`--resume`, `-c`) already
holds — and itself never carries `turn`; neither does anything said between
turns (an `attach`'s notices). **The turn accounting:** every accepted `user`
line ends in exactly one `result` or one `error`, cancelled or not, so N lines
sent are N turns closed. A `task run`'s events carry no `turn`: its turns have
no `user` line.

### Three rules a driver must follow

**1. Ignore unknown types — and unknown fields.** New event types, and new
fields on the events a driver already reads, are added *without* a version
bump, because drivers are required to tolerate them — that is what lets the
schema grow. A driver that treats an unfamiliar `type`, or a key it does not
know on a familiar one, as an error breaks on upgrade.

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

### The stability promise

What a host built against `protocol_version: 1` can rely on, for as long as a
binary says `1`:

- **Nothing it reads changes under it.** Every event type documented here keeps
  its name and its meaning; every field keeps its name, its type and its
  meaning; a field documented as sometimes absent (`usage`) stays absent rather
  than turning up as a zero.
- **The stream's shape holds.** stdout carries one JSON object per line and
  nothing else; `session` comes first, unprompted, before anything is read;
  every accepted `user` line ends in exactly one `result` or one `error`;
  `question` is the only event that waits for a reply.
- **Growth is by addition only** — new event types, new fields on existing
  events, new inbound line types, new values a host can ignore — and each moves
  `capabilities.schema`. Rule 1 is what makes that safe for a host; the child
  keeps the same rule inbound: a line it does not recognise is ignored, never
  fatal, so a host may send something newer than the binary understands (a
  `hello` to a binary that predates it is simply ignored).
- **A breaking change is a new `protocol_version`.** Removing an event or a
  field, renaming one, or changing what one means is never done under `1`. It
  is announced in the release notes of the release that makes it, and that
  release's `session` says the new number — so a host that checks
  `protocol_version` can refuse a stream it was not built for rather than
  misread it.

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

## A long job's run

Since 52b the four verbs that run for minutes — `models pull`, `video
generate`, `video compose` and `voice say` — take `--output-format
stream-json`: the progress each already narrates for people, as one JSON
object a line on stdout, and the verb's own document last. It is a relay,
never a second narrator: the chain's steps, the download's counts and the
drivers' events are the same producers the busy line and the `[i/n]` lines
read, framed as events instead. Every line a verb prints for people on stdout
reaches the stream as a `notice` (blank lines dropped); stderr keeps the
diagnostics, as ever. Without the flag every verb prints exactly what it did
before.

```jsonl
{"type":"job","verb":"models pull","protocol_version":1,"schema":"2026-10-20"}
{"type":"notice","text":"pulling org/model, then converting it to F16 and registering it"}
{"type":"stage","index":1,"count":3,"label":"pull org/model's full weights","state":"started"}
{"type":"bytes","done":67108864,"total":4971201024,"file":"model-00001-of-00002.safetensors","index":1,"count":4}
{"type":"stage","index":1,"count":3,"label":"pull org/model's full weights","state":"done"}
…
{"type":"document","document":{"name":"org--model/safetensors/1a2b3c4d5e6f","fields":[…]}}
{"type":"document","document":{"name":"org--model/gguf/9f8e7d6c5b4a","fields":[…]}}
```

```jsonl
{"type":"job","verb":"video compose","protocol_version":1,"schema":"2026-10-20"}
{"type":"notice","text":"plan: \"Tides\" -- 2 scenes, about 5.5 s of video"}
{"type":"scene","scene":1,"scenes":2,"state":"speaking"}
{"type":"ready","device":"mps","scene":1}
{"type":"segment","index":1,"of":1,"seconds":2.4,"scene":1}
…
{"type":"scene","scene":1,"scenes":2,"state":"rendering"}
{"type":"ready","device":"mps","dtype":"bfloat16","mode":"t2v","scene":1}
{"type":"step","step":1,"steps":50,"scene":1}
…
{"type":"frames","frames":73,"width":1280,"height":704,"scene":1}
{"type":"assembling","frames":73,"scene":1}
…
{"type":"document","document":{"object":"video.project","status":"done",…}}
```

| Event | Meaning |
|---|---|
| `job` | First: the `verb`, `protocol_version` and `schema` — the same versions a session's announce. |
| `stage` | A step of `models pull`'s chain (`--safetensors --register[-with]`): `index` of `count`, its `label` in the chain's words, `state` `started`, `done` or `failed`. A failed stage is followed by the chain's own `notice`s saying where it stopped and the one command that resumes it. |
| `bytes` | Bytes a pull has moved — a download, a copy from the Ollama store, a hash: `done`, and `total` **only when known** (never guessed). In a tree of files (a snapshot, an MLX model, a pipeline, a voice model), the `file` moving and its `index` of `count`. Sent at each file's start, at each whole percent of a known total (each 16 MiB of an unknown one), and at its end. |
| `ready` | A runtime loaded: `device`, and `dtype` and `mode` (`t2v`, `i2v`) when the driver says them. |
| `step` | A denoising step done: `step` of `steps`. |
| `frames` | A clip's frames written: `frames`, `width`, `height`. |
| `assembling` | Those `frames` going into the clip. |
| `segment` | Speech made: segment `index` of `of`, its `seconds` as the driver counted. |
| `scene` | A composition's scene begun: `scene` of `scenes`, `state` `speaking` (its narration), `rendering` (its clip) or `asset` (its asset taken as it is). Every `ready`, `step`, `frames`, `assembling` and `segment` inside a composition carries the `scene` it belongs to; outside one they carry none. |
| `document` | Last on success: the verb's own document, whole. For `video generate`, `video compose` and `voice say` it is exactly what `--output-format json` prints (the record beside the output). For `models pull`, which has no document of its own, one `document` per set of weights the run stored, in the order stored, each exactly what `apogee models info <model>/<format>/<id> --output-format json` prints for it. |

A failure ends the stream with an `error` event, its `message` the words stderr
gets; a media run that wrote its document anyway (`status` `failed` or
`cancelled`, as `--output-format json` prints it) sends that `document` first.
Ctrl-C exits `130` with the stream ending cleanly on its `error`, every line
whole. `models pull` takes `text` or `stream-json` only; the three media verbs
take `text`, `json` or `stream-json`.

## Validating and generating: the schema

The vocabulary above is also a machine-readable artifact: a JSON Schema (draft
2020-12) that the binary prints for itself —

```bash
apogee __machine-schema > machine-schema.json
```

— and that every release archive carries beside the binary as
`machine-schema.json`, byte for byte what that build prints. The command is the
truth; the copy is a convenience. Validate a stream against it, or generate a
client's types from it, instead of transcribing this page:

- **The root validates one line Apogee writes on stdout**; `#/$defs/inbound`
  validates one line a host writes on stdin. Each event type has its own
  definition, `#/$defs/event_<type>` (`event_result`), and each inbound line
  `#/$defs/line_<type>` (`line_user`).
- **It states the stability promise rather than contradicting it**: an unknown
  `type` validates (only a known type is held to its definition), every
  definition admits fields it does not name, and a value that may grow — a
  `finish_reason`, a `kind` — is a string, never a closed list. A validator
  built from it accepts exactly what rule 1 tells a host to accept.
- **Optional is honest**: a field that may be absent — `usage`, a permission
  question's `kind`, every event's `turn` outside a driven chat — is not
  required, so generated types make it optional rather than zero.
- `x-apogee.schema` is the same date `capabilities.schema` announces; a host
  can compare the two to know its generated types match the binary it spawned.

`apogee __machine-schema` is hidden from `--help`, like `__complete`: a
protocol, not a feature. Its stdout carries the schema and nothing else.

## Writing to the child

One JSON object per line on stdin.

```jsonl
{"type":"hello","client":{"name":"my-host","version":"1.2.0"},"wants":["tools"]}
{"type":"user","text":"what is 2+2?"}
{"type":"cancel"}
{"type":"answer","text":"Yes"}
{"type":"attach","path":"report.pdf"}
{"type":"attach","path":"src","graph":"off"}
{"type":"detach","path":"report.pdf"}
{"type":"set","model":"claude","think":"off","temperature":0.2}
{"type":"compact"}
```

An unrecognised line is ignored rather than fatal — the tolerance this protocol
asks of drivers, honoured in the other direction.

### Stopping a turn: `cancel`

`{"type":"cancel"}` ends the turn in flight the way Ctrl-C ends a turn at the
terminal — a host's Stop button. The model call is abandoned, the half-turn is
rolled back, and the session is left as the last finished turn left it: the
cancelled line and anything said for it never reach the saved conversation or
a later request. The turn still ends in one `result` — `finish_reason:
"cancelled"`, the text streamed before the cancel — and the same child answers
the next `user` line. A `cancel` reaches the turn of the last `user` line sent
that has not ended, even one sent a moment before that has not started; with
no turn open it is ignored, so a cancel racing a turn that just finished is
harmless. While a `question` or a permission prompt waits, a `cancel` fails the
turn as closing stdin would — without ending the session. Turns are serial, so
`cancel` names no turn. `complete` has no `cancel`: it is one-shot, and a host
stops it by ending the process.

### Introducing yourself: `hello`

A host may send one optional `hello` as its **first** line: its `client` name
and version, and what it `wants`, a list of words. It is recorded in Apogee's
operational log for diagnostics and changes nothing in this release — the
session has already announced its `capabilities`, because the child speaks
first and never waits for a `hello` (a driver that predates it would wait
forever). A `hello` anywhere but the first line is ignored with a note on
stderr, never an error. `complete --output-format stream-json` reading its
prompt on stdin takes a `hello` as that input's first line, and the rest is the
prompt.

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

Whether a folder of code is graphed is the attach's method, `code` or `off`. An
`attach` line may carry its own, `"graph":"code"` or `"graph":"off"`, as `/attach
<path> --graph=off` does at a terminal; without one, the config's
`attachments.graph` decides, and without that, a chat builds the graph. A method the
default did not choose is named at the end of the attach's first `notice` --
`attaching src (4 files, 1.2 KB) -- without its code graph (--graph=off)`, or
`(attachments.graph: off in the config)` -- and `off` indexes the folder's chunks
alone, with no `graph:` notice (an earlier graph of the folder is forgotten). Any
other value attaches nothing, and a `notice` says why: `src not attached: graph:
unknown value 'tree' (accepted: code, off)`. A child started with `chat --attach
<path> --graph off` applies it to those attaches; `complete --attach <path>
--graph=code` builds the graph a one-shot otherwise skips. A `user` message's
`@path` mention is a bare path -- anything after it is the message's -- so it takes
the default.

While a folder's graph is attached, each turn's retrieval walks it from the
excerpts it found: the turn's attachments `notice` then ends with the count,
`3 excerpts from the attachments, strong match (0.912 [lexical], 2 of 2 question
words) +5 graph entities`. And a chat started with `--tools` offers the model the
four graph tools -- `graph_query`, `graph_path`, `graph_explain`,
`graph_neighbors` -- reading that chat's own graph: ordinary, never-gated tool
calls whose results are the documents `apogee graph <verb> --output-format json`
prints for the same tree, named `"graph": "attachments"`. Detached, the tools read
the configured graphs again.

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

### The session's own doors: `detach`, `set`, `compact`

Since 52d a driven `chat` or `execute` takes three lines that reach exactly
what the session's slash commands reach — never a second implementation —
each answered by `notice`s and effective from the next turn. Sent while a
turn runs, a line waits for it to end, as an `attach` does.

- **`{"type":"detach","path":…}`** detaches what was attached under that
  name (the `path` the `attach` line gave), as `/detach` does: `notice`
  `detached <name>`, or `nothing attached as '<name>' -- /attachments lists
  them`.
- **`{"type":"set",…}`** moves the run settings the slash commands move —
  `model` (a backend, a roster model or `backend:model`, as `/model` and `-m`
  take it), `system` (the system prompt, replaced whole), `think` (`on`,
  `off`, `auto`), `effort` (a level or `auto`) and `temperature` (a number,
  or its text). Several in one line apply in that order — the model first,
  so thinking and effort are judged on the backend that will answer. Each is
  said as a `notice` ending `-- from the next turn` (`switched to claude --
  from the next turn`), or refused by name (`set effort: not an effort level:
  'loud' (…)`) while the rest still apply. **Any key that is no setting — or
  a value neither text nor a number — refuses the whole line by name, and
  nothing is set**: `set: 'max_tokens' is not a setting a session moves, as
  text -- model, system, think, effort or temperature; nothing was set`. A
  line with no setting says so. The permission mode is not among them: it is
  chosen at launch.
- **`{"type":"compact"}`** compacts the conversation now, as `/compact`
  does — the utility model, else the chat's own backend, folds the earlier
  exchanges into a summary — said as `history compacted by <backend>`. A
  saved chat is compacted with the verb `apogee chats compact <id>` (asked
  first at a terminal, `--yes` for a script).

### A private chat: `chat --private`

`chat --private` (and `execute --private`, the session's one opening)
writes **nothing** of the conversation: no transcript under `sessions/` —
not written and later deleted, never written — no recall summary and no
recall of earlier chats, no saved model state, no auto-capture, and its
attachments indexed in a folder of the run's own in the system's temporary
directory, removed as the run ends. The `session` event carries `"private":
true`, and a `notice` says it: `private: nothing of this chat is written --
no transcript, no recall`; the terminal and the shell say the same line at
the start. A saved chat cannot be resumed privately: `--private` with
`--resume` or `--continue` is refused.

### Playing a symphony (`execute`)

`apogee execute` (27s) is chat's session opened with a suite: the same lines in,
the same events out, a `user` line answered by the suite's root model as chat
answers it. One `user` line means more there: one whose text is a `/play`
command plays that symphony on the suite's members instead of asking the root.
Since 41a it also opens **bare** -- no suite named and none the default, on
`-m <backend>` or `models.default` -- exactly as chat opens: the same events,
the `session` event naming the model, and a `/play`'s stages resolved through
the one role chain (a helper role on the conversation's backend, the chat
role on its pointer). `--orchestrate` is a suite's, and refused bare.
Since 41b an execute session opened inside a **project** -- a folder holding a
`.git` or a context file -- opens with it: `AGENTS.md` (else `CLAUDE.md`, read and
never written) rides every request as system context, never the conversation's,
and the folder is attached as `--attach .` would attach it (`--attach ""` skips
that; any other `--attach` replaces it). Both are said in the events the session
already has: a `notice` -- `project context: AGENTS.md, 1,234 words, in every
request`, or the cut when the file passes 4,000 words -- before the folder's own
attach lines. Since 41c such a session also carries the shipped **coder persona** -- a
coding system prompt, ahead of the project's file -- unless it has a system
prompt of its own (`-s`), which replaces it; a `notice` says which (`persona:
coder, the shipped coding prompt, in every request -- -s replaces it`, or
`persona: your -s prompt, in place of the coder persona`).

Since 41e a session with tools may **delegate**: the model's `delegate` call runs
a bounded sub-session -- the same loop on a fresh conversation, with the
session's tools less `delegate` itself, under the session's own permissions --
and its answer comes back as that call's result. A driver sees it in the events
it already has: the delegation and each of its tool lines as `tool_status`
(`delegate — a sub-session on <model>: <brief>`, then `delegate — read_file …`),
and a permission prompt a delegated tool raises as an ordinary `question`,
answered the same way -- `always` included. Nothing of the sub-session is saved
but the result, in this session's record.

```jsonl
{"type":"user","text":"/play summarize-verify The minutes of the meeting…"}
```

It arrives as an ordinary turn, in events the protocol already has: a
`tool_status` as each stage starts, in a side call's shape (`stage 1/2 summarize
— asking utility (l3b): …`; a chain's stages name their position, `digest →
summarize-verify, stage 1/2 summarize`), then `answer_start`, the output in
`answer_delta`, `answer_end`, and the turn's `result` carrying the whole output.
The session keeps the play as one ordinary exchange — the line as typed, then
the output — so the next `user` line builds on it, and a resumed or compacted
session holds it like any other. A play that cannot run — no symphony of that
name, no input for one that reads it, one that takes an image (a play from a
line carries text alone), a stage refused or failed, the play's
`symphony_caps` budget spent — ends in an `error` event instead of a `result`,
the reason on stderr too, and nothing is kept. Every other line is what it is
in `chat`: a slash word other than `/play` is a prompt like any text. The
catalog is a read: `apogee symphonies list --output-format json`.

**The model's own plays** (27t). Launched with `--orchestrate` -- or under a
suite whose `orchestrate: true` says so -- an `execute` session offers its root
model each symphony it can play as an ordinary tool, `play_<symphony>`, and the
model may choose one on any turn. Nothing new crosses the wire: the choice is
a tool call, said as one -- `[tool] play_summarize-verify` in a `tool_status`,
then `play — the model chose summarize-verify: 2 member calls, utility → chat`,
then a `tool_status` per stage as for `/play` -- and the play's output reaches
the model as the tool's result, which it answers from in the turn's ordinary
`answer_*` events and `result`. A play the model starts runs on local,
unmetered members only, and spends from the turn's member-call budget with
its consults and checks; one the budget cannot finish is refused before its
first call -- a `notice` (`orchestrate: summarize-verify not played -- …`) --
and one that fails once started is `orchestrate: summarize-verify stopped -- …`;
either way a result the model reads, never silent. Without orchestration no `play_` tool exists.

### Answering a question

When the model calls `ask_user`, the child emits a `question` event and **blocks
until answered**:

```jsonl
{"type":"question","questions":[{"header":"Overwrite","question":"The file exists. Overwrite it?","multi_select":false,"options":[{"label":"Yes","description":"Replace the contents"},{"label":"No","description":"Leave it alone"}]}]}
```

Reply with one `{"type":"answer","text":"…"}` line per question, in order. The
offered options are a convenience, not a constraint — free text is always
accepted. Closing stdin with a question outstanding fails the turn, and the
half-turn is rolled out of history rather than persisted half-finished. A
`cancel` while it waits fails the turn the same way, and the session goes on
([Stopping a turn](#stopping-a-turn-cancel)).

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

`write_file` and `edit_file` carry the change itself (52a): `detail` is a
unified diff of the file as it is against what the call would leave, computed
from the very arguments the call runs with — `--- a/<path>` (`--- /dev/null`
for a new file), `+++ b/<path>`, `@@` hunks with three lines of context, the
path relative to the tools' root. A binary side is `Binary files a/<path> and
b/<path> differ`. Past **32 KB** the diff stops at a line, its last hunk's
header counting the lines it kept, and a last line says the cap and the whole
change's counts: `[diff cut at 32 KB: +1200 -45 lines in all]`. Count `+` and
`-` lines inside the hunks for a card's `+12 −3` (or read that last line);
`detail` is absent when the call cannot run as given (an `old_string` that is
not in the file) or the file is past 16 MB. The terminal and the shell show
the counts, not the diff.

```jsonl
{"type":"question","kind":"permission","tool":"edit_file","target":"src/app.py","detail":"--- a/src/app.py\n+++ b/src/app.py\n@@ -3,7 +3,7 @@\n import os\n \n def main():\n-    run(timeout=5)\n+    run(timeout=30)\n \n if __name__ == \"__main__\":\n     main()\n","questions":[…]}
```

**The session's permission mode** decides what is asked at all. It is set at
launch with `--permission-mode manual|accept-edits|plan|dont-ask|bypass`, the
same modes a terminal session moves between with `/permission-mode` and
Shift+Tab. No line moves it — `set` moves only the run settings — so a
driver chooses the mode when it starts the child.

- `manual`, the default, asks as above.
- `accept-edits` runs the file and note edits unasked.
- `plan` refuses every tool that writes, saying so in the tool result.
- `dont-ask` refuses whatever would have been a `question`. It is the mode for
  a driver that must never be asked.
- `bypass` asks nothing.

A config `deny` refuses in every mode. A driver chooses the mode at launch,
`bypass` included: in the terminal and the shell `/permission-mode bypass`
asks the person once, and a machine-mode child has nobody to ask that of.

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

Since 40c the task recipes — named goals with their checks baked in, which `task
run --recipe <name> [--input "…"]` expands — read the same way:

```bash
apogee task recipes --output-format json              # {"object":"list","data":[…],"problems":[…]}
apogee task recipes show <name> --output-format json  # {"object":"task.recipe",…}
```

Each recipe is `name`, `description`, `source` (`shipped` — its compiled-in
text, `seeded` — its file under the data directory's `recipes/`, as shipped,
or `user` — a file of the user's, or a starter's edited), `path` (`null` for
`shipped`), `input` (what `--input` is for; `""` when the goal reads none),
`tools`, `require` and `require_files` (the checks), `allow` (the grants it
SUGGESTS — a run asks the person at the terminal for each and, with nobody
there, gives none the line did not), `agent` and `rounds`; `show` adds `goal`,
the template. `problems` names a file under `recipes/` that did not parse. A
recipe's run is a task run like any other — the same events, ledger and view
— its ledger carrying the recipe's name as `recipe`.

Since 48b the review skills — the lenses a review runs, each a markdown file —
read the same way:

```bash
apogee review skills list --output-format json         # {"object":"list","data":[…],"problems":[…]}
apogee review skills show <name> --output-format json  # {"object":"review.skill",…}
```

Each skill is `name`, `description`, `source` (`shipped` — its compiled-in
text, `seeded` — its file under the data directory's `review-skills/`, as
shipped, or `user` — a file of the user's, or a shipped one edited), `path`
(absent for `shipped`), `applies` (`extensions`, `paths` — globs over
repository paths — and `languages`, the hints that decide which changed files
a skill applies to) and `everywhere` (no hints: it applies to every file);
`show` adds `prompt`, the hunt prompt. `problems` names a file under
`review-skills/` that did not parse. Every skill's answer is held to one
finding contract, the shipped `code-review` agent's findings: `severity`,
`kind`, `file`, `location`, `claim`, `suggested_fix`, `confidence`, and an
empty list a complete answer.

Since 48c a review's run is a document too, whether or not it posted:

```bash
apogee review mr <ref> --output-format json   # {"object":"review.run",…}
```

`ref` is a merge request's URL, `owner/repo#12` (GitHub) or `group/project!4`
(GitLab). The document is `merge_request` (the canonical spelling,
`github.com/owner/repo#12`), `url`, `title`, `head` (the commit reviewed),
`mode` (`full`; `incremental` when this login reviewed an earlier head, with
`since` that head; `already reviewed` when it read this head before), `model`
(the backend that reviewed), `skills` (the lenses run) with `plan` (`named`,
`hints`, `hints, narrowed`) and `left_out_skills`, `excluded_files` (the
exclusion list's and binary files), `chunks`, and `findings` -- each the
finding contract's fields plus `line` (where its comment anchors),
`identity`, `found_by` (every lens that found it), `tightened` (the confirm
pass reworded it) and `inline` (`false` past the cap, listed in the summary
instead). `counts` says what each step left out: `candidates` (what the hunts
returned), `off_change` (not on a line the change added), `not_new` (not on a
line the new push added), `known` (posted before), `duplicates`, `dismissed`
and `tightened` (the confirm pass). `posted` is `{"done": true, "comments",
"summary", "batched"}`, or `{"done": false, "reason"}` -- nothing to say,
`--dry-run`, or the gate's refusal (`nobody to ask -- run with --yes to post,
or --dry-run`). `notes` says what a pass that did not answer cost. The
narration (`[review] hunting with security`) goes to stderr; stdout carries
the document alone. Posting goes through the gate as `review_post`: asked on
a terminal, allowed by `--yes` or `permissions.review_post: allow`, governed
by `--permission-mode`.

Since 48d the sweep and its targets read the same way:

```bash
apogee review targets --output-format json   # {"object":"list","data":[{"name","forge","repo"|"owner",…}]}
apogee review sweep --output-format json     # {"object":"review.sweep",…}
```

A target is `name`, `forge`, `repo` or `owner`, `host` when it is not the
forge's default, `skills` (empty: each merge request's plan decides), `post`
(its declared grant), `drafts`, and `model` and `cli` when set -- the `review:`
section's entry. A sweep's document is `started`, `dry_run`, `allow_post`,
`cap`, and per target its `name`, `kind` (`repo`, `owner`), `grant` (`target`,
`--allow-post`, `none`) and `projects`: each repository's `project`
(`github.com/owner/repo`), `watermark_before` and `watermark_after`,
`first_sweep` (it started at now), `reviewed` (each a `review.run` document,
above), `skipped_drafts`, and `error` when it stopped -- a merge request that
could not be reviewed is not advanced past, so the next sweep tries it again.
`reviewed`, `posted`, `deferred` (past the cap, waiting for the next sweep)
and `errors` count the whole run, and `document` names where it was kept
(the data directory's `review/sweeps/`). A sweep with errors exits `1`; one a
live sweep's lock refuses exits `1` naming its pid.

Since 49c a generated clip's record is a document, whether or not it landed:

```bash
apogee video generate "<prompt>" -o clip.mp4 --output-format json   # {"object":"video.generation",…}
```

It is `status` (`done`, `failed`, `cancelled`), `output` (the clip, absolute)
and `sidecar` (where the record was written beside it -- `<clip>.json` -- or
null when nothing landed); `model` (`name` as it was given, `path`, `class`
from its `model_index.json`, `ref` and `revision` from its pull record,
`variant` or null); `prompt`; `image` (the seed image, or null) and `mode`
(`t2v`, `i2v`); `seed` and `seed_source` (`given` by `--seed`, `held` from a
record run again, or `generated` and kept); `settings` (`width`, `height`, `duration`, `fps`,
`frames` -- the duration at the rate, rounded to the pipelines' `4k+1` --
`steps`, `guidance` or null); `device` (`device` and `dtype` as torch
reported them, `platform` this build's target); `versions` (`torch`,
`diffusers`, `apogee` -- the ones that ran); `codec` (`h264`, `mpeg4`, or
null); `seconds`; `created_at`; since 49d `parent` -- null for a run of its
own, else `move` (`from`: a record run again; `extend`: a clip continued),
`document` (the record read, or the continued clip's record), `clip` (the
clip that record is beside, or the clip continued) and `continued` (the clip
whose last frame the run started from, or null) -- and `preset` (`draft`,
`final`, or null); since 49e `loras` -- each LoRA the run applied, in the
order given: `path` (the `.safetensors` file), `sha256` (the file as the run
began, what a `--from` rerun verifies it against) and `scale` -- empty for
none, with `versions` then naming `peft` too; and `error` when it did not
land. The clip and its sidecar are written only on `done`; a failed run exits `1` and a
cancelled one `130`, the output path untouched either way. The narration --
the device and the scale to expect, then each step -- goes to stderr. A run
refused before it starts (a cap, an existing output, no pipeline, a runtime
not installed) prints no document: one line on stderr naming the fix, exit
`1`. A run from a record (`--from`) or continuing a clip (`--extend`) says
first, on stderr, what came from the record and what the flags changed.

Since 50a spoken text is a document too, whether or not it landed:

```bash
apogee voice say "<text>" -o hello.wav --output-format json   # {"object":"voice.speech",…}
```

It is `status` (`done`, `failed`, `cancelled`), `output` and `sidecar`
(`<file>.json`, or null when nothing landed); `model` (`name` as given,
`path`, `ref`, `revision`, `checkpoint`); `voice` and its `language` (by
the voice's first letter, Kokoro's convention); `speed`; `text`;
`segments` -- each its `text`, `seconds` (**measured from the WAV's own
header**, never estimated) and `offset`, and its `sentences` (`text`,
`seconds` counted from the samples spoken, `offset`); `seconds` and
`sample_rate` for the whole file; `device` (`device` as torch reported it,
`platform`); `versions` (`torch`, `kokoro`, `apogee`); `run_seconds`;
`created_at`; and `error` when it did not land. Exit `1` on a failure, `130`
on a cancel, the output untouched either way; a refusal before the run
prints no document, one line on stderr naming the fix.

Since 51 a bootstrap's summary is one:

```bash
apogee bootstrap --output-format json   # {"object":"apogee.bootstrap",…}
```

It is `minimal` (and `minimal_by`: `--minimal` or `APOGEE_MINIMAL_INSTALL`,
null when it ran) and `steps` -- each `name`, `command` (the `apogee …` line
it ran, null for a step this host is not for), `status` (`done` or
`skipped`) and `reason` (null when done). The narration goes to stderr;
the exit is `0` whatever was skipped. Since 2026-10-21 the steps include
`the code-execution server` (`apogee mcp create code --from code-exec`) and,
last, `ffmpeg with captions`, whose `command` is another program's line as
typed -- `brew install ffmpeg-full && brew unlink ffmpeg; brew link --force
ffmpeg-full` on a Mac with Homebrew whose ffmpeg cannot burn captions, null
where it is skipped.

Since 50c a composed video is a document too -- its project, kept beside the
video as `<video>.json` from the first step and printed when the run ends:

```bash
apogee video compose "<brief>" -o video.mp4 --output-format json   # {"object":"video.project",…}
```

It is `version` (1), `status` (`rendering` while it runs, then `done`,
`failed` or `cancelled`; `planned` for a dry run's), `brief`, `title`, `output` and `scenes_dir`
(`<stem>.scenes/` beside the video); `settings` (`model` and `voice_model`
as store handles, `voice`, `crossfade`, `captions`, `music` and `assets`,
null when not given); `plan` (`backend`, and the `attempts` it took: 1, or
2 after its one correction); `scenes` -- each its `index`, `narration`,
`visual`, `seconds` (the plan's), `asset` (null for none), `seed`, `voice`
(`file`, `seconds` measured from the WAV, its `sentences` with `seconds` and
`offset`) and `clip` (`file`, `seconds` as ffprobe read them), each null
until it exists, and each file with its own record beside it
(`video.generation`, `voice.speech`); `captions` (`file`, and `burned` --
false when this ffmpeg has no libass and the SRT is kept beside the video);
`assembly` (the joins, the mix and the burn in words); `error`; `created`
and `platform`; and since 50d `parent` (the project it was edited from, null
for an original) and `edits` -- every edit from the original brief to this
one, each its `instruction` (null for none), the scenes it re-rolled
(`redo`) and the project it was made `from`; and since 52c, in an edited
project only, each scene's `from` (the parent's scene it came from, or
`"new"`), `changes` (which of its records differ from that scene's, in the
diff's order: `narration`, `prompt`, `duration`, `asset`, `seed` -- empty
when untouched) and `moved` (it left the parent's order), and the project's
`removed` (the parent's scenes the edit left out). Read off the edit's own
record comparison, they say exactly what the diff on stderr says, case for
case. A project no edit made carries none of them, as before.

`--dry-run` plans on paper (52c): on a brief, the one planning call alone --
the plan and its spend said on stderr, the project written beside the video
with `status` `planned` and printed, nothing spoken, rendered or kept under
`scenes_dir`. The same command without `--dry-run` renders that plan as
kept, never planning again (`rendering the plan a dry run kept at …`); a dry
run over a plan already kept makes no call at all. An edit is the same verb:

```bash
apogee video compose --from video.mp4 --edit "<instruction>" -o edited.mp4 --output-format json
```

Its scene diff is said on stderr before anything renders; `--dry-run`
prints the edited project with `status` `planned`, its scenes' `from` and
`changes` the diff as data, and renders and writes nothing. Exit `1` on a failure and `130` on a cancel, every finished
scene kept either way: the same command again resumes it, rendering only
what the document does not hold. A refusal before the plan prints no
document, one line on stderr naming the fix.

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

Since 27q the symphonies — named, staged prompt processes over a suite's
members — read the same way, and a play prints one document too:

```bash
apogee symphonies list --output-format json                  # {"object":"list","data":[…],"problems":[…]}
apogee symphonies show <name> --output-format json           # {"object":"symphony",…}
apogee symphonies play <name> --input "…" --output-format json   # {"object":"symphony.play",…}
```

`list` and `show` are byte for byte what `GET /v1/admin/symphonies[/{id}]`
serves; their shape is in
[http-api.md](http-api.md#get-v1adminsymphonies). A play's document is
`symphony` (its name), `suite` (the active suite's, `null` for none), `output`
(the last stage's answer) and `stages` — each `name`, `role`, `backend` (the one
that answered), `answer`, `cut` (it reached its cap), `tokens` when the provider
said, and `seconds`. Since 27r a stage may play another symphony: its entry
in `stages` is `name`, `play` (the symphony it played), `answer` (that
symphony's output), `cut`, `tokens` when any of its stages said, `seconds`,
and `stages` — the played symphony's own, in this same shape, as deep as the
play went. A play is not a turn and emits no events: a host drives it as a
command, the input as `--input` or on stdin. A play that stops — a stage
refused or failed, an answer outside its stage's schema, the whole walk's
budget (`symphony_caps`) spent — prints nothing on stdout and exits `1`
(refused, or the budget) or `2` (a member's failure), the stage named on
stderr with its position (`outer → inner, stage 2/3 verify (chat): …`).

Since 28h the listings a host UI renders are reads of the same kind:

```bash
apogee models list --output-format json     # {"object":"list","data":[…],"folded":N}; --all for the folded ones too
apogee models info <backend> --output-format json    # {"name":…,"fields":[{"field":"backend","value":…},…]}
apogee models status --output-format json            # {"fields":[{"field":"chat","value":…},…]}
apogee chats list --output-format json      # {"object":"list","data":[{"id","updated","turns","name","backend","model"},…]}
apogee agents list --output-format json     # byte for byte GET /v1/admin/agents
apogee mcp list --output-format json        # GET /v1/admin/mcp-servers's entries, with what connecting found
apogee mcp presets --output-format json     # {"object":"list","data":[{"name","description","provides","registers_into_apogee","command","args","runnable",…}]}
apogee check --output-format json           # {"rows":[{"section","name","status","detail","remedy"?}],"ok":…,"failures":N,"warnings":N}
apogee providers scan --output-format json  # {"object":"list","data":[{"provider","type","label","tier","backend","evidence":[…]}]}
apogee system --output-format json          # {"cpu":{…},"memory":{…},"apogee":{…},"gpu":{…},"disk":{…}}
apogee knowledge list --output-format json  # {"object":"list","db":…,"data":[records]}
apogee knowledge info <id> --output-format json      # the record, as --json prints it
apogee embed list --output-format json      # {"object":"list","data":[{"name","chunks","sources","error"?}]}
apogee embed info <name> --output-format json        # {"collection","path","schema","chunks","vectors":{…}|null,"index","sources":[…]}
apogee graph stats <name> --output-format json       # {"object":"graph.stats","graph","kind","built",…}
apogee graph show <name> <entity> --output-format json   # {"object":"graph.show","match",…,"data":[entities]}
apogee train status --output-format json    # byte for byte GET /v1/admin/training/status
apogee train versions [backend] --output-format json   # byte for byte GET /v1/admin/training/versions
apogee datasets list --output-format json   # byte for byte GET /v1/admin/datasets
apogee datasets info <name> --output-format json       # byte for byte GET /v1/admin/datasets/{name}
apogee datasets kits --output-format json   # byte for byte GET /v1/admin/datasets/kits
apogee train regime list --output-format json        # {"object":"list","data":[…],"problems":[…]}
apogee train regime show <name> --output-format json # {"object":"training.regime",…}
apogee auth list --output-format json       # byte for byte GET /v1/admin/auth: metadata only
```

- **`models list`**: one object per row the table shows — `backend`,
  `backend_type`, `model`, `roles`, `source`, `architecture`, `profile`,
  `state`, `verified`, and `note`, `format`, `quant`, `window` where the row has
  them — the very object each line of `--output-format stream-json` carries
  (which stays, a row per line). A snapshot a conversion consumed is folded as
  the table folds it, and `folded` counts them. Since 49a a stored diffusion
  pipeline is a row of its own (`format` and `group` `pipeline`, `state`
  `pipeline` or `cannot load`, `architecture` its pipeline class) carrying two
  numbers no other row has: `components`, how many the pipeline's
  `model_index.json` names, and `bytes`, what it holds on disk. Since 50a a stored voice model is a row too (`format` and `group`
  `voice`, `state` `voice` or `cannot load`, `architecture` its checkpoint),
  with its `bytes`. Since 53e an MLX row — a stored MLX directory, or an `mlx`
  backend over one — carries `kind`, what the directory is: `chat` (a language
  model), `vision` (a vision model, its towers kept), `embedding`, `drafter`,
  `video` or `voice`. It is the directory's own record's word, written by the
  conversion that made it; else `vision` when its files say so (a
  `vision_config` and an image processor beside it); else `chat` — so every
  directory made before the record reads as it did. Since 53i a directory with
  no record whose `config.json` declares a draft head (a drafter pulled whole)
  reads `drafter` from its files, before the vision rule. A word this build does not
  know is carried as written. Only `chat` and `vision` are what an `mlx`
  backend chats with: a row of any other kind has, in place of the backend's
  summary, a `note` saying what it is and what uses it (an `mlx` backend's row
  says first why its runtime cannot run, as before), and `config scan
  --register` registers it as nothing. Since 53h an `embedding` directory is
  the one exception: an `mlx` backend embeds with it, so `config scan
  --register` registers it as an `mlx` entry for the `embedding` role, and
  such a backend's row is not marked for attention -- its `note` says what it
  is and how wide its vectors are. A backend whose directory is missing, and
  every other row, has no `kind`.
- **`models info`** and **`models status`**: their `label: value` record as
  `fields`, in order, each label as printed and a value's indented continuation
  joined to it with a newline. The values are the same words the human view
  prints. Since 52e `models info` on one stored set (`<model>/<format>/<id>`,
  or its id) adds a typed **`data`** beside the unchanged `fields`: `format`
  (`gguf`, `mlx`, `safetensors`, `pipeline`, `voice`), `handle`, `bytes` (every
  file of the set on disk), `architecture`, `window` (`tokens`, `source`
  `default` or `context_size`, `trained` — the trained window the files state),
  `cache` (a GGUF's: `bytes` at that window, `type`, `sliding_positions`) and
  `lineage` (each step back, `{kind, from, detail, inferred, missing}` —
  `pulled`, `converted`, `quantized`, `trained`). Each is what the format's own
  reader states — the GGUF header, an MLX model's files — and `null` where it
  states nothing (a pipeline's window, any cache but a GGUF's): an unknown is
  never guessed. Since M20, a SafeTensors set's `data` also carries
  `converts_to`, the formats `models convert` takes it to — `["gguf","mlx"]`
  for a language model's weights, `[]` for a draft head's or a diffusion
  model's, which every conversion refuses. Since 53i a draft head's is
  `["mlx"]` — an MLX drafter, never a GGUF — where the mlx-vlm installed in
  Apogee's Python environment drafts its `model_type` (a package under its
  own `speculative/drafters/`), its `mlx_conversion.converter` `mlx-vlm`; with
  none installed, or one without that drafter, it stays `[]`. Since M21, an embedding model's
  (sentence-transformers', by its `modules.json`) is `["gguf"]` -- and since
  53h `["gguf","mlx"]` where the mlx-embeddings installed in Apogee's Python
  environment builds its `model_type`, read from its own `models/`; with none
  installed, or one without that type, it stays `["gguf"]`. And `mlx` is
  left out wherever the mlx-lm installed in Apogee's Python environment builds
  no model of the set's `model_type`, by its own modules; with none installed
  nothing is decided, and `mlx` stays. It is decided once, from the set's own
  files, by the same functions that make those refusals, and it is `null` on
  the other formats, which are what conversions make. Since 53f a set whose
  `config.json` declares a tower (`vision_config`, `audio_config`, an omni
  model's `thinker_config`) is offered `mlx` by the mlx-vlm installed, which
  builds it with its towers kept, where that builds its `model_type`; else by
  mlx-lm's build of its language model alone (`--mlx --text-only`), where
  mlx-lm builds it; else not; with neither installed, nothing is decided. Its
  `data` also carries `mlx_conversion`, what an MLX conversion runs: `towers`
  (the keys, as `config.json` names them, `[]` for a language model),
  `converter` (`mlx-vlm` or `mlx-lm`, what a plain `--mlx` runs, or `null`
  where only `--text-only` can; since 53h `mlx-embeddings` for an embedding
  model it builds) and `text_only` (whether `--mlx --text-only` is offered:
  never for a language model, which has no tower to leave out, nor an
  embedding model).
  It is `null` where `mlx` is not offered, and on every other format; the
  text says the same for a set with towers in a `towers:` line. Since 53e an MLX set's
  `data` also carries `kind` — the same word, from the same reader, as its
  `models list` row's `kind` above — and the text gains a `kind:` line saying
  the word, where it came from (`recorded`, `inferred from its files`, or `no
  kind recorded`) and what runs it; `kind` is `null` on every other format. An
  `mlx` backend's own record has the `kind:` line too. Since 53h an MLX set's
  `data` also carries `vector_width`: for an `embedding` directory, how wide
  its vectors are as its own files state it (the last sentence-transformers
  Dense module's `out_features`, else `config.json`'s `hidden_size`), a number;
  `null` for every other kind, every other format, and an embedding directory
  whose files state no width. The text says it in a `vectors:` line -- on the
  set, and on an `mlx` backend over one, beside an `embeds:` line. Since 53j a
  SafeTensors set's `data` also carries `wan_layout`: for Wan's original
  layout -- a `config.json` naming Wan's own transformer class (`WanModel`, or
  a later class named after it; never diffusers' `WanTransformer3DModel`) --
  `class` (as written), `whole` (whether every component it needs beside its
  transformer is there: its T5 text encoder, its VAE, and any other root
  `.pth` its record lists) and `missing` (those not there, by file name, in
  that order: `[]` when whole). A file not there and never recorded is named
  as Wan's releases name it -- `models_t5_umt5-xxl-enc-bf16.pth`, and the VAE
  by the latent width the config's `out_dim` declares (`Wan2.2_VAE.pth` for
  48, `Wan2.1_VAE.pth` for 16, else `*_VAE.pth`). `models pull <repo>
  --safetensors` on such a set fetches what is missing into it, its id
  unchanged. It is `null` for every other set and on every other format; the
  text says the same in the `diffusion:` line, with the pull that completes
  the set and what runs the model today. A backend's document has no `data`.
- **`chats list`**: newest first, each conversation's `id`, `updated`, `turns`
  and `name`, and since 52a the `backend` it runs on and the roster `model` it
  pinned there with `-m` or `/model` — empty when it runs on the backend's own
  model — as its session file records them.
- **`agents list`** is exactly the body `GET /v1/admin/agents` serves; **`mcp
  list`** each server as `GET /v1/admin/mcp-servers` serves it — `name`,
  `command`, `args`, `enabled`, `env_set` (never the environment itself) — plus
  what connecting to it found: `state` (`connected`, `not connected`,
  `disabled`), `protocol_version`, `tools`, and `error` when it did not connect.
- **`mcp presets`** (since 47c): one row per server Apogee ships, as `mcp
  presets` prints them — `name`, `description`, `provides`,
  `registers_into_apogee`, and what it registers on this machine: `command`
  and `args`, resolved here (the training environment's interpreter or
  `python3` on PATH, the seeded script; this binary's own path for `self`).
  `runnable` says whether this machine can run it; when it cannot, `missing`
  says what is lacking and `remedy` the command that provides it. A
  registrable preset carries `register` (`apogee mcp create code --from
  code-exec`), and `self` its `refusal` instead. Each carries `host_entry`,
  `{"name", "server": {"command", "args"}}`: the entry another MCP host takes.
- **`check`**: every row the report shows, `status` one of `ok`, `warn`, `fail`
  and `skipped` — never folded into a pass — `ok` the verdict and the exit code
  the human run's (non-zero on a failure). With `--fix`, what it repaired is the
  document's `fixed`, never a line beside it. Since 52e the `Video` section's
  `ffmpeg captions` (the `subtitles` filter, libass: a composition's captions
  burn in, else they are kept beside the video as an SRT) and `ffmpeg h264`
  (`libx264`: else clips encode as MPEG-4 Part 2) rows read ffmpeg's own
  listings; each missing ability is `skipped` — optional, as pdftotext is,
  so a fresh install stays green — its `remedy` the command that brings it on
  that platform, and an ffmpeg missing or not answering skips both too; neither
  is ever a pass. Since 53c an `Adapters` section, present when an entry
  applies an adapter: a row per adapter, named `<backend>: <file>` -- `fail`
  when it is not there, not the kind its runtime reads, pinned to no base, at a
  scale the loader refuses, or pinned to other weights than the base holds
  now; `skipped` when the base cannot be read to check its pin -- each failure's
  `remedy` its doors, `, or `-joined, each a command before ` to <what it
  does>`.
- **`providers scan`** (since 37b): a row per provider in the knowledge table, as the human scan prints them -- `provider` (`claude`), `type` (`claude-cli`), `label` (`Claude CLI`), `tier` (`credentials found`, `verified 2026-10-06`, `not found`), `backend` (which configured backends reach it, `not registered` when it could be, else empty) and `evidence`, a line each. It is the explicit scan, so it may start a vendor's binary for its version as the human scan does. With `--register`, what registering did is the document's `registration` and the rosters it fetched its `rosters`, a line each; with `--refresh`, the rosters re-fetched are `rosters` too.
- **`knowledge list`** and **`knowledge info`** (since 37c): the records the
  human list counts, newest first and kept to `--status`/`--discipline` as it
  is, each record the object `--json` prints (which stays, a bare array for
  `list`); `db` names the collection read.
- **`embed list`** and **`embed info`** (since 37c): each collection's `name`,
  `chunks` and `sources` count, or the `error` that kept it from being read;
  one collection's `path`, `schema`, `chunks`, `vectors` (`chunks`,
  `dimension`, `model` when one is pinned, `widths`) or `null` when it has
  none, its lexical `index`, and its `sources`.
- **`graph stats`** (since 37c): the counts the text prints, whole --
  `kind` (`named` or `collection`), `built`, `nodes` with `nodes_by_type`,
  `edges` with their `edges_extracted`/`edges_inferred` origin, `mentions`,
  coverage (`total_chunks`, `chunks_with_mentions`), `nodes_with_vectors`,
  the code layer (`code_files`, `code_files_by_language`, `code_mentions`,
  `unresolved_names`), `extract_model`, `stale_files`, `failed_chunks` and
  the communities; a named graph adds its entry's `collections` and
  `sources`, its `embed_model`, each member's share in `members`, and
  `membership_changed_from` when the entry moved since the build. A graph not
  built yet is `"built": false`, not a refusal.
- **`graph show`** (since 37c): `match` (`exact`, or `closest` with
  `also_matched` as the text's lines say) and each entity in `data` -- `name`,
  `type`, the whole `description`, `mentions`, `vector_dim`, a decision node's
  `status` and `discipline`, a code node's `stated_at`, its `relations`
  (`relation`, `direction`, `peer`, `peer_type`, `weight`, `origin`,
  `description`, an extracted edge's first `sites`) and its supporting
  `chunks`, their text whole.
- **`train status`** and **`train versions`** (since 37f): the bodies the
  control plane serves for the same reads (`operations/training_reads`, one
  builder for both) -- the status's run count, the runs `running`, the newest
  five as `recent_runs`, each backend's `versions` headline, the pipelines,
  the `active_pipeline` and `latest_pipeline`, whether a cycle holds the lock
  (`cycle_active`) and the cycle's headline; a version ledger, or every one in
  a list -- each version its `gguf_path`, an MLX one's `mlx_path`, or since
  53b an adapter version's `adapter_path` (a GGUF LoRA, or an mlx-lm adapter
  directory, registered on the entry's `adapters:` over its untouched base)
  and the `base_sha256` it was pinned to -- and since 53c, for one its gate
  scored on the quantized base the entry serves, `eval_scored_on` (`<base>
  (<precision>)`). A backend with no history is refused as the text refuses
  it.
- **`datasets list`**, **`datasets info`** and **`datasets kits`** (since
  37f): the control plane's bodies -- each dataset's `name`, `path`, `lines`,
  `bytes` and `shape`; each kit's `name`, `path`, `description`,
  `eval_items` and `error` when it cannot be read.
- **`train regime list`** and **`train regime show`** (since 42a): the
  regimes a `train regime run` can name, the host's own -- each `name`,
  `description`, `source` (`shipped` -- its compiled-in text, `seeded` --
  its file under `training/regimes/`, as shipped, or `user` -- a
  `training.regimes:` entry, a spec of the user's, or a shipped one's
  edited), `config` (`true` for a `training.regimes:` entry), `path`
  (`null` for `shipped` and for an entry), `kits` in order, `count` and
  `iters` (`0`: each kit's own), `temperature` (`0`: each kit's), and
  `teacher`, `student` and `promote_as` -- always `""` for anything
  shipped, which names kits and tuning only. `list` adds `problems`, a line
  per spec file left out because it does not parse; `show` is one regime
  as `{"object":"training.regime",…}`. A name nothing answers is refused
  as the text refuses it.
- **`auth list`** (since 37g): the control plane's body -- each stored slot's
  `provider` and `stored_at`, which rung answers for each configured backend
  that takes a key (`backends`: `name`, `type`, `source`, and the `variable`
  when it is one), and the store's `warning` when it could not be read. A
  stored credential is metadata by type: there is no field a key could be
  written to.
- **`system`** (since 32a): the machine as one reading — `cpu` (`model`,
  `architecture`, the core counts, `load`, `utilization` with its `window_ms`),
  `memory` (`total_bytes`, `used_bytes`, `available_bytes`), `apogee`
  (`process_bytes`, `models_held`), `gpu` (`name`, `unified_memory`,
  `model_budget_bytes` — the memory a model may offload to, the number window
  sizing reads) and `disk` (the model store's `store_bytes` and its volume).
  Every field is always there; one the platform cannot read is `null`, with
  the reason beside it where there is one (`load_unknown`, `gpu.unknown`,
  `model_budget_unknown`). The read takes about half a second: the CPU's
  utilization is measured over a 500 ms window.

No document carries a key or a token. A command that has no JSON face refuses
`--output-format json` and names the commands that have one.

## What machine mode does not do

- **It opens no sockets.** Not one, ever — asserted in CI with `lsof`. Only
  `apogee serve` owns a port.
- **It does not push.** Events arrive in response to turns, never unprompted —
  and a task's events only on the stdout of the `task run` or `task resume`
  that runs it: a `chat` child never narrates a task it did not start.
- **It does not represent slash commands.** `/model`, `/compact` and the rest are
  terminal-REPL affordances; a driver uses the CLI commands and its own UI. The
  one exception is an `execute` session's `/play`, which is a turn rather than a
  setting (see [Playing a symphony](#playing-a-symphony-execute)).
