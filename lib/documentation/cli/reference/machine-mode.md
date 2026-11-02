# Machine mode: the JSONL protocol

Apogee's contract with a front-end. A GUI powers Apogee by **spawning the
executable and speaking over its pipes** — never over localhost. `serve` remains
the surface for genuine server deployments, where a remote client makes REST
calls to Apogee running on a host; a local front-end uses this instead.

This document is the reference for that protocol. The worked example is
[`reference_driver.py`](../../../src/cli/tests/scripts/py/reference_driver.py), which is also
the test that keeps this document true.

Apogee drives it too: `apogee acp` (56a) runs each Agent Client Protocol
session as a child of its own `execute` in this mode and translates between
the two -- so an ACP host such as Xcode is told exactly what a driver here is
told, and nothing more ([acp.md](acp.md)).

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
document last (see [A long job's run](#a-long-jobs-run)) -- and since 55h
`train pipeline run`, `train pipeline resume` and `train cycle run` (see
[A training run](#a-training-run-train-pipeline-and-train-cycle)).

## Reading the stream

One JSON object per line on stdout. Every object has a `type`.

```jsonl
{"type":"session","protocol_version":1,"model":"claude-sonnet-5","chat_id":"20261024-120000-a1b2","next_turn":1,"capabilities":{"events":["session","thinking","…"],"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"tools":true,"ask":true,"schema":"2026-11-01"}}
{"type":"thinking"}
{"type":"thinking_delta","text":"…"}
{"type":"memory","chats":1,"decisions":1,"sessions":[{"id":"20261019-142233-a1b2c3","name":"asyncio timeouts in the scraper","updated":"2026-10-19T14:40:02Z"}]}
{"type":"sources","sources":[{"file":"report.pdf","pages":[41,42],"score":0.81,"retriever":"vector"},{"file":"notes.md","lines":[12,40],"score":0.4,"retriever":"lexical"}]}
{"type":"tool_status","text":"fetch_url https://…"}
{"type":"tool_call","id":"call_1","name":"fetch_url","state":"running","target":"docs.python.org"}
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
| `session` | Once, first, unprompted. Names the protocol version, the model and the session's `capabilities` (below); `chat_id`, the chat the session writes (M28, [below](#which-chat-chat_id)); `"private": true` for a chat that writes nothing (52d, [below](#a-private-chat-chat---private)); `suite`, the suite it runs under, and for `execute` `orchestrating` (M29, [below](#which-suite-suite-and-orchestrating)). A launch the child refuses sends none ([below](#a-launch-refused)). |
| `thinking` | The model began reasoning. No text. Sent again with `"budget_reached": true` when the reasoning reached its thinking budget and was ended there (`--think-budget`, an `--effort` level's budget, or the backend's `thinking_budget`). Machine mode carries the reasoning as it streams whatever `ui.thinking_display` says: the terminal's and the shell's one-line summary (43) is theirs alone, and is never asked for here. |
| `thinking_delta` | A chunk of reasoning. **Droppable** — see below. |
| `memory` | `chat` only: what a turn was handed from earlier conversations -- `chats`, past chats' summaries, and `decisions`, recorded knowledge records -- injected for this turn and never into the transcript (26l). Sent before the turn, only when it recalled something. Since 52a `sessions` names each past chat recalled, in the order sent: its `id` (what `chats list` and `--resume` take), and the `name` and `updated` time the recall index recorded when it summarised the chat -- what the model was handed, so a later rename shows only once the chat is summarised again (its next run with a turn). Since 55b that `name` is the chat's name as `chats list` gives it -- the one `chats title` or `/title` gave it, else its generated title, else its id -- never a generated title it was renamed from. Absent when `chats` is 0. |
| `sources` | The excerpts retrieval sent the model this turn (52a), before the answer: `sources`, each `{file, pages?, lines?, times?, score, retriever}` in the order sent -- the chat's attachments' excerpts first, then a collection's. `file` is the file as its index names it (an attachment's file name, a collection's source path); `pages`, `lines` and `times` (seconds, for a recording) are `[first, last]` when the excerpt's chunks record them. **What was sent, never what was found**: an excerpt the context budget trimmed is not here, and with nothing sent there is no event. `score` is on its `retriever`'s scale -- `lexical`, `vector` or `hybrid` scores do not compare. Number them in order to draw citations. Past chats' recall is `memory`'s, not this. |
| `tool_status` | A tool is running, described in `text` for display -- or another model call the turn makes besides the chat model's own (the embedder, the utility model, the rerank judge, the knowledge clerk, a suite member the model consults), as `<role> — <what it is doing>` (26n). A `consult` (27f) is an ordinary tool call: `[tool] consult`, then `consult — asking utility (<backend>): <the question's first words>` while the member answers, and the answer reaches the model as the tool's result. A suite's verifier checking a tool call or an answer (27g) is said the same way: `validate — asking utility (<backend>): Check …`. A symphony the root model plays on its own (27t, an orchestrating `execute` session) is an ordinary tool call too: `[tool] play_<symphony>`, then `play — the model chose <symphony>: <n> member calls, <roles>`, then a line per stage. A tool the **vendor** ran on its own servers (47a: an entry's `server_tools:`, or the run's `--search`) is said here too, never dispatched: `<tool>, run by <vendor>: <what>` -- `web_search, run by Anthropic: "apogee harness"`, then `web_search, run by Anthropic: 5 results` -- and since it can fall between two parts of an answer, the answer's events end before it and start again after. |
| `tool_call` | One tool call's state as it moves (56a), beside its `tool_status` line: `id`, the model's id for the call -- which can repeat: where a provider's wire gives a call no id, one is made for it (Gemini's API uses the tool's name), and every call is reported `pending` first, so a `pending` under an id seen before is a new call; `name`, the tool; `state` -- `pending` (before its permission gate), `running`, then `done` or `failed` (an error result, a refusal, a tool that does not exist, or a turn stopped while the call's permission prompt waited -- a `cancel`, or stdin closed -- reported before that turn's `result` or `error`; more states may be added), so every call reported `pending` ends `done` or `failed`; and `target` when the tool names one -- the path, the command or the host the gate is told, absent otherwise. A call answered without running (an `ask_user`, a repeated call, a tool withdrawn for the turn) has none. Draw it as a card keyed on `id`, a new card at each `pending`; `tool_status` is unchanged, and the terminal and the shell keep drawing it. |
| `notice` | A line for the user in `text` that is neither progress nor an error — a local model answering without the tools it was given because its chat template cannot take them, or a reply kept as text because it did not match the template's format, or a request trimmed to fit the model's window (`context budget: 2 earlier exchanges not sent`), or what a suite's verifier said (27g): an objection to a tool call returned to the model, a dispute the call runs over, the two positions on an answer under `answers: always` (`validate: …`), a check that could not be made; what a resumed chat could not carry over (55b, `resume: …`, [below](#which-chat-chat_id)); what the session's suite takes of this machine and its warnings at launch (M29, [below](#which-suite-suite-and-orchestrating)); a compaction that folded nothing or failed ([below](#the-sessions-own-doors-detach-set-compact)); a chat deleted while its session ran; what a session's MCP servers said at startup that stays (56b, [below](#which-suite-suite-and-orchestrating)). Show it and keep it; it never ends the turn. |
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

### Which chat: `chat_id`

A driven `chat` or `execute`, and a `task run`, names on its `session` event
the chat it writes (M28): `chat_id`, the id `chats list` lists it by, `chats
show` reads and `--resume` takes. A resumed session (`--resume`, `-c`) names the
chat it resumed -- its id, when it was resumed by name. A private chat (`chat
--private`) writes none, so its `session` carries no `chat_id`; neither does a
`complete` or an agent's run, which write no chat. Read it rather than guessing
the session's chat from what appears in `chats list`: an older binary sends no
`chat_id`, and a host falling back to a guess must allow for that.

**A chat is written once a turn is answered.** A new session closed before any
`user` line was answered -- sent none, or each it sent failed or was cancelled
-- writes nothing: no file, no row in `chats list`, not its title or settings,
not the index of what was attached. So the id it named is listed only after an
answered turn. A resumed chat closed with no new turn, and nothing moved in it,
keeps its file exactly as it was; a setting a `set` line moved is saved, as it
always was. Chats an older binary saved with no turn stay listed until deleted.

**A resume says what it could not carry over** (55b). Resuming degrades rather
than fails -- a file older than the session metadata, a backend or suite no
longer configured (the chat then runs on the default), a message that could not
be read -- and each is a `notice` `resume: <what>`, after the `session` event
(and a resumed `todo`) and before anything the session reads, carrying no
`turn`: `resume: backend 'gone' is no longer configured; using the default
instead`. Before 55b they went to stderr alone; the terminal still prints them
as `[resume]` warnings.

**The one store is edited from outside too** (55b). `chats title` and `chats
delete` may act on a chat while a session runs it -- a host's Rename and Delete.
Each save the session makes reads the chat's file first: a name given outside
since is kept, and the session goes on under it; a chat deleted outside stays
deleted -- the session writes nothing more of it (no transcript, no recall
summary, no saved model state), answers its turns as before, and says so once in
a `notice`: `chat <id> was deleted while this session ran -- it stays deleted,
and nothing more of it is saved`.

### Which suite: `suite` and `orchestrating`

A driven `chat` or `execute` running under a suite names it on its `session`
event (M29): `suite`, the suite as `--suite`, a resumed chat's own or
`models.default_suite` chose it -- the one the chat saves, so a resumed session
(`--resume`, `-c`) names the suite it resumed under. A session under none -- no
suite configured or named, `--suite off`, a bare `execute` -- carries no
`suite`. An `execute` session also says `orchestrating`: `true` when its root
model may play the symphonies on its own (`--orchestrate`, or the suite's
`orchestrate: true`; [below](#playing-a-symphony-execute)), else `false`; a
`chat` carries none. A later `/suite` at a terminal moves the suite; a driver
reads the one the session opened under here.

**The suite says what it takes of the machine as a `notice`** (M29). Each
session under a suite prices its members against this machine at launch, and
the line saying so -- `suite research needs 10600 MiB of this machine's 98304
MiB: root 7468 + helper 2108 + 1024 margin -- fits`, `suite research: no member
holds memory on this machine`, or a run forced over budget, `... -- over budget,
run anyway (--force)` -- is a `notice` after the `session` event, as are the
suite's warnings at launch: a member that could not be rebuilt at the window
the suite pins (`gone keeps its old window -- it could not be rebuilt: …`), a
member `--warm` could not load. They are said with a resume's own notices,
in the order they arose, before anything the session reads, carrying no
`turn`; stderr carries none of them. The terminal prints them on stderr as
before.

**The session's MCP servers say what went wrong as one `notice`** (56b). A
driven `chat` or `execute` with `--tools` connects its MCP servers before its
`session` event; every line of that startup that stays -- `[mcp] warning: x:
connect failed (skipped): … -- stderr: <its last line>`, `[mcp] warning: x:
connect timed out after 20s (skipped)`, an agent's server not configured, and
for an `apogee acp` session the host's servers it set aside -- is one `notice`
after the `session` event, a line each, with a resume's and the suite's,
carrying no `turn`. Progress (`[mcp] connecting: x...`, `[mcp] connected: x (3
tools)`) is no notice, and a session whose servers all connected says nothing.
Before 56b these lines reached stderr alone, which still has them. `complete`'s
one-shot keeps them on stderr.

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
no `user` line. The turns a resumed conversation already holds are a read,
not events: `apogee chats show <id> --output-format json` (since M27,
[below](#everything-else-is-a-cli-command)), whose last turn is `next_turn`
minus one. Both read one count (55b): the turns the file counts, or the
answered turns its history holds when that is more -- a file older than the
count, or a served session sent several exchanges at once -- so a resumed
session never numbers a live turn with a number the read already drew.

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

### A launch refused

A child that cannot open the session it was asked for refuses it **before the
`session` event**: nothing is written on stdout -- no `session`, no `error` --
the child exits `1`, and stderr carries one line saying why, `apogee chat:
<why>` (or `apogee execute: …`). An `error` there would break the stream's
shape -- `session` comes first ([the stability promise](#the-stability-promise))
-- and would say a turn failed when none was read. So a host that sees stdout
end with no `session` event reads the exit code and stderr for the reason. A
launch is refused for, among others:

- a backend named by `-m` that did not start, with that backend's own problem
  -- `backend 'mlx' is configured but could not be started: … (started
  backends: claude)` -- or a name the config defines nowhere, `no backend named
  'nope' (configured: …)` (M29). Nothing else answers under the name asked
  for. A `set` line's `model` naming such a backend mid-session is refused
  the same way, as a `notice` (56a; `set` below);
- a suite that does not fit this machine: `suite vast needs 247056 MiB of this
  machine's 110100 MiB: … -- it does not fit; --force runs it anyway`, and a
  suite that does not exist, `--suite: no suite named 'nope' (configured: …)`;
- a chat that cannot be resumed (`no chat named '…'`), flags that cannot go
  together, and no backend that started at all.

Once `session` is out, what the session says for the user is on the stream;
stderr keeps its diagnostics.

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
{"type":"session","protocol_version":1,"model":"local","chat_id":"20261004-120000-c4f2"}
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

**A task names its backend or is refused** (M29). A task whose backend cannot
answer is refused at launch, as [a session's is](#a-launch-refused): `-m`
naming a backend that did not start or one the config defines nowhere, and on
`task resume` the backend its conversation was left on, if it no longer
starts -- `task <id> cannot resume: backend 'm' is configured but could not
be started: … (started backends: …)`. Nothing is made or run, nothing is
written on stdout, and the exit code is `1`, the reason on stderr. A resumed
task whose backend is no longer configured at all runs on the default, said
on stderr as a `[resume]` line, as a chat's resume says it.

## A long job's run

Since 52b the four verbs that run for minutes — `models pull`, `video
generate`, `video compose` and `voice say` — take `--output-format
stream-json`, and since M30 `datasets pull` too: the progress each already
narrates for people, as one JSON object a line on stdout, and the verb's own
document last. It is a relay,
never a second narrator: the chain's steps, the download's counts and the
drivers' events are the same producers the busy line and the `[i/n]` lines
read, framed as events instead. Every line a verb prints for people on stdout
reaches the stream as a `notice` (blank lines dropped); stderr keeps the
diagnostics, as ever. Without the flag every verb prints exactly what it did
before.

```jsonl
{"type":"job","verb":"models pull","protocol_version":1,"schema":"2026-11-01"}
{"type":"notice","text":"pulling org/model, then converting it to F16 and registering it"}
{"type":"stage","index":1,"count":3,"label":"pull org/model's full weights","state":"started"}
{"type":"bytes","done":67108864,"total":4971201024,"file":"model-00001-of-00002.safetensors","index":1,"count":4}
{"type":"stage","index":1,"count":3,"label":"pull org/model's full weights","state":"done"}
…
{"type":"document","document":{"name":"org--model/safetensors/1a2b3c4d5e6f","fields":[…]}}
{"type":"document","document":{"name":"org--model/gguf/9f8e7d6c5b4a","fields":[…]}}
```

```jsonl
{"type":"job","verb":"video compose","protocol_version":1,"schema":"2026-11-01"}
{"type":"notice","text":"plan: \"Tides\" -- 2 scenes, about 5.5 s of video"}
{"type":"scene","scene":1,"scenes":2,"state":"speaking"}
{"type":"ready","device":"mps","engine":"torch","scene":1}
{"type":"segment","index":1,"of":1,"seconds":2.4,"scene":1}
…
{"type":"scene","scene":1,"scenes":2,"state":"rendering"}
{"type":"ready","device":"mps","dtype":"bfloat16","mode":"t2v","engine":"torch","scene":1}
{"type":"step","step":1,"steps":50,"scene":1}
…
{"type":"frames","frames":73,"width":1280,"height":704,"scene":1}
{"type":"assembling","frames":73,"scene":1}
…
{"type":"document","document":{"object":"video.project","status":"done",…}}
```

```jsonl
{"type":"job","verb":"datasets pull","protocol_version":1,"schema":"2026-11-01"}
{"type":"notice","text":"downloading 2 file(s), 1.2 MiB, into /Users/you/.apogee/training/datasets/raw/org--ds"}
{"type":"bytes","done":524288,"total":1048576,"file":"data/train.jsonl","index":1,"count":2}
…
{"type":"notice","text":"Convert it with:  apogee datasets prepare /Users/you/.apogee/training/datasets/raw/org--ds"}
{"type":"document","document":{"object":"dataset.pull","ref":"org/ds","path":"/Users/you/.apogee/training/datasets/raw/org--ds","files":2,"bytes":1258291}}
```

| Event | Meaning |
|---|---|
| `job` | First: the `verb`, `protocol_version` and `schema` — the same versions a session's announce. |
| `stage` | A step of `models pull`'s chain (`--safetensors --register[-with]`): `index` of `count`, its `label` in the chain's words, `state` `started`, `done` or `failed`. Since 55h a pipeline's stage too, as its manifest records it ([below](#a-training-run-train-pipeline-and-train-cycle)). A failed stage is followed by the chain's own `notice`s saying where it stopped and the one command that resumes it -- or, since 55d, where it stopped alone, when the stage found nothing after it could run (a pulled draft head, whose refusal names its one conversion). |
| `bytes` | Bytes a pull has moved — a download, a copy from the Ollama store, a hash: `done`, and `total` **only when known** (never guessed). In a tree of files (a snapshot, an MLX model, a pipeline, a voice model), the `file` moving and its `index` of `count`. Sent at each file's start, at each whole percent of a known total (each 16 MiB of an unknown one), and at its end. |
| `ready` | A runtime loaded: `device`, and `dtype` and `mode` (`t2v`, `i2v`) when the driver says them. Since M24 `video generate`'s, `voice say`'s and a composition's also carry `engine`, the engine the run takes — `torch` or `mlx` — the same word the final document's `engine` says, so a run is known as MLX's while it runs. A `ready` without it (an older build) is PyTorch's. |
| `step` | A denoising step done: `step` of `steps`. |
| `frames` | A clip's frames written: `frames`, `width`, `height`. |
| `assembling` | Those `frames` going into the clip. |
| `segment` | Speech made: segment `index` of `of`, its `seconds` as the driver counted. |
| `scene` | A composition's scene begun: `scene` of `scenes`, `state` `speaking` (its narration), `rendering` (its clip) or `asset` (its asset taken as it is). Every `ready`, `step`, `frames`, `assembling` and `segment` inside a composition carries the `scene` it belongs to; outside one they carry none. |
| `document` | Last on success: the verb's own document, whole. For `video generate`, `video compose` and `voice say` it is exactly what `--output-format json` prints -- on success, the record written beside the output. For `models pull`, which has no document of its own, one `document` per set of weights the run stored, in the order stored, each exactly what `apogee models info <model>/<format>/<id> --output-format json` prints for it. For `datasets pull` (since M30), one `document` naming where the dataset landed -- `{"object":"dataset.pull","ref","path","files","bytes"}`: the `ref` as given, the folder its data files landed in (`path`, under `training/datasets/raw/`, what `apogee datasets prepare <path>` takes), how many files and their bytes. |

A failure ends the stream with an `error` event, its `message` the words stderr
gets -- the stream's last event, so a chain's `failed` stage and the `notice`s
saying where it stopped and how to resume come before it. A media run that
started and failed or was cancelled sends its document first, exactly as
`--output-format json` prints it, `status` `failed` or `cancelled`: for `video
generate` and `voice say` a record of the run written nowhere -- nothing lands
beside the output, and its `sidecar` is `null` -- and for `video compose` the
project as it saved it beside the video, its finished scenes kept for the same
command to resume. A run refused before it starts sends no document, and ends
on its `error` all the same -- a runtime not installed among them (M29): `torch
and diffusers are not installed in Apogee's Python environment -- apogee train
setup --with video` (`torch and kokoro` and `--with voice` for speech), or no
Python environment yet, `the Python environment has not been created yet. Run
'apogee train setup' first (…)`. Asked of a terminal those are questions; on a
pipe they are refused, the words on stderr and in the `error`. Ctrl-C
exits `130` with the stream ending cleanly on its `error`, every line whole --
`models pull` on every path too since 55c: a copy from the Ollama store, a
GGUF, a SafeTensors snapshot, an MLX model, a pipeline, a voice model and the
register chain's download, what it staged removed (`cancelled -- nothing was
written`) -- and `datasets pull` since M30, the listing and the download
alike, its staged folder removed (`cancelled -- nothing was written`).
`models pull` and `datasets pull` take `text` or `stream-json` only; the
three media verbs take `text`, `json` or `stream-json`.

### A training run: `train pipeline` and `train cycle`

Since 55h `train pipeline run`, `train pipeline resume` and `train cycle run`
take `--output-format stream-json` -- `text` or `stream-json`, no `json` of
their own: the document is the read's, `train pipeline status` and `train cycle
status` ([below](#everything-else-is-a-cli-command)). The same relay: nothing
is narrated a second time. A pipeline's stages are **its manifest's** -- the
manifest under `training/pipelines/<id>/` is rewritten on every transition, and
each write is relayed once it is on disk, as a `stage` for every stage whose
status that write moved (on the first write every stage, so a host joining at a
resume knows which already passed). The trainer's ticks, which the terminal's
status line shows, are `iteration`s; every other line the terminal would print
-- the `[pipeline]` header, `[train]`, `[eval]`, `[gate]`, `[fuse]` and
`[warn]` lines, and in a cycle the promotion's `[promote]` lines -- is a
`notice` in the same words; a cycle pass's own narration is its `pass` events.
Since M30 a halt taken while a pass runs -- `apogee train cycle halt` from
another terminal -- is kept, as a `resume`'s reset failure count is: the pass
finishes as it would have, its `pass` events unchanged, and a `notice` just
before the document says so -- `[cycle] halted while this pass ran (halted by
'apogee train cycle halt') -- the halt is kept: 'apogee train cycle run'
refuses until 'apogee train cycle resume'` -- the document's `halted` `true`.
It is never a `pass` of state `halted`, which is the breaker tripping.
In the text view all of it goes to stderr; under `stream-json` it is on the
stream and stderr keeps the diagnostics -- a refusal or a failure is said there
as well as in the stream's `error`. Without the flag every verb prints exactly
what it did before.

```jsonl
{"type":"job","verb":"train pipeline run","protocol_version":1,"schema":"2026-11-01"}
{"type":"notice","text":"[pipeline] skills as pipe-20261025-120000: 2 stage(s), mlx on aaaaaaaaaaaa; no judge: unmatched items skip"}
{"type":"stage","index":1,"count":2,"label":"a","state":"pending","pipeline_run_id":"pipe-20261025-120000"}
{"type":"stage","index":2,"count":2,"label":"b","state":"pending","pipeline_run_id":"pipe-20261025-120000"}
{"type":"stage","index":1,"count":2,"label":"a","state":"training","pipeline_run_id":"pipe-20261025-120000","run_id":"pipe-20261025-120000-s0"}
{"type":"notice","text":"[train] stage 1/2 'a': mlx lora on aaaaaaaaaaaa, dataset starter.jsonl"}
{"type":"iteration","iteration":10,"iterations":600,"loss":1.7342,"lr":0.0001,"throughput":2.4,"stage":1}
…
{"type":"stage","index":1,"count":2,"label":"a","state":"evaluating","pipeline_run_id":"pipe-20261025-120000","run_id":"pipe-20261025-120000-s0"}
{"type":"notice","text":"[gate] stage 'a' PASSED the cumulative eval -- 100% (24/24)"}
{"type":"stage","index":1,"count":2,"label":"a","state":"fusing","pipeline_run_id":"pipe-20261025-120000","run_id":"pipe-20261025-120000-s0","cumulative_score":1.0}
{"type":"stage","index":1,"count":2,"label":"a","state":"passed","pipeline_run_id":"pipe-20261025-120000","run_id":"pipe-20261025-120000-s0","cumulative_score":1.0}
…
{"type":"document","document":{"kind":"pipeline","pipeline":{"pipeline_run_id":"pipe-20261025-120000","spec_name":"skills","status":"complete","stages":[…],…}}}
```

```jsonl
{"type":"job","verb":"train cycle run","protocol_version":1,"schema":"2026-11-01"}
{"type":"pass","state":"collecting","text":"[cycle] directory: 1 file(s) in /Users/you/queue"}
{"type":"pass","state":"merging","text":"[cycle] 12 example(s) from directory"}
{"type":"pass","state":"training","text":"[cycle] pipeline cycle-20261025-030000 (nightly): 1 stage(s)"}
{"type":"stage","index":1,"count":1,"label":"base","state":"pending","pipeline_run_id":"cycle-20261025-030000"}
…
{"type":"pass","state":"gating","text":"[cycle] gate -- current 96%, previous 94%, anchor 95% (v3), threshold 2%"}
{"type":"pass","state":"promoting","text":"[cycle] gate PASSED (96%) -- promoting cycle-20261025-030000-s0 into nightly-model"}
{"type":"notice","text":"[promote] run cycle-20261025-030000-s0 -> nightly-model v4"}
…
{"type":"pass","state":"passed","text":"[cycle] complete -- nightly-model promoted to v4 (/Users/you/.apogee/models/…/nightly-model-v4.gguf)"}
{"type":"document","document":{"backend":"nightly-model","anchor_version":3,"halted":false,"runs":[…],"active":false,…}}
```

| Event | Meaning |
|---|---|
| `stage` | A pipeline's stage as its manifest records it: `index` of `count` (from 1), its `label` the stage's name, `state` the manifest's own word -- `pending`, `training`, `evaluating`, `fusing` (an intermediate stage merged into the next one's base), `passed` or `failed` -- with `pipeline_run_id`, the stage's own `run_id` once it has one (what `train eval` and `train promote` take), and `cumulative_score`, 0 to 1, once its cumulative gate has scored it -- this attempt's: a stage trained again (a resume's first unpassed) carries none from `training` until it is scored again, and its manifest holds none of the last attempt's `eval_results`, score, `adapter_dir` or `fused_dir` meanwhile (M30). A run Ctrl-C stopped leaves its stage where the manifest left it; the pipeline's own status is the document's. |
| `iteration` | A training iteration, as the trainer reported it: `iteration` of `iterations` (absent when the trainer did not say), `loss`, `lr` and `throughput` (iterations a second) -- the status line's facts -- and the pipeline `stage` it belongs to, from 1. As often as the trainer reports, never more. |
| `pass` | A line of a cycle pass's narration, `text` as the terminal says it, and `state`, where the pass is: `collecting` (its sources read), `merging`, `training` (its pipeline -- whose `stage`s, `iteration`s and `notice`s follow), `gating` (the dual gate against the last passing pass and the anchor), `promoting` -- or how it ended: `skipped` (no new data), `failed`, `halted` (the circuit breaker tripped on this pass) or `passed`. More states may be added. |

**The document is the read's**, last, whatever became of the run: for a
pipeline, what `apogee train pipeline status <id> --output-format json` prints
-- its `status` `complete`, `aborted` (a hard gate failed, or a Ctrl-C) or
`failed` -- and for a cycle pass, what `apogee train cycle status
--output-format json` prints, the history with the pass recorded. A run that
failed, or was cancelled, sends it and then the `error` that ends the stream: a
pipeline's gate failure in the words stderr gets, a cycle's `cycle FAILED --
<reason>` (on stderr too since M30, as `apogee train: cycle FAILED --
<reason>`), a Ctrl-C's `cancelled; recorded as aborted -- 'apogee train pipeline
resume <id>' continues it` or `cancelled before an outcome was recorded`. A run
refused before it starts -- a spec, a student or a dataset that does not
resolve, the cycle's lock held, the loop halted, a source that cannot be read
-- sends `job` and its `error` and no document. The exit codes are the text
view's: a pipeline `0` complete, `2` aborted or failed, `130` cancelled, `1`
refused; a pass `0` passed or skipped, `2` failed, `130` cancelled, `1`
refused.

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
turn as closing stdin would — without ending the session; the call a permission
prompt asked about is reported `failed` first, never left `pending` (it never
ran). Turns are serial, so
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
conversation, and exits cleanly -- a new chat with no answered turn writes
nothing (M28, [above](#which-chat-chat_id)).

### The session's own doors: `detach`, `set`, `compact`

Since 52d a driven `chat` or `execute` takes three lines that reach exactly
what the session's slash commands reach — never a second implementation —
each answered by `notice`s and effective from the next turn. Sent while a
turn runs, a line waits for it to end, as an `attach` does -- **unless the turn
asks**. While a `question` or a permission prompt is outstanding, the child
reads lines only for its `answer`, and every other line it reads in that time
-- a `user`, `attach`, `detach`, `set`, `compact` or `hello`, one sent before
the question appeared and still waiting included -- is **dropped, with no
`notice`** (52d's recorded rule; a `cancel` still fails the turn). So a host
sends `detach`, `set` and `compact` **between turns**: after the turn's
`result` or `error`, or before its first `user` line -- never while a question
waits, and not mid-turn when the turn may ask one.

- **`{"type":"detach","path":…}`** detaches what was attached under that
  name (the `path` the `attach` line gave), as `/detach` does: `notice`
  `detached <name>`, or `nothing attached as '<name>' -- /attachments lists
  them`.
- **`{"type":"set",…}`** moves the run settings the slash commands move —
  `model` (a backend, a roster model or `backend:model`, as `/model` and `-m`
  take it), `system` (the system prompt, replaced whole), `think` (`on`,
  `off`, `auto`), `effort` (a level or `auto`), `temperature` (a number,
  or its text) and since 56a `permission_mode` (the words `--permission-mode`
  takes: `manual`, `accept-edits`, `plan`, `dont-ask` -- and `bypass` only in a
  session launched with `--allow-bypass` or `--permission-mode bypass`, since
  a driver has nobody at a keyboard to say yes to it: `set permission_mode:
  bypass needs --allow-bypass (or --permission-mode bypass) at launch --
  permission mode: manual`). Several in one line apply in that order — the model first,
  so thinking and effort are judged on the backend that will answer. Each is
  said as a `notice` ending `-- from the next turn` (`switched to claude --
  from the next turn`), or refused by name (`set effort: not an effort level:
  'loud' (…)`) while the rest still apply. A `model` that is configured but
  did not start is refused in the launch's words, and the session stays on
  the backend it was on (56a): `set model: backend 'mlx' is configured but
  could not be started: … (started backends: claude)`. **Any key that is no setting — or
  a value neither text nor a number — refuses the whole line by name, and
  nothing is set**: `set: 'max_tokens' is not a setting a session moves, as
  text -- model, system, think, effort, temperature or permission_mode;
  nothing was set`. A line with no setting says so.
- **`{"type":"compact"}`** compacts the conversation now, as `/compact`
  does — the utility model, else the chat's own backend, folds the earlier
  exchanges into a summary — said as `history compacted by <backend>`
  (`history compacted` when the chat's own backend did it). Only what happened
  is said and counted (55b): with no exchange to fold — before the first turn,
  or straight after a compaction — no model is asked and the `notice` is
  `nothing to compact -- no exchange to fold yet`; a summariser that fails or
  answers nothing leaves the history as it was, saying `compaction by
  <backend> failed: <why> -- the history is unchanged`. Neither counts as a
  compaction (`chats info`'s `compacted: N time(s)`), nor is saved. A saved
  chat is compacted with the verb `apogee chats compact <id>` (asked first at
  a terminal, `--yes` for a script), which says `nothing to compact in <id>`
  the same way and refuses a failure by name (exit `1`), the chat untouched.

A chat is titled in the background after its first answer. A `compact`, a
`set` naming a `model`, or an `attach` sent while that title is still being
asked for waits for it first (55a) -- one model call at a time, as before the
slash commands -- so its `notice` can take a few seconds longer.

### A private chat: `chat --private`

`chat --private` (and `execute --private`, the session's one opening)
writes **nothing** of the conversation: no transcript under `sessions/` —
not written and later deleted, never written — no recall summary and no
recall of earlier chats, no saved model state, no auto-capture, no title
asked of any model (55a: one would be kept nowhere, and asking for it sent the
chat's questions to the utility model), and its attachments indexed in a
folder of the run's own in the system's temporary directory, removed as the
run ends. At a terminal its typed lines are its own up-arrow's alone: the line
editor's history file (`chat_history`) is neither read nor written. The
`session` event carries `"private": true`, and a `notice` says it: `private:
nothing of this chat is written -- no transcript, no recall`; the terminal and
the shell say the same line at the start. A saved chat cannot be resumed
privately: `--private` with `--resume` or `--continue` is refused.

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
suite whose `orchestrate: true` says so, which the `session` event's
`orchestrating` reports (M29) -- an `execute` session offers its root
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
accepted. Any other line read while the question waits is dropped with no
`notice` -- a `cancel` aside -- so send it after the turn ends
([the between-turns rule](#the-sessions-own-doors-detach-set-compact)).
Closing stdin with a question outstanding fails the turn, and the half-turn
is rolled out of history rather than persisted half-finished. A
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

Since 56a a question asked about a tool call names it: `tool_call_id`, the
`id` of the call's `tool_call` events, so a driver that draws each call can put
the question on its card -- `"tool_call_id":"call_7"` beside `tool` and
`target`. A question asked outside a call carries none. Where ids repeat (see
`tool_call` above), it is the call last reported `pending` under that id; its
own `tool` and `target` say what is being asked about. A call a `delegate`
sub-session makes asks under its own id, and is reported as `tool_call` events
too, while the `delegate` call is `running` -- so every `tool_call_id` names a
call the stream reported, and that call ends `done` or `failed`.

Reply with one `{"type":"answer","text":"…"}` line: `yes` allows this once,
`session` allows the tool for the rest of the run, `always` allows it and
writes `permissions.<tool>: allow` to the config through the same editor the
CLI uses, and anything else denies. A denial is a **tool result** the model
reads, not an error; the turn continues. As with a question, a line other than
the `answer` read while the prompt waits is dropped with no `notice`. A driver
that closes stdin with a prompt outstanding fails the turn, exactly as with an
unanswered question.

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
Shift+Tab, and since 56a moved between turns by a `set` line's
`permission_mode` ([above](#the-sessions-own-doors-detach-set-compact)).

- `manual`, the default, asks as above.
- `accept-edits` runs the file and note edits unasked.
- `plan` refuses every tool that writes, saying so in the tool result.
- `dont-ask` refuses whatever would have been a `question`. It is the mode for
  a driver that must never be asked.
- `bypass` asks nothing.

A config `deny` refuses in every mode. `bypass` is chosen at launch, or by a
`set` line in a session launched allowing it: in the terminal and the shell
`/permission-mode bypass` asks the person once, and a machine-mode child has
nobody to ask that of.

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
newest 50 (`id`, `status`, `interrupted`, `rounds_used`, `rounds_budget`,
`goal`), `total` every task. Since M32 every row carries `interrupted`, decided
by the view's own rule: `true` for a task its ledger says is planning or
running with no process left to run it (killed mid-round, and resumable), as
`task status` says it, and `false` on every other row — so a host draws a
killed task's row as interrupted, not running, without reading its status. A
read given `stream-json`, or any word but `text` and `json`, is refused.

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
none, with `versions` then naming `peft` too; since 53l `engine` -- `torch`
(a diffusers pipeline, through diffusers) or `mlx` (an MLX video model,
mlx-video's build of Wan's original layout, through mlx-video's own Wan
generator) -- the engine the model's directory runs on, which `--engine
auto|torch|mlx` only names or is refused by, and which a `--from` rerun
replays; a record without it ran on `torch`. An `mlx` run's `model` is its
store handle and directory, `class` `Wan <mode>` (its config's `model_type`:
`t2v`, `i2v`, `ti2v`), `ref` the Wan set it was converted from, `variant`
null; its `device` is MLX's (`gpu`) with `dtype` its quantization (`4-bit`,
or empty when unstated); its `versions` carry `mlx` and `mlx_video` beside an
empty `torch` and `diffusers`; and its `loras` are applied through
mlx-video's own loader, with no `peft`. A pipeline's record is unchanged but
for `engine`. The record carries `error` when the clip did not
land. The clip and its sidecar are written only on `done`; a failed run exits `1` and a
cancelled one `130`, the output path untouched either way. The narration --
the device and the scale to expect, then each step -- goes to stderr. A run
refused before it starts (a cap, an existing output, no pipeline, an engine
its model does not run on, a runtime not installed) prints no document: one line on stderr naming the fix, exit
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
`created_at`; and `error` when it did not land. Since 53m `engine` --
`torch` (a voice model as Kokoro publishes it, through kokoro) or `mlx`
(mlx-audio's build of one, through mlx-audio's own Kokoro) -- the engine the
voice model's directory speaks on, which `voice say --engine auto|torch|mlx`
only names or is refused by; a record without it spoke on `torch`. An `mlx`
run's `model` is its store handle (`<model>/mlx/<id>`) and directory, `ref`
the voice model it was converted from, `checkpoint` its weights' file; its
`device` is MLX's (`gpu`); its `versions` carry `mlx` and `mlx_audio` beside
an empty `torch` and `kokoro`; and its `voice` is one of the presets the
build carries -- `af_heart` by default, as on PyTorch. Its segments are
measured from their own WAVs' headers exactly as PyTorch's are. A PyTorch
record is unchanged but for `engine`. The composer's narration records
(`<video>.scenes/sNN.wav.json`) are the same documents, each on its voice
model's own engine. Exit `1` on a failure, `130`
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
run over a plan already kept makes no call at all, and says it reads the plan
(`reading the plan a dry run kept at …`), never that it renders (55c).
Captions this ffmpeg cannot burn are kept beside the video as `<video>.srt`,
never over a file already there that is not the project's own (its
`captions.file`): one refuses every run that would render -- a fresh run, a
resumed one, the plan a dry run kept -- before anything runs.
An edit is the same verb:

```bash
apogee video compose --from video.mp4 --edit "<instruction>" -o edited.mp4 --output-format json
```

Its scene diff is said on stderr before anything renders; `--dry-run`
prints the edited project with `status` `planned`, its scenes' `from` and
`changes` the diff as data, and renders and writes nothing. Exit `1` on a failure and `130` on a cancel, every finished
scene kept either way: the same command again resumes it, rendering only
what the document does not hold, with the project's own settings -- a flag
asking for other than the project has is said `not applied`, one naming
what it has is not (55c). An edit project resumed with
an `--edit` or `--redo-scene` it does not hold last runs that edit over the
kept project -- the kept plan is the one it rewrites (55c): the diff, its
scenes' `from` and `changes` still read against the project's `parent`, the
edit added to its `edits`, what the kept project made carried and the rest
rendered -- or, with `--dry-run`, printed `planned` with nothing written. An
edit that cannot run over the kept project is refused by name: a
`--redo-scene` for a scene it does not have, a kept project whose scenes
record no `from`. A refusal before the plan
prints no document, one line on stderr naming the fix.

Since 27l the graph navigation verbs are reads of the same kind:

```bash
apogee graph path <from> <to> [--directed] [--relation calls] --output-format json   # {"object":"graph.path",…}
apogee graph explain <node> --output-format json                                       # {"object":"graph.node",…}
apogee graph neighbors <node> [--relation R] [--direction in|out] --output-format json # {"object":"graph.neighbors",…}
apogee graph query "<question>" --output-format json                                   # {"object":"graph.query",…}
```

Each takes `--graph <name>` or `--collection <name>` (neither: the one graph
built). Each document is byte for byte what the `graph` tools return — to a
model in a turn, and to any MCP client of `apogee mcp serve` — and what `GET
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

Since M31 `graph dedupe` answers with its control-plane twin's document --
a pass, not a read, so it merges unless `--dry-run` is given -- with the same
selection:

```bash
apogee graph dedupe <name> [--dry-run] [--threshold T] --output-format json   # {"groups":[…],"merged_nodes",…}
```

It is byte for byte the body `POST /v1/admin/graph/{id}/dedupe` serves, from
the one pass both run: `groups` (each `kept`, `kept_type` and the `merged`
names), `merged_nodes`, `threshold`, `dry_run`, `code_entities` and
`code_identity_merges`, and `prose_skipped` when the vector pass had nothing
to compare; the shape is in
[http-api.md](http-api.md#post-v1admingraphiddedupe). A threshold outside
`(0, 1]`, or a graph not built, exits `1` with the message on stderr and
nothing on stdout. The text says the same groups as before.

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
apogee chats list --output-format json      # {"object":"list","data":[{"id","updated","turns","name","backend","model","origin"},…]}
apogee chats info <id|name> --output-format json     # {"object":"chat.info","id",…,"messages","compactions",…,"cwd"}
apogee chats recall --output-format json    # {"object":"list","data":[{"chat","name","updated","summary"},…],"index","pending"}
apogee agents list --output-format json     # byte for byte GET /v1/admin/agents
apogee mcp list --output-format json        # GET /v1/admin/mcp-servers's entries, with what connecting found
apogee mcp presets --output-format json     # {"object":"list","data":[{"name","description","provides","registers_into_apogee","command","args","runnable",…}]}
apogee acp registration --output-format json  # {"object":"acp.registration","where","name","executable","interpreter":null,"arguments":[…],"environment":{…},"channel","xcode_minimum","approval"}
apogee check --output-format json           # {"rows":[{"section","name","status","detail","remedy"?}],"ok":…,"failures":N,"warnings":N}
apogee providers scan --output-format json  # {"object":"list","data":[{"provider","type","label","tier","backend","evidence":[…]}]}
apogee providers register <id> --output-format json  # {"object":"provider.registration","provider",…,"registration":[…],"rosters":[…]}
apogee system --output-format json          # {"cpu":{…},"memory":{…},"apogee":{…},"gpu":{…},"disk":{…}}
apogee knowledge list --output-format json  # {"object":"list","db":…,"data":[records]}
apogee knowledge info <id> --output-format json      # the record, as --json prints it
apogee embed list --output-format json      # {"object":"list","data":[{"name","chunks","sources","error"?}]}
apogee embed info <name> --output-format json        # {"collection","path","schema","chunks","vectors":{…}|null,"index","sources":[…]}
apogee graph stats <name> --output-format json       # {"object":"graph.stats","graph","kind","built",…}
apogee graph show <name> <entity> --output-format json   # {"object":"graph.show","match",…,"data":[entities]}
apogee train status --output-format json    # byte for byte GET /v1/admin/training/status
apogee train versions [backend] --output-format json   # byte for byte GET /v1/admin/training/versions
apogee train pipeline status [<id>] --output-format json   # byte for byte GET /v1/admin/training/runs?kind=pipeline, or …/runs/{id}
apogee train cycle status --output-format json   # byte for byte GET /v1/admin/training/cycle
apogee datasets list --output-format json   # byte for byte GET /v1/admin/datasets
apogee datasets info <name> --output-format json       # byte for byte GET /v1/admin/datasets/{name}
apogee datasets kits --output-format json   # byte for byte GET /v1/admin/datasets/kits
apogee train regime list --output-format json        # {"object":"list","data":[…],"problems":[…]}
apogee train regime show <name> --output-format json # {"object":"training.regime",…}
apogee train pipeline list --output-format json      # {"object":"list","data":[…]}
apogee train pipeline show <name> --output-format json   # {"object":"training.pipeline",…}
apogee auth list --output-format json       # byte for byte GET /v1/admin/auth: metadata only
```

Five of these are also MCP tools since 54a: `apogee mcp serve` answers
`models_list`, `models_status`, `train_status`, `train_versions` and `check`
with these same documents, byte for byte, less the newline that ends the
command's stdout ([tools.md](tools.md#apogee-as-an-mcp-server-apogee-mcp-serve)).

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
  reads `drafter` from its files, before the vision rule. Since M22 a directory
  with no record -- a set a pull brought ready-made -- reads `video` where a
  `config.json` sits beside `t5_encoder.safetensors` and `vae.safetensors`
  (mlx-video's layout), `voice` where a `config.json` naming Kokoro
  (`model_type: kokoro`, or its `istftnet` and `plbert`) sits beside
  `voices/*.safetensors`, and `embedding` where a `modules.json` names a
  sentence-transformers Pooling module -- after the drafter rule and before the
  vision rule, and only where exactly one of the three says so: files that say
  none of them, or two, read as before. A word this build does not
  know is carried as written. Only `chat` and `vision` are what an `mlx`
  backend chats with: a row of any other kind has, in place of the backend's
  summary, a `note` saying what it is and what uses it (an `mlx` backend's row
  says first why its runtime cannot run, as before), and `config scan
  --register` registers it as nothing. Since 53h an `embedding` directory is
  the one exception: an `mlx` backend embeds with it, so `config scan
  --register` registers it as an `mlx` entry for the `embedding` role, and
  such a backend's row is not marked for attention -- its `note` says what it
  is and how wide its vectors are. Since 55d the kind decides the role at every
  door: `config add-backend`, `config set-backend` and the `config set-default*`
  verbs refuse an `mlx` entry over any other kind, and the backend's own ladder
  turns away one written by hand -- its row's `state` `wrong kind` (a drafter's
  `a drafter`, as since 53i), its `note` the kind and what uses it. A backend
  whose directory is missing, and every other row, has no `kind`. Since M32
  an `mlx` backend's row whose entry names a drafter (`draft:`, 53i) carries
  `draft`: `{path, can_draft, refusal?}`, following `kind`'s shape. `path` is
  the drafter's directory as the entry names it, `~` and variables expanded;
  `can_draft` is whether it drafts beside the entry's model, decided by the
  rule `models info`'s `draft` field and the backend's own ladder answer by --
  a drafter there, beside a base mlx-vlm built of its family and width (the
  one its record names, where it names one), and the installed mlx-vlm
  drafting its type; and `refusal`, present only when it cannot, is why, with
  its fix where one command is it -- the very words `models info`'s `draft`
  field says after `cannot draft beside it: `. A drafter deleted from under
  its pairing is `can_draft: false`, its `refusal` saying there is no drafter
  at its path. An entry that names no drafter (or has no `model_path`), and
  every other row, has no `draft`; each stream-json line carries it the same.
- **`models info`** and **`models status`**: their `label: value` record as
  `fields`, in order, each label as printed and a value's indented continuation
  joined to it with a newline. The values are the same words the human view
  prints. Since M23 an `mlx` backend's `models info` record has a `hearing`
  field after its `vision` one -- `hears recordings as they are -- mlx-vlm
  <version>`, or `no -- <rung>: <why>` -- and `models status`'s
  `transcription` line on an `mlx` entry says whether it hears, as its
  `vision` line says whether it sees. Since 52e `models info` on one stored
  set (`<model>/<format>/<id>`, or its id) adds a typed **`data`** beside the unchanged `fields`: `format`
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
  none installed, or one without that drafter, it stays `[]`. Since 53k Wan's
  original layout's is `["mlx"]` -- an MLX video model, never a GGUF --
  exactly where its `wan_layout` is `whole` and mlx-video is installed in
  Apogee's Python environment (its Wan converter's module and the `torch` it
  loads checkpoints through, read from their files, never imported), its
  `mlx_conversion.converter` `mlx-video`; anywhere else it stays `[]`. Since M21, an embedding model's
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
  model it builds; since 53k `mlx-video` for Wan's original layout; since 53m
  `mlx-audio` for a voice model) and
  `text_only` (whether `--mlx --text-only` is offered:
  never for a language model, which has no tower to leave out, nor an
  embedding model).
  It is `null` where `mlx` is not offered, and on every other format; the
  text says the same for a set with towers in a `towers:` line. Since 53e an MLX set's
  `data` also carries `kind` — the same word, from the same reader, as its
  `models list` row's `kind` above — and the text gains a `kind:` line saying
  the word, where it came from (`recorded`, `inferred from its files`, or `no
  kind recorded`) and what runs it; `kind` is `null` on every other format. An
  `mlx` backend's own record has the `kind:` line too. Since 53k a `video`
  directory -- mlx-video's build of Wan's original layout -- is read by its
  own files (its `config.json`, `t5_encoder.safetensors`, `vae.safetensors`
  and the transformer's weights, each whole; no tokenizer), and its `window`
  is `null`: a video model has no token window. Since 53m a `voice`
  directory -- mlx-audio's build of Kokoro -- is read by its own files too
  (its `config.json`, the weights and every `voices/*.safetensors` pack, each
  whole; no tokenizer), its `window` `null` as well, and its text lists its
  voices by language in a `voices:` line, as a voice model's does. Since 53m
  a voice model's `data` (the `voice` format) also carries `converts_to`
  where mlx-audio is installed in Apogee's Python environment -- its
  converter's module, its Kokoro package and the `torch` Kokoro's checkpoint
  is read through, read from their files, never imported: `["mlx"]` for a
  whole voice model, an MLX voice model and never a GGUF, its
  `mlx_conversion.converter` `mlx-audio` (`towers` `[]`, `text_only`
  `false`), and `[]` for one that is not whole; without mlx-audio both stay
  `null`, as before. Since 53h an MLX set's
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
  the set and what runs the model on each of `video generate`'s engines: its
  Diffusers pipeline on PyTorch, and its MLX conversion through mlx-video --
  said made, by handle, where an MLX video model converted from the set (its
  record's `ref` naming the set, its `transform` mlx-video's converter) stands
  in the store (M24). A backend's document has no `data`.
  Since M31, with a suite active, `models status` adds a typed
  **`footprint`** beside the unchanged `fields` -- whose `footprint` field
  keeps its text, word for word -- from the same reading: `suite`;
  `members`, one per backend the suite puts to use, in role order, each
  `backend`, `roles`, `local` (whether its memory is on this machine),
  `bytes` (what the total counts of it: its whole figure -- weights, unless
  it `shares` another member's base, then cache, adapters and its drafter --
  when `known`, else only what is known of it, its drafter's weights, or `0`;
  `0` for a member holding nothing here), `known`, `shares` (the member
  whose loaded base it runs on, `""` for its own) and `note` (the reason the
  text gives in place of a figure -- why its size is unknown, or a backend
  configured nowhere -- `""` when it gives none); `total_bytes`, the members' `bytes`
  and the `margin_bytes` held back once for the set (`0` both when nothing is
  held here); `budget_bytes`, the machine's memory for models, `null` when it
  is not known or not asked; and `verdict`, one of `fits`, `fits as far as
  known`, `does not fit`, `budget not known here` and `no member holds
  memory`. The bytes are exact, where the text rounds each part to MiB. With
  no suite active there is no `footprint`.
- **`chats list`**: newest first, each conversation's `id`, `updated`, `turns`
  and `name`, and since 52a the `backend` it runs on and the roster `model` it
  pinned there with `-m` or `/model` — empty when it runs on the backend's own
  model — as its session file records them. Since 56c, `origin`: who began
  it -- the `clientInfo.name` of the ACP host whose `apogee acp` session it is
  ([acp.md](acp.md#saved-sessions)), `acp` for a host that sent none -- and
  `""` for a chat begun anywhere else. It is set when the chat begins and never
  rewritten by a resume; a binary older than 56c that resumes such a chat
  writes it back without `origin` and `cwd`, since it saves only what it
  knows, and the chat then reads as one begun elsewhere. The text row ends
  `  (from <origin>)` only when it is set.
- **`chats info`** (since M31): one conversation's details, `object`
  `chat.info` -- `chats list`'s row (`id`, `updated`, `turns`, `name`,
  `backend`, `model`), then what the text says beside it: `custom_name` (the
  name `chats title` or `/title` gave it, `""` for none), `title` (its
  generated title), `started`, `messages` (how many its history holds),
  `compactions`, `temperature` and `max_tokens` (`null` where unset, the lines
  the text leaves out) and `schema_version` (`0` for a file written before
  the schema existed, the text's `legacy`), and since 56c `cwd`, the folder
  an ACP host began it in, as its `session/new` named it (`""` for any other
  chat; `origin` rides the row). The text gains `origin:` and `cwd:` lines
  only when they are set. `backend` is `""` where the text
  says `(default)`. A name no chat answers is refused as the text refuses it:
  `apogee chats: no chat named '<name>'` on stderr, exit `1`, nothing on
  stdout.
- **`chats recall`** (since M31): the summaries recall holds of earlier
  chats, newest first by `updated` (undated last) -- each `chat` (the id
  `chats show` and `--resume` take), the `name` and `updated` time the index
  recorded when it summarised the chat (what the `memory` event names it by,
  so a later rename shows once the chat is summarised again), and `summary`,
  whole: the text the model is handed under its date-and-title line. `index`
  and `pending` are the count line's other facts -- the index's path, and how
  many chats are marked due a summary. `data` is `[]`, never null, when recall
  holds none. An index an older Apogee wrote reads as it is: each summary is
  taken from its indexed text, nothing migrated. The text keeps its count
  line (`recall holds 2 chat summaries in …`), then a line per summary: its
  chat, date and name. `--forget` answers in text, and is refused with
  `--output-format json`. Recall is the command line's alone (26l): no route
  serves it.
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
  Since 56d a third preset, `xcode` -- `xcrun mcpbridge`, `xcrun` by its
  absolute path, `runnable` on macOS only (`missing`: `Xcode runs on macOS
  only`) -- and a preset that needs something done outside Apogee carries
  `note`, in the words the listing prints under it: for `xcode`, Xcode's
  "Allow External Agents to Use Xcode Tools" switch, the open project, and
  Xcode's approval of the binary, asked again after every install or update.
- **`acp registration`** (since 56d): the fields an ACP host's "add an agent"
  takes -- Xcode 26.6 and later's Settings › Intelligence › Agents › Add an
  Agent -- as `acp registration` prints them: `where` (that menu path),
  `name` (`Apogee`, `Apogee (dev)` or `Apogee (test)`, by the build's
  `channel`), `executable` (the running binary's absolute path, symlinks
  resolved -- never `~` or a bare name), `interpreter` (`null`: the
  executable is a binary), `arguments` (one entry a line in the sheet: a root
  flag -- `--dev`, `--test`, `--release`, `--custom <absolute config>` -- and a
  typed `--config <absolute path>` ahead of `acp`, and the flags `acp` itself
  was given after it), `environment` (an object: `APOGEE_HOME` and
  `APOGEE_CONFIG`, absolute, exactly when set, and `PATH` when set -- an app
  opened from the Dock hands its children launchd's `PATH`, on which no
  subscription CLI is found), `xcode_minimum` (`26.6`) and `approval`, the
  sentence a host shows about Xcode's approval of an ad-hoc-signed binary.
  It reads nothing of Xcode's and writes nothing; a flag the protocol owns,
  given to `acp` before `registration`, is refused in `acp`'s words.
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
  does>`. Since M23 the `MLX` section's `runtime`, `vision` and `embeddings`
  rows say each set's installed version against its floor, and two rows join
  them, `video` (mlx-video, its commit against its pin) and `voice`
  (mlx-audio) -- read from the environment's `*.dist-info` records, nothing
  run: `warn` below a floor or off the pin, its `remedy` `apogee train setup
  --with <set>`; `skipped`, detail `not installed (opt-in) -- …`, for an
  opt-in set not installed. An `mlx` entry's `Config` row says whether it
  hears beside whether it sees, and `default_transcription` on an `mlx`
  entry is held to the same rungs, its detail the one that refuses. Since
  56d an `Xcode` section, after `MCP`: on macOS `xcode` (`ok`, `Xcode 27.1
  (27A9275), <developer directory>`; `skipped` when none is installed, only
  the Command Line Tools are selected -- `remedy` `sudo xcode-select -s
  /Applications/Xcode.app` when that app is there -- a selected one is gone,
  or Xcode's command-line tools would not answer within 5 seconds), then,
  for an Xcode, `mcpbridge` (`ok` when `xcrun --find mcpbridge` finds it,
  Xcode 26.3+, naming the server that runs it or `apogee mcp create xcode
  --from xcode`; else `skipped`), `acp agents` (`ok` from Xcode 26.6, naming
  `apogee acp registration`; else `skipped`) and `approval` (`ok`, shown
  beside either: Xcode approves an agent by its signature and hash, and an
  ad-hoc-signed binary is asked about again after every install). Off macOS
  the section is one `skipped` row, `xcode`. Never `warn` or `fail`: Xcode is
  optional. It asks Xcode's command-line tools only -- `xcode-select -p`,
  then `xcrun` -- and reads none of Xcode's settings.
- **`providers scan`** (since 37b): a row per provider in the knowledge table, as the human scan prints them -- `provider` (`claude`), `type` (`claude-cli`), `label` (`Claude CLI`), `tier` (`credentials found`, `verified 2026-10-06`, `not found`), `backend` (which configured backends reach it, `not registered` when it could be, else empty) and `evidence`, a line each. It is the explicit scan, so it may start a vendor's binary for its version as the human scan does. With `--register`, what registering did is the document's `registration` and the rosters it fetched its `rosters`, a line each; with `--refresh`, the rosters re-fetched are `rosters` too.
- **`providers register <id>`** (since M33): one provider the last scan found, registered by the pass `scan --register` runs -- the core the shell's Providers view runs on `r` -- over the provider cache, never a scan of its own. Its document is `object` (`provider.registration`), `provider` (the id), and `scan --register`'s `registration` and `rosters`, a line each: `registered claude (claude-cli)`, `models.default set to 'claude' (none was set)` when the config set none, or `claude: already registered as 'claude' -- nothing written`; when it registers, the rosters of the configured types not yet fetched are fetched on this word, as M13 rules and `scan --register` does. An id the knowledge table does not hold, a machine never scanned, or no config file is refused on stderr, exit `1`, nothing written; no key passes through it -- keys stay `apogee auth add`'s.
- **`knowledge list`** and **`knowledge info`** (since 37c): the records the
  human list counts, newest first and kept to `--status`/`--discipline` as it
  is, each record the object `--json` prints (which stays, a bare array for
  `list`); `db` names the collection read. Since M31 `knowledge info --raw`
  carries `raw` beside the record, under `--output-format json` and `--json`
  alike: the archived conversation, or `null` where the archive cannot be
  read (the text's `Raw conversation: (unavailable)`); without `--raw` there
  is none. The text is unchanged -- the conversation after its `-- Raw
  conversation --` line.
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
- **`train pipeline status`** and **`train cycle status`** (since 55h): the
  bodies the control plane serves, from the same builders. Without an id, every
  pipeline run -- a cycle pass's among them -- newest first, `{"object":
  "list", "data": [{kind: "pipeline", id, spec_name, status, stages, passed,
  started_at[, completed_at]}]}`, `data` `[]` and never null; with one, that
  run's manifest whole, `{"kind": "pipeline", "pipeline": {pipeline_run_id,
  spec_name, student, base_model, status, started_at[, completed_at], stages:
  [{index, name, run_id, base_model, dataset, adapter_dir, fused_dir, status,
  cumulative_score, cumulative_passed[, eval_results]}]}}` -- each stage's
  `status` the word a run's `stage` events carry. An id no pipeline run
  answers is refused as the text refuses it, and so is one that is not a plain
  name. The cycle's is its history, `{backend, anchor_version, anchor_score,
  consecutive_fails, total_runs, halted, halt_reason, sessions_until, runs:
  [{run_at, source, dataset_rows, pipeline_id, gate, gate_reason, cycle_score,
  prev_score, anchor_score, promoted_version[, note]}], active}`, `active`
  whether a pass holds the lock now. **Until the first pass has written the
  history there is no document**: the read is refused (`no cycle history yet`,
  exit `1`), as the route answers `404`, where the text view says `no cycle
  runs yet` -- `train status`'s `cycle` is `null` until then. Whether a cycle
  is configured at all is the config's to say.
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
- **`train pipeline list`** and **`train pipeline show`** (since M30): the
  pipelines a `train pipeline run --pipeline` can name, the config's
  `training.pipelines:` entries, by name -- each `name` (the entry's, what a
  run takes), `path` (`null` for an entry; `show` also reads a spec file, as
  a run does, and names it here), `student` as the spec names it, and
  `stages` in order: each `name`, `dataset`, `eval_suite`, `gate` -- the
  suites its cumulative gate holds it to, every earlier stage's then its own,
  at 100% -- and its tuning as written: `method` (`""`: lora), `iters`,
  `batch_size` and `num_layers` (`0`: the trainer's default),
  `grad_checkpoint`, `mask_prompt` and `rehearsal_fraction`. `list` is
  `{"object":"list","data":[…]}`, `data` `[]` and never null when none is
  named; `show` is one pipeline as `{"object":"training.pipeline",…}`. A name
  nothing answers is refused as a run refuses it. No route serves them:
  training is run, and read, on the host.
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

Since M27 a saved chat's turns read the same way -- what a host draws of a
conversation it resumes, or shows without resuming it:

```bash
apogee chats show <id|name> --output-format json   # {"object":"chat","id",…,"attachments":[…],"turns":[…],"warnings":[]}
```

The document is `id`, `name`, `backend` and `model` as `chats list`'s row
says them, `title` (the generated title, `""` when it has none),
`attachments` -- every attachment the chat holds, in the order attached, each
`{name, files, bytes}` -- and `turns`, the whole conversation in order. Each
entry is a **turn** -- a user message and everything after it up to the next
-- or a **compaction**, where it happened:

- `turn`: the conversation's turn count once the entry ended -- the count a
  resumed session numbers on from. The last entry's is one before the
  `next_turn` that `chat --resume <id>`'s `session` event announces, and an
  answered turn carries the number its `user` line was given. A turn left
  unanswered (cancelled at a terminal, or failed) and a compaction add
  nothing: each carries the number before it. The turns a compaction folded
  are in the count, not in `turns`.
- `user`: `text`, the user's words (`""` on a compaction), and `attachments`,
  what rode the message, each `{name, files, bytes}`: media the message
  carries itself, named by its media type (`image/png`) or its URL, then what
  the chat attached on it. An attachment is named, never its bytes -- no
  image's data is in the document. One retrieved rather than inlined rides no
  message, and a compaction clears what rode the messages it folded; the
  chat's `attachments` names every one. `bytes` is `null` where the history
  does not say (an image named by URL).
- `answer`: `text`, the model's words -- each message's, a blank line
  between, what it said before a tool call too -- and `tools`, each call in
  order as `{name, summary, result_bytes}`: `summary` its arguments in one
  line (compact JSON, clipped), `result_bytes` the size of what it returned,
  `null` when the history keeps no result. A result itself is never here.
- `answered`: whether the history holds the model's answer -- `false` for a
  turn left unanswered, and for a compaction that kept none.
- `compacted`: on a compaction alone, `{summary}`, the summary that stands in
  for the turns before it; its `answer` is the last answer the compaction
  kept.

And `warnings` (since 55b): what of the file the loader could not read and left
out -- a message it skipped -- each `{kind, message}` (`kind`
`field_dropped`), `[]` when it read everything. A turn missing from `turns` is
said here, not only on stderr: under `--output-format json` stderr carries
none of it; the text view says it on stderr as before.

Nothing the transcript never held is here: no thinking, and no injected
context -- retrieved excerpts, an inlined attachment's text, recall. The
system prompt is a setting, not a turn. A private chat (`chat --private`) is
never written, so there is nothing to show. A name no chat answers is refused
as `chats info` refuses it: `apogee chats: no chat named '<name>'` on stderr,
exit `1`, nothing on stdout.

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
