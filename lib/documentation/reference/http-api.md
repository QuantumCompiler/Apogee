# The HTTP API: `apogee serve`

Apogee's contract with a **remote client**. `apogee serve` runs on a server and
a client — a mobile or desktop app, a script, a stock OpenAI client library —
makes REST calls to it over the network. It is not for local front-ends: a GUI
on the same machine drives the CLI over its pipes instead
([machine-mode.md](machine-mode.md)), and never over a port.

This document is the reference for that contract. The route table it describes
lives in `httpserver/mux.cpp`, and `cli.http_api_conformance` keeps the two in
step in both directions.

## Starting the server

```bash
apogee serve                                   # 127.0.0.1:8080, models.default only
apogee serve --bind 0.0.0.0 --allow-remote     # a server deployment
apogee serve -m local --preload --tools        # one backend, loaded now, with fetch_url
apogee serve --all-backends --rag notes        # every servable backend, retrieval on
```

| Flag | Meaning |
|---|---|
| `--bind HOST` / `--port N` | Where to listen. Loopback by default; `--port 0` picks a free port and prints it. |
| `--allow-remote` | Permits a non-loopback `--bind`. **Without it the server refuses to start** — the inference plane is unauthenticated (for OpenAI-client compatibility), so exposing it is a decision the operator makes explicitly. Put it behind your own access control. |
| `-m BACKEND` / `--all-backends` | Which backends a request may name. The default (or `-m`) alone unless `--all-backends`: adding an entry to the config never silently exposes it. Vendor-CLI backends (`claude-cli`, `codex-cli`, `gemini-cli`, `ollama-cli`) are **never served** — they run on a personal subscription. |
| `--tools` | Run the tool loop server-side (`fetch_url`). Clients see only the final answer. |
| `--rag NAME`, `--rag-limit N`, `--retriever`, `--rerank` | Retrieval, fixed for the server's lifetime (`--rag ""` switches off the config's `auto_rag`). `?retriever=` and `?rerank=` override per request. |
| `--preload` | Load every served local model now rather than on the first request. |
| `--ignore-timeout` | Keep local models resident: ignore `idle_unload_seconds`. |
| `--session-ttl MINUTES` | How long a server-side session may sit idle before it leaves memory (default 60; 0 keeps them). |

The server serves **one inference at a time**. A second chat request waits for
the first; `/health`, `/v1/models` and the session routes answer meanwhile.

## Errors

Every refusal is the OpenAI error envelope, so a stock client library turns it
into its usual typed exception:

```json
{"error": {"message": "no backend named 'sonnnet' (serving: mock)", "type": "invalid_request_error"}}
```

| Status | `type` | When |
|---|---|---|
| 400 | `invalid_request_error` | A malformed body, an unknown or unserved model, a vendor-CLI backend, client-side `tools`, a bad `tool_mode` or `?retriever=`, an explicit retriever that cannot run |
| 404 | `not_found_error` | No such route, or a backend that reports no load state |
| 404 | `session_not_found` | An unknown or evicted `session_id`; the body also carries `session_id` |
| 405 | `invalid_request_error` | Wrong method; the `Allow` header names the right one |
| 502 | `backend_error` | The provider failed |
| 503 | `backend_unavailable` | A backend that is configured but could not be built, with its reason |

On a streamed response an error arrives as a `data:` frame carrying the same
`error` object, and the stream still ends with `[DONE]`.

## Routes

### `POST /v1/chat/completions`

The standard request, plus Apogee's extensions:

```json
{
  "model": "claude",
  "messages": [{"role": "user", "content": "hello"}],
  "stream": true,
  "temperature": 0.2,
  "max_tokens": 512,

  "system": "be terse",
  "tool_mode": "all",
  "apogee_events": true,
  "session_id": "new"
}
```

