# Claude CLI Streaming Backend — Design Notes

A design and implementation guide for driving Claude Code from a C++ harness
with token-level streaming, a persistent child process, and a single event sink
shared with in-process llama.cpp inference.

**Status:** design notes. Sections marked **[verified 2.1.233]** were checked
empirically against that CLI build on 2026-08-24 by dumping real sessions and
reading the bytes; everything else is still design. The stream schema has moved
between releases, so re-verify against your own `claude --version` before
relying on a field name.

**Provenance note:** the rendering and structured-output sections below were
written after implementing this against a real harness (Ommi, Go). Where a
recommendation exists because the obvious approach failed, that is called out —
those are the parts worth reading twice.

---

## 1. Scope and constraints

The harness supports two inference backends:

| Backend | Mechanism | Streaming source |
|---|---|---|
| Local models | llama.cpp linked in-process | token callback on a worker thread |
| Claude | official `claude` CLI as a child process | newline-delimited JSON on a pipe |

These are structurally different — one is a function pointer, the other is a
byte stream from another process — and the single most important design
decision is to **not let that difference reach the rest of the harness.** Both
adapters write into one sink interface (§6). Everything above the adapter layer
sees an ordered stream of `Event`.

Two hard constraints, both carried over from the policy discussion:

1. **Never read credentials.** The harness does not open `~/.claude`, does not
   implement an OAuth flow, and does not parse session files off disk. The CLI
   authenticates; the harness only spawns it. This is the line that separates
   "user runs Claude Code" from "third-party product routes subscription
   credentials," and it is the one part of the policy that is unambiguous.
2. **Never modify or bundle the binary.** Resolve `claude` from `PATH` (or an
   explicit config path) and require the user to install and log in themselves.
   Do not suppress any of the CLI's built-in auth methods.

Non-goal: reimplementing the agent loop. The Agent SDK is Python and TypeScript
only; for other languages, invoking the CLI as a subprocess is the documented
path. We are a caller, not a reimplementation.

---

## 2. Why the current output is choppy

Two independent causes, both fixable:

- **Per-invocation spawn.** Launching `claude -p` per turn pays process startup,
  config discovery, and MCP server initialisation every time. Fix: one
  long-lived child (§4).
- **Message-granularity chunking.** Plain `--output-format stream-json` emits
  one JSON object per *message*, so assistant text arrives in whole blocks
  rather than tokens. Fix: `--include-partial-messages` (§3).

If you only change one thing, change the second.

A third cause is not the CLI's fault and is the subject of §8: even with
perfect token streaming, dumping everything the model emits — including its
extended thinking — into the terminal produces output that *feels* worse than
choppy. It feels like noise.

---

## 3. Invocation

```
claude \
  -p \
  --input-format stream-json \
  --output-format stream-json \
  --verbose \
  --include-partial-messages \
  [--json-schema <inline-json-or-path>] \
  [--bare] \
  [--resume <session_id>]
```

Flag notes:

- `--output-format stream-json` **requires** `--verbose`. Without it the CLI
  will reject the combination.
- `--include-partial-messages` is what produces token deltas. This is the flag
  people miss.
- `--input-format stream-json` makes stdin accept JSONL user messages, which is
  what allows one process to serve many turns.
- `--json-schema` constrains the turn's result to a JSON Schema — see §9, which
  has the non-obvious part.
- `--bare` skips discovery of hooks, skills, plugins, MCP servers, and
  `CLAUDE.md`. It makes runs reproducible across machines and is the recommended
  mode for scripted invocation. **It also disables subscription auth** — see §5.

**[verified 2.1.233]** All of the above flags exist in this build. `--session-id
<uuid>` also exists, so a caller may pre-name a session rather than capturing
the CLI's minted id — useful only if your own session ids are UUIDs; do not
build a mapping layer just to use it.

---

## 4. Process model

One child process per session, not per turn.

```
                   ┌──────────────────────────────┐
   turn request →  │ stdin  (JSONL user messages) │
                   │                              │
                   │        claude -p             │
                   │                              │
   reader thread ← │ stdout (JSONL events)        │
   stderr thread ← │ stderr (diagnostics)         │
                   └──────────────────────────────┘
```

