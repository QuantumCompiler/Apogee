# Claude CLI backend: persistent child process with token-level streaming

**What / why.** A fifth backend: Claude driven through the official `claude` CLI as a long-lived child process — the **subscription-auth path** (a user's existing Claude plan) beside the direct API-key path already shipped in `backends/anthropic.h` ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone D). It reuses that item's typed `ThinkingSink` seam (`harness/provider.h`) and its IR mapping, but **not** its HTTP client or SSE parser — this backend speaks JSONL over pipes to a child process, not HTTPS. This reverses the plan's original "claude CLI deleted by design" divergence (revised 2026-08-24 by user decision). **The full design notes are the appendix at the bottom of this document** — read it before building; it carries the invocation flags, the wire-event table verified against CLI 2.1.233, the reader discipline, and the structured-output characterization, and it is the spec where this summary and it disagree. The shape: **one child per chat session, not per turn** (`claude -p --input-format stream-json --output-format stream-json --verbose --include-partial-messages`), JSONL user messages written to stdin, a reader thread turning stdout JSONL into the same typed events the other backends emit — `--include-partial-messages` is what produces token deltas and is the flag that fixes message-granularity choppiness. One-shot side work (auto-titling, schema-constrained clerks) runs on its own short-lived `claude -p` invocation, never the session child (the SideRequest isolation rule, same as llama.cpp KV). Process-start flags (system prompt, effort, allowed tools, MCP config) changed mid-session restart the child with `--resume <session_id>`, transparently. Crash recovery: capture `session_id` from every `result` event, respawn with `--resume`; when resume fails, replay Apogee's own transcript into a fresh child — **Apogee's transcript stays authoritative; the CLI session is an optimization, never the source of truth.** Structured output is a distinct entry point (`complete_structured(prompt, schema)` via `--json-schema`, which the notes characterize as a post-answer forced tool call costing roughly one extra generation): in schema mode the adapter suppresses TextDelta (holding the prose as fallback), forwards thinking normally, and emits `structured_output` from the terminal `result` — falling back to the held prose if the field is absent. Auth is configuration, not a hardcoded choice: `mode: subscription` (no `--bare`; the user's CLI login) vs `mode: bare` (`--bare`: reproducible, API-key/helper only, no hooks/MCP/CLAUDE.md discovery). Adds a `claude-cli` backend type to the config enum — a recorded widening event per config-engine's closed-enum constraint (fields: `binary` resolved from PATH if unset, `mode`).

**Core constraint(s).**
- **Never read credentials:** the harness never opens `~/.claude`, never implements OAuth, never parses the CLI's session files off disk. The CLI authenticates; Apogee only spawns it. Crash recovery goes through `--resume`, which is exactly why disk-reading is never needed.
- **Never modify or bundle the binary:** resolve `claude` from PATH (or explicit config path); the user installs and logs in themselves; no built-in auth method is suppressed.
- **Environment inherited wholesale** — never inject `ANTHROPIC_API_KEY` from harness config unless the user explicitly set it there, and document loudly that it overrides subscription auth (the stale-key-redirects-a-Max-plan-to-per-token-billing support trap; surface the variable by name in auth-related error messages).
- **Adapter boundary is total:** the rest of the harness never sees a `stream_event` — wire JSON maps to the typed event union at the adapter and nowhere else. Unknown wire `type`s are allowlist-ignored (log-and-drop), never errors: an unknown-type crash turns a CLI upgrade into an outage.
- **stderr never merges into stdout** (diagnostics interleaved into JSONL break the parser); separate reader, bounded ~8 KiB tail surfaced on non-zero exit — the same rule the terminal UX layer's OnNotice startup discipline enforces (the terminal UX layer (shipped 2026-08-26; `lib/src/cli/source/commands/thinking_view.h`)).
- **Thinking is display-only** (already a harness-wide rule): never persisted, never in programmatic returns, never in served responses.
- A persistent child process is not a listening socket — the interactive-never-listens invariant holds and stays covered by the existing lsof test.