| Field | Meaning |
|---|---|
| `model` | A backend key, or an entry's `model:` id. Omitted: the served default. |
| `messages` | OpenAI messages. `content` may be a string or an array of parts (`text`, `image_url` with a `data:` URI). |
| `stream` | `true` for server-sent events (below); otherwise one `chat.completion` object. |
| `temperature`, `max_tokens` | Per request; otherwise the backend entry's values. |
| `system` | Shorthand for a leading system message. Merged in front of an existing one. |
| `tool_mode` | `all` (default: whatever `--tools` enabled) or `none`. **A client cannot send `tools`** — the server runs its own loop and returns only the final answer; a request with `tools` is a 400. |
| `apogee_events` | With `stream`, interleave status meta-frames (below). |
| `session_id` | `"new"` to mint a server-side session, a known id to continue one, absent to stay stateless. See **Sessions**. |

Query parameters: `?retriever=lexical|vector|hybrid|auto` and `?rerank=BACKEND|off`
override the server's retrieval settings for one request, through the same
validator and resolver the CLI flags use.

**The non-streamed response:**

```json
{
  "id": "chatcmpl-83d520e93a59da79",
  "object": "chat.completion",
  "created": 1789265585,
  "model": "claude",
  "choices": [{"index": 0, "message": {"role": "assistant", "content": "…"}, "finish_reason": "stop"}],
  "usage": {"prompt_tokens": 12, "completion_tokens": 3, "total_tokens": 15},
  "session_id": "20260913-021305-4bd4",
  "apogee_tool_calls": ["fetch_url"]
}
```

`finish_reason` is the provider's, in OpenAI's vocabulary: `stop`, `length` when the answer was cut short by `max_tokens` (a reasoning model that spends its whole budget thinking returns an empty answer **and** `length` — raise the budget), or `content_filter`.

`usage` is **absent** when the provider reported none — absent is not zero.
`session_id` appears when a session is active (also as the
`X-Apogee-Session-Id` header). `apogee_tool_calls` names the tools that ran,
in order (also as `X-Apogee-Tools-Used`).

**The streamed response** is `text/event-stream`: `data:` frames each carrying
one `chat.completion.chunk`, ending with `data: [DONE]`. The first chunk names
the role; the last carries `finish_reason: "stop"`, plus `session_id`,
`apogee_tool_calls` and `usage` when there are any.

```
data: {"id":"chatcmpl-…","object":"chat.completion.chunk","created":…,"model":"claude","choices":[{"index":0,"delta":{"role":"assistant","content":""},"finish_reason":null}]}

data: {"id":"chatcmpl-…","object":"chat.completion.chunk","created":…,"model":"claude","choices":[{"index":0,"delta":{"content":"Hello"},"finish_reason":null}]}

data: {"id":"chatcmpl-…","object":"chat.completion.chunk","created":…,"model":"claude","choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}

data: [DONE]
```

Reasoning never appears in a served response, streamed or not: a local model's
thinking is filtered at the source, and a cloud model's arrives on its own
channel and is dropped here.

#### Meta-frames (`apogee_events`)

With `"stream": true, "apogee_events": true`, status rides the stream as
**meta-frames**: a standard chunk whose `delta.content` is the empty string,
carrying a top-level `meta` object. A spec-compliant client appends nothing and
ignores the field it does not know; an event-aware client renders a status
indicator. The vocabulary is the same one the terminal status line shows.

```json
{"object": "chat.completion.chunk", "choices": [{"index": 0, "delta": {"content": ""}, "finish_reason": null}],
 "meta": {"type": "tool_call", "phase": "start", "name": "fetch_url"}}
```

| `meta.type` | `phase` | Extra fields | Meaning |
|---|---|---|---|
| `context_warning` | `start` | `name` (`warn` or `compact`), `used_tokens`, `context_size`, `usage_percent`, `detail: "estimated"` when the count is one | The prompt is at 80% (`warn`) or 90% (`compact`) of the window. A session is compacted before the turn; a stateless request is only warned. |
| `rag_search` | `start` / `done` | `name` = collection | Retrieval is running / has returned. |
| `rag_result` | `done` | `collection`, `chunks_found`, `top_score`, `retriever`, `reranked`, `detail` (notes) | What was injected. `retriever` sets `top_score`'s scale — lexical and vector scores are not comparable. |
| `model_loading` / `model_ready` | `start` / `done` | `name` = backend | A local model is loading; loading finished. |
| `thinking` | `start` | — | The model is working: the top of each loop iteration. |
| `tool_call` | `start` / `done` | `name` = tool | A server-side tool call. |
| `token_count` | `done` | `tokens`, `tokens_per_second`, `detail: "estimated"` when estimated | After the answer. |