**Spawn.** `posix_spawn` with three pipes, or `fork`/`exec` if you need to
manipulate the child environment more precisely. Set `CLOEXEC` on the parent
ends. Do not use `popen` — it gives you one direction and a `FILE*` you do not
want (§7).

**Environment.** Inherit the user's environment wholesale. Do not inject
`ANTHROPIC_API_KEY` from harness config unless the user explicitly set it there,
and if you do, document loudly that it overrides subscription auth — a stale key
silently redirecting a Max plan to per-token billing is the single most common
support complaint in this area.

**Turn submission.** Write one JSON object per line to the child's stdin,
flushing after each. Because stdin stays open, you can also inject additional
user messages while a response is in flight — useful for a "wait, actually…"
affordance in the UI. Guard the write end with a mutex; only one thread writes.

**Keep one-shot work one-shot.** Not every model call is a turn of the session.
Background summarisation (auto-titling a conversation), classification, and any
schema-constrained extraction share no prefix with the conversation and may run
*concurrently* with a real turn. Route those to their own short-lived `claude
-p` invocation rather than the session child. The same rule applies to a local
backend's prompt cache, for the same reason: a side request that writes into
session state corrupts it.

**Flag changes mid-session.** System prompt, permission mode, effort, allowed
tools, and MCP config are all *process-start* flags. If your UI lets the user
change one mid-conversation, the child must be restarted with `--resume
<session_id>` — transparent to the user, and far better than silently ignoring
the change until the next session.

**Shutdown.** Close stdin, drain stdout until EOF, then wait. The CLI drains
queued output before exiting rather than truncating, scaling its wait with the
size of the backlog up to roughly 30 seconds. Budget for that in your shutdown
timeout — a 5-second kill will cut off tail output under load.

**Crash recovery.** Capture `session_id` from every `result` event. On
unexpected child exit, respawn with `--resume <session_id>` rather than
replaying history yourself. This is why you never need to read session files
off disk.

**Keep your own transcript authoritative.** The CLI's session is an
optimisation — it saves you re-sending history and preserves server-side prompt
caching. It is not your source of truth. When a resume fails, fall back to
replaying your own transcript into a fresh child. A conversation that dies
because a session id went stale is a worse failure than one slow turn.

---

## 5. Authentication modes

Expose this as configuration, not a hardcoded choice, because the two modes are
mutually exclusive and different users need different ones.

```yaml
claude:
  binary: claude          # resolved from PATH if unset
  mode: subscription      # subscription | bare
```

| | `mode: subscription` | `mode: bare` |
|---|---|---|
| Flags | no `--bare` | `--bare` |
| Credentials | whatever the user's CLI is logged into | `ANTHROPIC_API_KEY` or `apiKeyHelper` only |
| Keychain / OAuth | used | never read |
| `CLAUDE_CODE_OAUTH_TOKEN` | honoured | **not** read |
| Hooks / MCP / `CLAUDE.md` | discovered | skipped |
| Best for | an individual on their own machine | CI, servers, reproducible runs |

Precedence gotcha worth surfacing in your error messages: in subscription mode,
if `ANTHROPIC_API_KEY` is present in the environment and approved, the API key
wins over the subscription login. If a user reports unexpected charges, that
variable is the first thing to check.

Cloud provider auth (Bedrock, Google Cloud, Microsoft Foundry) reads its own
provider credentials and works under either mode.

---

## 6. Event model

Define a tagged union at the adapter boundary. Do not pass JSON objects around
the harness — the point of the adapter is that the rest of the code never learns
what a `stream_event` is.

```cpp
namespace harness {

struct TextDelta      { std::string text; };
struct ThinkingDelta  { std::string text; };
struct ThinkingTokens { int estimated; };            // text-redacted progress (§8)
struct ToolUseStart   { std::string id, name; };
struct ToolResult     { std::string id; bool is_error; std::string content; };
struct TurnComplete {
    std::string session_id;
    std::string final_text;
    std::string structured_output;   // raw JSON, empty when no schema was set (§9)
    double      cost_usd   = 0.0;
    int         num_turns  = 0;
    bool        is_error   = false;
    std::string error_subtype;       // empty on success
};
struct Notice        { std::string kind, detail; };  // init, api_retry, …

using Event = std::variant<
    TextDelta, ThinkingDelta, ThinkingTokens,
    ToolUseStart, ToolResult, TurnComplete, Notice>;

class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void on_event(Event&&) = 0;
};

} // namespace harness
```

