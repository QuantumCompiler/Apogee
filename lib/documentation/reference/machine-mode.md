# Machine mode: the JSONL protocol

Apogee's contract with a front-end. A GUI powers Apogee by **spawning the
executable and speaking over its pipes** — never over localhost. `serve` remains
the surface for genuine server deployments, where a remote client makes REST
calls to Apogee running on a host; a local front-end uses this instead.

This document is the reference for that protocol. The worked example is
[`reference_driver.py`](../../src/cli/tests/reference_driver.py), which is also
the test that keeps this document true.

## The two flags

```bash
apogee complete --output-format stream-json "what is 2+2?"
```

```bash
apogee chat --output-format stream-json --input-format stream-json
```

`--output-format` selects the event stream on stdout. `--input-format` selects
JSONL user turns on stdin, which is what makes one `chat` child serve a whole
conversation. It defaults to whatever `--output-format` is, so a driver may pass
one flag; on `chat` the two directions **must agree**, and disagreeing is an
error rather than a silently ignored flag.

## Reading the stream

One JSON object per line on stdout. Every object has a `type`.

```jsonl
{"type":"session","protocol_version":1,"model":"claude-sonnet-5"}
{"type":"thinking"}
{"type":"thinking_delta","text":"…"}
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
| `thinking` | The model began reasoning. No text. |
| `thinking_delta` | A chunk of reasoning. **Droppable** — see below. |
| `tool_status` | A tool is running, described in `text` for display. |
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

## Writing to the child

One JSON object per line on stdin.

```jsonl
{"type":"user","text":"what is 2+2?"}
{"type":"answer","text":"Yes"}
```

An unrecognised line is ignored rather than fatal — the tolerance this protocol
asks of drivers, honoured in the other direction.

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

## What machine mode does not do

- **It opens no sockets.** Not one, ever — asserted in CI with `lsof`. Only
  `apogee serve` owns a port.
- **It does not push.** Events arrive in response to turns, never unprompted.
- **It does not represent slash commands.** `/model`, `/compact` and the rest are
  terminal-REPL affordances; a driver uses the CLI commands and its own UI.