### `POST /v1/completions`

The legacy text-completion shape: `prompt` (one string), `model`, `stream`,
`temperature`, `max_tokens`. One user message, no tools, no retrieval, no
session. Answers a `text_completion` object, or `text_completion` chunks ending
with `[DONE]`.

### `GET /v1/models`

The served backends, in the OpenAI list shape. Each entry carries two
extensions: `apogee_backend` (the config key) and `default` (which one answers
when a request names no model).

### `GET /v1/model/status`

Load state for served backends that report one — local models. `?backend=NAME`
asks about one (404 when it reports none). A cloud backend never appears here:
it has nothing to load.

### `GET /health`

`{"status": "ok"}`. Never calls a model.

## Sessions

Every cloud backend Apogee serves is stateless, so a client either re-sends its
whole history on every request or asks the server to keep it. **A session is
asked for, never implied:** a request without `session_id` is stateless, exactly
as a stock client expects, and leaves nothing behind on the server.

```bash
# Turn 1: ask for a session. The id comes back in the header and the body.
curl -i localhost:8080/v1/chat/completions -d '{
  "messages": [{"role": "user", "content": "My name is Ada."}], "session_id": "new"}'
# → X-Apogee-Session-Id: 20260913-021305-4bd4

# Turn 2: send ONLY the new message. The server remembers Ada.
curl localhost:8080/v1/chat/completions -d '{
  "messages": [{"role": "user", "content": "What is my name?"}],
  "session_id": "20260913-021305-4bd4"}'
```

- **`"session_id": "new"`** mints a session bound to the request's backend.
- **A known id** appends the request's messages to the stored history and
  dispatches the whole conversation.
- **An unknown id** is a `404` with `type: session_not_found` — an evicted
  session is a signal to start again, never a silent fresh context.

**A served session is a chat session.** The server persists the transcript after
every completed turn through the same file `apogee chat` writes, under the same
id, so a conversation started over HTTP continues from the terminal with
`apogee chat --resume <id>`. Retrieved context is spliced into the outgoing
request only and never into that transcript.

When the history nears the model's window the server **compacts** it before the
turn, the same way `apogee chat` does (a `context_warning` meta-frame with
`name: "compact"` says so). Idle sessions leave memory after `--session-ttl`
minutes; the transcript on disk stays.

### `GET /v1/sessions`

Every live session: `{session_id, model, turn_count, last_active}`.

### `GET /v1/sessions/{id}`

One session's `messages` (the same shape as a request's), with `turn_count` and
`compactions`. 404 when unknown.

### `DELETE /v1/sessions/{id}`

Ends the live session (`204`). The transcript on disk is untouched.

## The admin plane

Everything that *changes* configuration or data lives under `/v1/admin/`, behind
a bearer token. The public plane above stays open for OpenAI-client
compatibility; this one never is.

### Authentication

Every request under `/v1/admin/` carries:

```
Authorization: Bearer <token>
```

The token is a per-install secret generated at the first `apogee serve` as a
sibling of `config.yaml` (`~/.apogee/config/admin-token`, mode `0600`). Read it,
generating it if needed, with:

```bash
apogee serve --print-admin-token
```

- A missing or wrong token is `401` with `WWW-Authenticate: Bearer`, **whether
  or not the path exists** — the gate runs before routing, so an unauthenticated
  probe learns nothing about which routes there are.
- **Query-string tokens are refused**, even when correct: a query string lands in
  request logs. An SSE client therefore needs a fetch-style streaming reader
  that can set a header, not a bare browser `EventSource`.
- There is no separate bind flag for this plane. A non-loopback bind already
  needs `--allow-remote` (above), and the bearer is required on top of it.

Every write reads the config fresh from disk and reports `restart_required`:
`true` when the file now differs from what the running server started with, in
backend membership or the role pointers. The server does not hot-reload; a
backend added here is served after a restart (and only if the server is started
with `-m` or `--all-backends` to serve it).

### `GET /v1/admin/backends`