Both backends target `EventSink`. The llama.cpp adapter emits `TextDelta` from
its token callback and one `TurnComplete` at the end; the Claude adapter
translates wire events into the same union. Your UI, your logger, and your
`ommi`-style HTTP layer all bind to `EventSink` and stay backend-agnostic.

**Keep `ThinkingDelta` a distinct variant — do not merge it into `TextDelta`.**
If your provider interface is a plain token channel (`chan string`,
`std::function<void(std::string_view)>`) rather than a typed sink, you will be
tempted to wrap thinking in in-band markers like `<thinking>…</thinking>` and
demux them downstream. That works — it is what a channel-typed harness is
forced into — but it costs you a filter that must tolerate markers split across
reads, and it creates a permanent hazard: a *literal* `<thinking>` in the
model's answer text is now ambiguous. With a typed sink you get this for free.
Take the typed sink.

### Wire events to map

**[verified 2.1.233]** — observed in a real `--include-partial-messages` session
(text turn with a schema, so the tool-call turn appears too):

| Wire `type` | `subtype` / `event.type` | Meaning | Maps to |
|---|---|---|---|
| `system` | `init` | session start, model, tools, session id | `Notice` |
| `system` | `status` | periodic transport status | ignore |
| `system` | `thinking_tokens` | running estimate, field `estimated_tokens` | `ThinkingTokens` |
| `system` | `post_turn_summary` | end-of-turn accounting | ignore |
| `system` | `api_retry` | transient API failure being retried | `Notice` |
| `rate_limit_event` | — | rate-limit state change | `Notice` (or ignore) |
| `stream_event` | `message_start` / `content_block_start` / `content_block_stop` / `message_delta` / `message_stop` | block framing | ignore |
| `stream_event` | `content_block_delta`, `delta.type == "text_delta"` | answer tokens, field `delta.text` | `TextDelta` |
| `stream_event` | `content_block_delta`, `delta.type == "thinking_delta"` | reasoning tokens, field `delta.thinking` | `ThinkingDelta` |
| `stream_event` | `content_block_delta`, `delta.type == "signature_delta"` | thinking signature, no payload | ignore |
| `stream_event` | `content_block_delta`, `delta.type == "input_json_delta"` | tool-call arguments streaming in | ignore, or feed a tool-args view |
| `assistant` | — | a complete assistant message | usually ignore (already streamed) |
| `user` | — | tool results being fed back | `ToolResult` |
| `result` | `success` / error subtypes | terminal event: `session_id`, `result`, `structured_output`, `total_cost_usd`, `num_turns`, `usage` | `TurnComplete` |

Ignore unknown `type` values rather than erroring. New event types get added;
an unknown-type crash turns a CLI upgrade into an outage. Note how many of the
observed types are *framing* — a strict allowlist that logs-and-drops the rest
is the right shape, not a switch that assumes it has seen everything.

---

## 7. Reading and parsing

### Read the fd, not a `FILE*`

The child's stdout is a pipe. If you wrap the read end in a fully-buffered
`FILE*`, glibc will hold up to 4 KiB before handing you anything and you will
have reintroduced the exact chunking you set out to fix. Use `poll()` + `read()`
on the raw fd.

```cpp
void ReaderThread::run() {
    std::string carry;               // partial line across reads
    char buf[16 * 1024];

    for (;;) {
        struct pollfd p{fd_, POLLIN, 0};
        int rc = ::poll(&p, 1, 250);          // timeout lets us observe cancel_
        if (rc < 0 && errno == EINTR) continue;
        if (cancel_.load(std::memory_order_relaxed)) break;
        if (rc == 0) continue;

        ssize_t n = ::read(fd_, buf, sizeof buf);
        if (n == 0) break;                     // EOF: child closed stdout
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }

        carry.append(buf, static_cast<size_t>(n));

        size_t start = 0, nl;
        while ((nl = carry.find('\n', start)) != std::string::npos) {
            std::string_view line{carry.data() + start, nl - start};
            if (!line.empty()) dispatch_line(line);
            start = nl + 1;
        }
        carry.erase(0, start);
    }

    if (!carry.empty()) dispatch_line(carry);  // unterminated final line
}
```