**Seam + files.** lib/src/cli/source/backends/claude_cli.h/.cpp (LLMProvider impl: `posix_spawn` with three pipes + CLOEXEC on parent ends — never `popen`; mutex-guarded single-writer stdin; lifecycle: flag-change restart via --resume, shutdown by closing stdin and draining stdout to EOF with a ≥30s budget — the CLI drains its backlog rather than truncating, and a 5s kill cuts off tail output), lib/src/cli/source/backends/claude_cli_reader.cpp (reader thread: `poll()` + `read()` on the **raw fd** — a buffered `FILE*` reintroduces the exact 4 KiB chunking this design removes; carry buffer for lines split across reads; generous line sizing — a real `init` event exceeds 64 KiB with MCP servers configured; skip lines whose first non-space byte isn't `{`), lib/src/cli/source/backends/claude_cli_events.cpp (wire→event mapping per the design notes' verified table: `text_delta`→answer tokens, `thinking_delta`→OnThinking, `system`/`thinking_tokens` `estimated_tokens`→the spinner's token-estimate path, `result`→turn completion carrying session_id/structured_output/cost; framing events ignored), structured entry point wired to the loop's structured-output seam, config schema + template additions (`claude-cli` type, `binary`, `mode`), lib/src/cli/tests/backends/claude_cli_test.cpp + tests/fixtures/*.jsonl replay.

**Reference (Ommi).** Ommi's claude.go was a per-request `claude -p` spawn — this item deliberately does NOT port that shape: the persistent child with `--input-format stream-json` is what removes per-turn startup/MCP-init cost, and `--include-partial-messages` is what Ommi's message-granularity streaming lacked. Ommi's StreamClaudeTurn/native_tools/allowed_tools special-casing stays un-ported: this backend emits the same typed events as every other provider, so the loop and surfaces need no special cases. The design notes' §6 typed-sink recommendation is already satisfied by Apogee's harness (typed OnThinking — no in-band `<thinking>` markers, no demux filter).

**Decisions made** (dated):
- 2026-08-24 — Backend re-added by user decision, adopting the design notes (proven against another harness, wire schema verified on CLI 2.1.233) as the spec. Persistent-child-per-session, not per-turn spawns; typed events at the adapter boundary; transcript-authoritative resume.
- 2026-08-25 — The design notes, previously a standalone `lib/documentation/assistant/claude-cli-streaming-backend.md`, were **folded into this document** (appendix below) by user decision: they are item spec, not a standing contributor doc, and `assistant/` holds only the five standing docs. The rendering half (notes §8) went to the terminal UX layer (shipped 2026-08-26; `lib/src/cli/source/commands/thinking_view.h`), which owns the terminal UX layer; the two credential/binary policy rules were promoted to [SPEC.md](../assistant/SPEC.md) → Principles, where they bind every vendor-CLI backend rather than just this one.
- 2026-08-24 — Placed at the top of the gated ring rather than v0.1.0 (the release already carries four backends; this slots into the same seams with no new infrastructure debt).
- 2026-08-24 — This item is the template for the whole vendor-CLI family ([vendor-cli-backends.md](vendor-cli-backends.md): codex, gemini, ollama) — extract the child-process/reader machinery reusably rather than claude-specifically.

**Open calls:**
- [user] Earmark into v0.1.0 after all, or ship in the first ring release? (It needs only harness-core/agentloop seams that v0.1.0 builds anyway.)
- [default: nlohmann/json line-at-a-time, per the skeleton's standardized pick] JSON parsing: the notes bless simdjson `iterate_many` for NDJSON, but throughput is irrelevant next to model latency — switch only if parsing ever profiles
- [default: synchronous — the value arrives whole on `result`] Whether `complete_structured()` accepts a streaming sink (appendix §12)
- [default: cost_usd meaningful only for this backend; local reports zero] `TurnComplete.cost_usd` semantics across backends. Local models get cache reuse across turns via the persistent process while the CLI manages its own caching, so cost accounting is not symmetric between backends (appendix §12)
- [default: store `session_id` in Apogee's own state file] Session persistence across harness restarts — storing the id ourselves is fine and involves no reading of Claude's session storage (appendix §12)

**Guardrail(s).** The fixture-replay suite from the notes §11, adopted wholesale: recorded real-session JSONL fixtures (simple text, thinking with payloads, **redacted thinking** — empty `thinking_delta` + `thinking_tokens` events, structured-output two-turn run, tool use, api_retry, max-turns error, hand-truncated mid-object) fed to the parser in adversarial chunk sizes (1 byte / 3 bytes / 4096 / whole file) asserting an identical event sequence every time — this catches the whole carry-buffer framing bug class offline. Plus: a kill-the-child mid-session test asserting respawn-with-`--resume` and the replay-own-transcript fallback; a grep test that no path reads under `~/.claude`; the schema-mode test that prose is suppressed, held, and only used when `structured_output` is absent.

**Acceptance criteria:**
- [ ] A multi-turn chat streams token-level deltas from one persistent child (no per-turn spawn), with the second turn's latency visibly free of process startup — verified live and via fixtures
- [ ] Redacted-thinking models never open an empty thinking view; the spinner runs on `estimated_tokens` alone (fixture-tested — the empty-payload guard is load-bearing)
- [ ] Killing the child mid-session resumes via `--resume` with history intact; a failed resume falls back to replaying Apogee's own transcript, and the conversation continues either way
- [ ] `complete_structured` returns schema-conforming JSON from `structured_output`, suppresses the prose pre-answer from display, and degrades to the held prose if the field is missing
- [ ] `mode: subscription` and `mode: bare` both work as configured; auth errors name `ANTHROPIC_API_KEY` when it is the likely cause; the harness never touches `~/.claude` (test-locked)
- [ ] Mid-session system-prompt/effort changes restart the child with `--resume` transparently; shutdown drains queued output (≥30s budget) rather than truncating
- [ ] The adversarial chunk-size replay suite passes byte-for-byte identically at every chunk size

**Scope note.** gated ring, first position (ring convention: assumes the complete v0.1.0 set; no ring-internal gate). Out of scope here: the thinking *renderer* itself (rolling window + collapse) — that is the terminal UX layer's job, and the terminal UX layer (shipped 2026-08-26; `lib/src/cli/source/commands/thinking_view.h`) now carries those notes; this backend only feeds it typed events.

**⚠ On ship, preserve the appendix.** The usual flow deletes this document when the item ships. Do not simply drop the appendix with it: [vendor-cli-backends.md](vendor-cli-backends.md) (codex / gemini / ollama) templates off the process model, reader discipline, and testing shape, and [stdio-machine-mode.md](stdio-machine-mode.md) reuses the event vocabulary. Carry the appendix into the MILESTONES.md entry for this work — MILESTONES is the permanent record and explicitly welcomes this level of detail — and repoint those two documents at it in the same change.

---

# Appendix — Claude CLI streaming: design and wire characterization

*Folded in 2026-08-25 from `lib/documentation/assistant/claude-cli-streaming-backend.md`. Sections marked **[verified 2.1.233]** were checked empirically against that CLI build on 2026-08-24 by dumping real sessions and reading the bytes; everything else is design. **The stream schema has moved between releases — re-verify against your own `claude --version` before relying on a field name.***

*Provenance: the structured-output and rendering sections were written after implementing this against a real harness (Ommi, Go). Where a recommendation exists because the obvious approach failed, that is called out — those are the parts worth reading twice.*

## 1. Scope and constraints

Two inference mechanisms, structurally different:

| Backend | Mechanism | Streaming source |
|---|---|---|
| Local models | llama.cpp linked in-process | token callback on a worker thread |
| Claude | official `claude` CLI as a child process | newline-delimited JSON on a pipe |

One is a function pointer, the other a byte stream from another process, and **the single most important design decision is to not let that difference reach the rest of the harness.** Both adapters write into one sink (§6); everything above the adapter layer sees an ordered stream of `Event`.

The two hard policy constraints — never read the CLI's credentials, never modify or bundle its binary — now live in [SPEC.md](../assistant/SPEC.md) → Principles, because they bind every vendor-CLI backend and not just this one. They are also restated as Core constraints above.

Non-goal: reimplementing the agent loop. The Agent SDK is Python and TypeScript only; for other languages, invoking the CLI as a subprocess is the documented path. We are a caller, not a reimplementation.

## 2. Why naive output is choppy

Two independent causes, both fixable:

- **Per-invocation spawn.** Launching `claude -p` per turn pays process startup, config discovery, and MCP server initialisation every time. Fix: one long-lived child (§4).
- **Message-granularity chunking.** Plain `--output-format stream-json` emits one JSON object per *message*, so assistant text arrives in whole blocks rather than tokens. Fix: `--include-partial-messages` (§3).

If you only change one thing, change the second.

A third cause is not the CLI's fault: even with perfect token streaming, dumping everything the model emits — including extended thinking — into the terminal produces output that *feels* worse than choppy. It feels like noise. That is the renderer's problem, and it lives in the terminal UX layer (shipped 2026-08-26; `lib/src/cli/source/commands/thinking_view.h`).

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

- `--output-format stream-json` **requires** `--verbose`. Without it the CLI rejects the combination.
- `--include-partial-messages` is what produces token deltas. This is the flag people miss.
- `--input-format stream-json` makes stdin accept JSONL user messages, which is what allows one process to serve many turns.
- `--json-schema` constrains the turn's result to a JSON Schema — see §9, which has the non-obvious part.
- `--bare` skips discovery of hooks, skills, plugins, MCP servers, and `CLAUDE.md`. It makes runs reproducible and is the right mode for scripted invocation. **It also disables subscription auth** — see §5.

**[verified 2.1.233]** All the above flags exist in this build. `--session-id <uuid>` also exists, so a caller may pre-name a session rather than capturing the CLI's minted id — useful only if your own session ids are UUIDs; do not build a mapping layer just to use it.

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

**Spawn.** `posix_spawn` with three pipes, or `fork`/`exec` if you need finer control of the child environment. Set `CLOEXEC` on the parent ends. Do not use `popen` — it gives you one direction and a `FILE*` you do not want (§7).

**Environment.** Inherit the user's environment wholesale. Do not inject `ANTHROPIC_API_KEY` from harness config unless the user explicitly set it there, and if you do, document loudly that it overrides subscription auth — a stale key silently redirecting a Max plan to per-token billing is the single most common support complaint in this area.

**Turn submission.** One JSON object per line to the child's stdin, flushed after each. Because stdin stays open you can also inject additional user messages while a response is in flight — useful for a "wait, actually…" affordance. Guard the write end with a mutex; only one thread writes.

**Keep one-shot work one-shot.** Not every model call is a turn of the session. Background summarisation (auto-titling), classification, and schema-constrained extraction share no prefix with the conversation and may run *concurrently* with a real turn. Route those to their own short-lived `claude -p` invocation rather than the session child. The same rule applies to a local backend's prompt cache, for the same reason: a side request that writes into session state corrupts it.

**Flag changes mid-session.** System prompt, permission mode, effort, allowed tools, and MCP config are all *process-start* flags. If the UI lets the user change one mid-conversation, the child must be restarted with `--resume <session_id>` — transparent, and far better than silently ignoring the change until the next session.

**Shutdown.** Close stdin, drain stdout until EOF, then wait. The CLI drains queued output before exiting rather than truncating, scaling its wait with the backlog up to roughly 30 seconds. Budget for that — a 5-second kill will cut off tail output under load.

**Crash recovery.** Capture `session_id` from every `result` event. On unexpected child exit, respawn with `--resume <session_id>` rather than replaying history yourself. This is why you never need to read session files off disk.

**Keep your own transcript authoritative.** The CLI's session is an optimisation — it saves re-sending history and preserves server-side prompt caching. It is not the source of truth. When a resume fails, fall back to replaying Apogee's own transcript into a fresh child. A conversation that dies because a session id went stale is a worse failure than one slow turn.

## 5. Authentication modes

Configuration, not a hardcoded choice — the two modes are mutually exclusive and different users need different ones.

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

Precedence gotcha worth surfacing in error messages: in subscription mode, if `ANTHROPIC_API_KEY` is present in the environment and approved, the API key wins over the subscription login. If a user reports unexpected charges, that variable is the first thing to check.

Cloud provider auth (Bedrock, Google Cloud, Microsoft Foundry) reads its own provider credentials and works under either mode.

## 6. Event model

A tagged union at the adapter boundary. Do not pass JSON objects around the harness — the point of the adapter is that the rest of the code never learns what a `stream_event` is.

```cpp
namespace harness {

struct TextDelta      { std::string text; };
struct ThinkingDelta  { std::string text; };
struct ThinkingTokens { int estimated; };            // text-redacted progress
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

**Keep `ThinkingDelta` a distinct variant — do not merge it into `TextDelta`.** If the provider interface is a plain token channel rather than a typed sink, you will be tempted to wrap thinking in in-band markers like `<thinking>…</thinking>` and demux downstream. That works — it is what a channel-typed harness is forced into — but it costs a filter that must tolerate markers split across reads, and it creates a permanent hazard: a *literal* `<thinking>` in the answer text is now ambiguous. Apogee's typed harness gets this for free.

### Wire events to map

**[verified 2.1.233]** — observed in a real `--include-partial-messages` session (text turn with a schema, so the tool-call turn appears too):

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

Ignore unknown `type` values rather than erroring. New event types get added; an unknown-type crash turns a CLI upgrade into an outage. Note how many observed types are *framing* — a strict allowlist that logs-and-drops the rest is the right shape, not a switch that assumes it has seen everything.

## 7. Reading and parsing

### Read the fd, not a `FILE*`

The child's stdout is a pipe. Wrap the read end in a fully-buffered `FILE*` and glibc will hold up to 4 KiB before handing you anything — reintroducing the exact chunking you set out to fix. Use `poll()` + `read()` on the raw fd.

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

The `carry` buffer is the part that gets skipped and then causes intermittent parse failures under load. A single `read()` will routinely land mid-object.

**Size the line buffer generously.** The `init` event alone can exceed 64 KiB once a user has MCP servers configured, and a schema echo or large tool result goes further. A fixed 64 KiB cap will work on your machine and fail on a user's.

**Tolerate non-JSON lines on stdout.** Login notices and similar have been observed interleaved with the JSONL. Skip any line whose first non-space character is not `{` rather than treating it as a parse failure.

**Note on the platform seam:** `poll`/`read`/`posix_spawn` are POSIX. Per the six-target matrix this goes behind `lib/src/cli/source/platform/` with a Windows equivalent (overlapped I/O or a reader thread on the pipe handle), or a recorded skip — not an inline `#ifdef`.

### JSON

**simdjson** is a natural fit — `iterate_many` is built for newline-delimited JSON and fast enough that parsing never shows up in a profile. **nlohmann/json** is fine parsing one line at a time, and it is Apogee's standardized pick; the throughput difference is irrelevant next to model latency. Treat every field as optional either way: a missing `cost_usd` should produce a zero, not a thrown exception mid-stream.

## 8. Rendering thinking

Moved to the terminal UX layer (shipped 2026-08-26; `lib/src/cli/source/commands/thinking_view.h`) — the terminal UX layer owns it. This backend only emits typed events. Two facts from that section matter here, because they are wire behavior rather than rendering:

**[verified 2.1.233]** Thinking arrives in two distinct forms, and the adapter must handle both:

1. **Thinking text streams.** `thinking_delta` events carry real reasoning in `delta.thinking`.
2. **Thinking text is redacted.** The newest models default to `display: "omitted"` at the API level. You still get `thinking_delta` events — but with an **empty** `thinking` field, followed by a `signature_delta`. The only live signal is `system` / `thinking_tokens` carrying `estimated_tokens`.

Case 2 is why the empty-payload guard is load-bearing: a blank `thinking_delta` that opens the thinking UI renders an empty reasoning block on every redacted turn.

**Display only, always.** Thinking must never leak into persisted conversation history, the text returned to a programmatic caller, or a served API response. Strip it at the boundary: it bloats every subsequent prompt if it re-enters history, and an OpenAI-format client does not expect reasoning in its content field.

## 9. Structured output (`--json-schema`)

For calls wanting a typed result rather than prose — classification, extraction, routing, any "clerk" that must return a record:

```
claude -p --output-format json --json-schema '{"type":"object", …}' "…"
```

**[verified 2.1.233]**, and the details are not what the flag's name suggests:

- **Inline JSON is accepted.** No temp file; pass the schema bytes directly.
- **The conforming value arrives in `structured_output`** on the `result` object, as a real JSON value. The `result` string field carries the same value serialised, so it remains a usable fallback.
- **It composes with `--output-format stream-json`.** An earlier draft advised skipping streaming for schema calls; that turned out to be unnecessary.
- **But it is implemented as a forced tool call *after* the answer.** The model first produces a normal prose answer, then the CLI makes it call an internal `StructuredOutput` tool with the conforming value. On the streaming path: the `text_delta` events are a **prose pre-answer**, the schema value appears only on the terminal `result` event, and thinking streams for *both* turns.
- **Budget one extra generation.** A trivial schema call reported `num_turns: 3`. Calling a schema-constrained clerk once per document chunk roughly doubles the cost of the job. Worth it where a malformed parse would waste the whole generation anyway; measure before applying it to a hot loop.

Consequences for the adapter:

```
schema mode:  suppress TextDelta  →  hold the prose as fallback
              forward ThinkingDelta / ThinkingTokens normally
              on `result`: emit structured_output if present,
                           else emit the held prose
```

Suppressing the prose matters: without it the user watches a full paragraph get streamed only to be replaced by a JSON object. Holding it as fallback matters too — if a future CLI stops populating `structured_output`, the clerk degrades to the prose-parsing path instead of returning nothing.

Expose this as a distinct entry point — `complete_structured(prompt, schema)` — rather than a flag threaded through the conversational path. The two have different failure modes and different cost profiles.

## 10. Threading

```
  llama.cpp token callback ─┐
                            ├──→ [ SPSC / MPSC queue ] ──→ consumer (UI, HTTP, log)
  CLI reader thread ────────┘
```

- One reader thread per child process, owning the fd and the `carry` buffer.
- One writer path, mutex-guarded, for stdin.
- A separate stderr reader. **Do not merge stderr into stdout** — diagnostics interleaved into the JSONL will break the parser at the worst possible time. Log it separately and surface it on non-zero exit; retain a bounded tail (8 KiB is plenty) so a spawn failure can be reported with what the child actually said.
- Push into a **bounded** queue with the same `Event` type from both backends. Bounded matters: if the consumer stalls you want back-pressure in your own process, not unbounded memory growth.

Cancellation: set an atomic flag, close stdin, let the `poll()` timeout notice. Avoid killing the child mid-turn — a clean stdin close lets it emit its `result` event, which is where the cost accounting lives.

**Terminal writes are shared state too.** If a spinner thread and the reader thread both draw, they will interleave and tear the line. Serialise every terminal write under one mutex. Not theoretical — a spinner repainting at 200 ms against a token stream is exactly the collision case.

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

The replay harness feeds bytes in adversarial chunk sizes — 1 byte, 3 bytes, 4096 bytes, whole file — and asserts the emitted `Event` sequence is identical in every case. This catches the entire class of framing bugs with no network call and no API charge, and it is the suite that will actually save you when the CLI's schema shifts.

The renderer half of this section moved to the terminal UX layer (shipped 2026-08-26; `lib/src/cli/source/commands/thinking_view.h`) with §8.

## 12. Open items

Resolved since the first draft: `--json-schema` is characterised (§9 — a post-answer forced tool call, composes with streaming, costs an extra generation), and the thinking-render question is settled (rolling window plus collapse, made safe by pre-wrapping — now in the terminal UX layer (shipped 2026-08-26; `lib/src/cli/source/commands/thinking_view.h`)).

Still open — each carried into **Open calls** above with a default:

- Whether `complete_structured()` should also accept a streaming sink, or stay synchronous.
- Prompt-cache interaction and whether `TurnComplete.cost_usd` is meaningful for local models or always zero.
- Session persistence across harness restarts.
- Whether the thinking window's height should be user-configurable — carried to the terminal UX layer (shipped 2026-08-26; `lib/src/cli/source/commands/thinking_view.h`), which owns the renderer.

## References

- Headless mode and stream formats — https://code.claude.com/docs/en/headless
- Authentication — https://code.claude.com/docs/en/authentication
- Legal and compliance — https://code.claude.com/docs/en/legal-and-compliance