Every backend entry as a **view that has no `api_key` field** — `api_key_set`
says whether one is configured — plus `roles`, each named by the backend that
*resolves* for it and which rung answered (`default`, `role_pointer`, …), and
`restart_required`.

### `POST /v1/admin/backends`

The twin of `apogee config add-backend`. Body: `name` and `type` (required),
then any of `model`, `model_path`, `api_key`, `embedding_model`,
`system_prompt`, `context_size`, `max_tokens`, `temperature`, and `force`
(the `--force` twin: replace an existing entry). `201` with the view; `409`
(`type: conflict`) on a name collision without `force`; `400` on a bad type or
body.

A **literal** `api_key` is accepted from loopback peers only (`403` otherwise),
judged from the connection's own peer address and never from a forwarded
header. The `${ENV_VAR}` reference the CLI recommends is not a secret and is
accepted from anywhere. The key is never returned, by any route.

### `GET /v1/admin/backends/{id}`

One entry's view. `404` when unknown.

### `DELETE /v1/admin/backends/{id}`

The twin of `apogee config delete-backend`. `200 {deleted, restart_required}`;
`404` when unknown.

### `POST /v1/admin/backends/default`

The twin of `apogee config set-default`: `{"name": "<backend>"}` sets
`models.default`. `400` when the backend is not configured, naming the ones that
are.

### `POST /v1/admin/backends/default-embedding`

The twin of `apogee config set-default-embedding`. Same body and rules.

### `POST /v1/admin/backends/default-extraction`

The twin of `apogee config set-default-extraction`. Same body and rules.

### `POST /v1/admin/config/format`

The twin of `apogee config format`: trailing spaces stripped, blank-line runs
folded, one final newline. Content untouched; `200 {formatted, restart_required}`.

Every one of these writes goes through the same comment-preserving edit the
CLI uses, so a file edited here is **byte-identical** to one edited from the
terminal.

### `GET /v1/admin/events`

One server-wide stream of lifecycle events — distinct from a chat request's
`apogee_events` meta-frames — as named Server-Sent Events, with a `: heartbeat`
comment every 25 seconds:

```
event: session.created
data: {"type":"session.created","time":"2026-09-13T10:04:11Z","data":{"session_id":"…","model":"…"}}
```

| Event | Fires when | `data` |
|---|---|---|
| `session.created` | a server-side session is minted | `session_id`, `model` |
| `session.evicted` | a session leaves memory | `session_id`, `model`, `turns`, `reason` (`ttl` or `deleted`) |
| `model.load.started` / `model.load.completed` | a local model loads | `backend`, `model` |
| `agent.run.started` / `agent.run.completed` | a chat turn begins / ends | `model`, `tools`, `stream`, `session_id` / `model`, `ok` |
| `admin.job.started` / `progress` / `completed` / `failed` / `cancelled` | an async admin job moves | `job_id`, `kind`, plus the job's own fields |

Delivery is advisory: a client that falls 256 events behind drops the next one
rather than stalling the server.

### `GET /v1/admin/jobs`

Every async job this process has run, newest first:
`{id, kind, status, message?, result?, error?, started, finished?}` with
`status` one of `running`, `succeeded`, `failed`, `cancelled`.

### `GET /v1/admin/jobs/{id}`

One job. `404` when unknown.

### `DELETE /v1/admin/jobs/{id}`

Cancels a running job and returns its record. A cancelled job stays cancelled:
a worker that dies afterwards cannot turn it into `failed`. Idempotent on a
finished job; `404` when unknown.

No route starts a job yet: the substrate ships with the plane, and the first
job kinds — a server-local ingest, a model pull — arrive with the routes that
own them.

## What this server does not do

- **The inference plane does not authenticate.** It stays open for
  OpenAI-client compatibility; the bind policy is the guard, and a deployment
  puts its own access control in front. Only the admin plane takes a bearer.
- **It does not terminate TLS.** Plain HTTP, behind a reverse proxy that does.
- **It does not serve subscription backends.** A vendor-CLI entry is refused by
  type, with a 400 that says why.
- **It does not run tools the client defines.** The loop is the server's; a
  request with `tools` is refused rather than silently ignored.