The `carry` buffer is the part that gets skipped and then causes intermittent
parse failures under load. A single `read()` will routinely land mid-object.

**Size your line buffer generously.** The `init` event alone can exceed 64 KiB
once a user has MCP servers configured, and a schema echo or a large tool result
goes further. A fixed 64 KiB line cap will work on your machine and fail on a
user's.

**Tolerate non-JSON lines on stdout.** Login notices and similar have been
observed interleaved with the JSONL. Skip any line whose first non-space
character is not `{` rather than treating it as a parse failure.

### JSON

**simdjson** is the right choice here — `iterate_many` is built for
newline-delimited JSON and is fast enough that parsing never shows up in a
profile. **nlohmann/json** is fine if you are parsing one line at a time and
prefer the ergonomics; the throughput difference is irrelevant next to model
latency, so pick on API taste.

Either way, treat every field as optional. Missing `cost_usd` should produce a
zero, not a thrown exception mid-stream.

---

## 8. Rendering thinking

This section is the one with the most hard-won detail, because the obvious
implementation is wrong in a way that only shows up after a few real turns.

### The wire behaviour first

**[verified 2.1.233]** Two distinct cases, and you must handle both:

1. **Thinking text streams.** `thinking_delta` events carry real reasoning in
   `delta.thinking`. You get the model's private deliberation, token by token.
2. **Thinking text is redacted.** The newest models default to
   `display: "omitted"` at the API level. You still get `thinking_delta`
   events — but with an **empty** `thinking` field, followed by a
   `signature_delta`. The only live signal is the `system` / `thinking_tokens`
   progress event carrying `estimated_tokens`.

Case 2 is what makes the empty-payload guard load-bearing: if a blank
`thinking_delta` opens your thinking UI, every redacted turn renders an empty
reasoning block. Guard on non-empty text before opening anything, and drive a
spinner from `ThinkingTokens` instead — `✻ Thinking… (5s · ~87 tokens)` is a
genuinely informative wait indicator built from nothing but that counter.

### Do not put reasoning in scrollback

The naive rendering — print a `✻ Thinking…` header, then stream the reasoning
underneath, then print the answer — is what we shipped first. It reads fine for
one turn and becomes unusable by the third: a conversation where every turn
leaves a 10-line internal monologue above a 2-line answer shows more
deliberation than content, and scrolling back through it to find what was
actually *said* is miserable. The user report that triggered the rewrite was
exactly this.

The behaviour to copy from the Claude CLI itself:

- **While thinking:** a header plus a **rolling window of the last N wrapped
  lines** (N = 2 works well), repainted in place as text arrives. The user sees
  that reasoning is happening and roughly what about, without it accumulating.
- **When the block completes:** erase the whole region and leave **one line** —
  `✻ Thought for 4s`, dimmed. That is what survives in scrollback.

### The constraint that makes it work

Our first design rejected collapsing for a specific reason, recorded at the
time as *"no cursor rewind over wrapped lines"* — and that reason was correct.
You cannot erase N printed lines with `\033[A` if you do not know how many
**screen rows** they occupied, and any line longer than the terminal width
occupies more than one. Get this wrong and the erase eats the user's prompt, or
leaves orphaned fragments.

The fix is to remove the unknown rather than to compute it: **never print a line
the terminal has to wrap.** Pre-wrap every row you paint to `width - indent`
before printing it. Then painted rows and screen rows are the same number by
construction, and the arithmetic is exact.

```cpp
// Every string in `lines` is already <= width-2 display cells.
void ThinkingView::repaint() {
    auto lines = wrap_tail(tail_, width_ - 2, kTailLines);   // last N, pre-wrapped
    erase();
    out_ << "✻ Thinking…";
    for (const auto& ln : lines) out_ << "\n  " << dim(ln);
    out_.flush();
    painted_ = 1 + lines.size();                 // header + tail rows
}

// Leaves the cursor at column 0 of the row the header occupied.
void ThinkingView::erase() {
    if (painted_ == 0) return;
    out_ << "\r\033[2K";
    for (int i = 1; i < painted_; ++i) out_ << "\033[A\033[2K";
    painted_ = 0;
}

void ThinkingView::finish() {
    if (!open_) return;
    open_ = false;
    erase();
    auto secs = std::max(1, elapsed_seconds(started_));
    out_ << dim("✻ Thought for " + std::to_string(secs) + "s") << "\n\n";
}
```

Note that `repaint()` deliberately leaves the cursor on the last painted row
with no trailing newline — `erase()`'s row count depends on it.

### Details that only surface in live use

- **Bound the retained text.** You display at most N lines; keeping the entire
  block is pointless. A few KB of tail is generous. Cut on a UTF-8 rune
  boundary, not a byte.
- **Drop blank lines from the window.** A paragraph break inside the reasoning
  will otherwise spend one of your two precious rows painting nothing. We found
  this on the first live run.
- **Wrap by codepoint, not byte.** Obvious, and still easy to get wrong when the
  tail is a `std::string`.
- **Blocks interleave.** A turn can go thinking → text → thinking → text. Each
  block opens and collapses independently, and `finish()` must be idempotent so
  the end-of-turn path can call it unconditionally.
- **Keep a verbose escape hatch.** `--verbose` should keep the full reasoning as
  a permanent transcript with no cursor movement and no collapse. Someone
  debugging a model's behaviour wants every word, and the collapsed view is
  actively hostile to that. Same rule as capturing a subprocess's stderr by
  default and restoring the firehose on demand.
- **Non-TTY: discard entirely.** On a pipe or redirect, emit no thinking, no
  escape codes, no summary line. Scripted output should be exactly the answer.
  Check this explicitly; it is the difference between a usable CLI and one that
  cannot be composed.

### Display only, always

Whatever you render, thinking must not leak into: persisted conversation
history, the text you return to a programmatic caller, or an API response your
harness serves. Strip it at the boundary. Two reasons — it bloats every
subsequent prompt if it re-enters history, and an OpenAI-format client does not
expect reasoning in its content field. Keep the rendering path and the
persistence path separate, and let only the rendering path see thinking.

---

## 9. Structured output (`--json-schema`)

For calls where you want a typed result rather than prose — classification,
extraction, routing decisions, or any "clerk" that must return a record — hand
the CLI a JSON Schema and let it enforce conformance:

```
claude -p --output-format json --json-schema '{"type":"object", …}' "…"
```

**[verified 2.1.233]**, and the details are not what the flag's name suggests:

- **Inline JSON is accepted.** No temp file needed; pass the schema bytes
  directly as the argument.
- **The conforming value arrives in `structured_output`** on the `result`
  object, as a real JSON value. The `result` string field carries the same value
  serialised, so it remains a usable fallback.
- **It composes with `--output-format stream-json`.** The original version of
  these notes advised skipping streaming for schema calls; that turned out to be
  unnecessary.
- **But it is implemented as a forced tool call *after* the answer.** The model
  first produces a normal prose answer, and the CLI then makes it call an
  internal `StructuredOutput` tool with the conforming value. On the streaming
  path this means: the `text_delta` events you receive are a **prose
  pre-answer**, the schema value appears only on the terminal `result` event,
  and thinking streams for *both* turns.
- **Budget one extra generation.** A trivial schema call reported `num_turns:
  3`. If you are calling a schema-constrained clerk once per document chunk,
  that roughly doubles the cost of the job. Worth it where a malformed parse
  would waste the whole generation anyway; measure before applying it to a hot
  loop.

The consequences for a streaming adapter:

```
schema mode:  suppress TextDelta  →  hold the prose as fallback
              forward ThinkingDelta / ThinkingTokens normally
              on `result`: emit structured_output if present,
                           else emit the held prose
```

Suppressing the prose matters: without it the user watches a full paragraph
answer get streamed, only to have it replaced by a JSON object. Holding it as a
fallback matters too — if a future CLI stops populating `structured_output`,
your clerk degrades to the prose-parsing path it had before instead of returning
nothing.

Expose this as a distinct entry point on the backend interface —
`complete_structured(prompt, schema)` — rather than a flag threaded through the
conversational path. The two have different failure modes and different cost
profiles.

---

## 10. Threading

```
  llama.cpp token callback ─┐
                            ├──→ [ SPSC / MPSC queue ] ──→ consumer (UI, HTTP, log)
  CLI reader thread ────────┘
```

- One reader thread per child process, owning the fd and the `carry` buffer.
- One writer path, mutex-guarded, for stdin.
- A separate stderr reader. Do not merge stderr into stdout — diagnostics
  interleaved into your JSONL will break the parser at the worst possible time.
  Log it separately and surface it on non-zero exit. Retain a bounded tail (8
  KiB is plenty) so a spawn failure can be reported with what the child actually
  said.
- Push into a bounded queue with the same `Event` type from both backends.
  Bounded matters: if the consumer stalls, you want back-pressure in your own
  process rather than unbounded memory growth.

Cancellation: set an atomic flag, close stdin, and let the `poll()` timeout
notice. Avoid killing the child mid-turn if you can — a clean stdin close lets
it emit its `result` event, which is where the cost accounting lives.

**Terminal writes are shared state too.** If a spinner thread and the reader
thread both draw, they will interleave and tear the line. Serialise every write
to the terminal under one mutex, including the thinking view's repaints and any
status animation. This is not theoretical — a spinner repainting at 200 ms
against a token stream is exactly the collision case.

---

## 11. Testing

Record real sessions to fixture files and replay them into the parser:

```
tests/fixtures/
  simple_text.jsonl
  thinking_text.jsonl          # thinking_delta with real payloads
  thinking_redacted.jsonl      # empty thinking_delta + thinking_tokens events
  structured_output.jsonl      # schema run: prose turn, then the tool-call turn
  tool_use.jsonl
  api_retry.jsonl
  error_max_turns.jsonl
  truncated_mid_object.jsonl   # hand-truncated, for the carry-buffer path
```

The replay harness should feed bytes in adversarial chunk sizes — 1 byte, 3
bytes, 4096 bytes, whole file — and assert the emitted `Event` sequence is
identical in every case. This catches the entire class of framing bugs without
a network call or an API charge, and it is the test suite that will actually
save you when the CLI's schema shifts.

**Test the renderer too, and do it by injecting the output stream.** Terminal
rendering gets waved off as "thin I/O, untestable" — it is not, and the escape
sequences are exactly where the bugs live. Give the view an `std::ostream&`
(or any sink), write to a string buffer in tests, and assert on the bytes:

- a scrolled-off line is absent from the final paint;
- a repaint begins with the correct number of `\033[A\033[2K` pairs;
- after `finish()`, the bytes following the last erase are *exactly* the
  collapsed summary — this is the assertion that proves no reasoning survived;
- two blocks in one turn produce two summaries;
- verbose mode emits no `\033[A` at all and no summary;
- a non-TTY / inactive view writes nothing whatsoever;
- the wrap helper: wrapping, last-N selection, blank dropping, width clamping,
  multibyte input.

For an end-to-end check, run the real binary under a pseudo-terminal, capture
the raw byte stream, and *replay the escape codes* in the test to reconstruct
the final screen. That is the only way to assert what the user is actually left
looking at.

---

## 12. Open items

Resolved since the first draft: `--json-schema` is now characterised (§9 — it is
a post-answer forced tool call, composes with streaming, costs an extra
generation), and the thinking-render question is settled (§8 — rolling window
plus collapse, made safe by pre-wrapping).

Still open:

- Whether `complete_structured()` should also accept a streaming sink, or stay
  synchronous. Currently it has no reason to stream — the value arrives whole.
- Prompt-cache interaction: local models get cache reuse across turns via the
  persistent process; the CLI manages its own caching, so cost accounting is
  not symmetric between backends. Decide whether `TurnComplete.cost_usd` is
  meaningful for local models or always zero.
- Session persistence across harness restarts — storing `session_id` in your own
  state file is fine and does not involve reading Claude's session storage.
- Whether the thinking window's height (N lines) should be user-configurable.
  Two is the Claude CLI's shape and needs no explanation; a setting invites
  someone to set it to 40 and recreate the problem §8 exists to solve.

---

## References

- Headless mode and stream formats — https://code.claude.com/docs/en/headless
- Authentication — https://code.claude.com/docs/en/authentication
- Legal and compliance — https://code.claude.com/docs/en/legal-and-compliance
