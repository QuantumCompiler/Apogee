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
| 501 | `backend_unavailable` | An admin route that needs a generation backend on a server that serves none (`/v1/admin/knowledge/capture`) |
| 502 | `backend_error` | The provider failed; the knowledge clerk returned no record |
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
| `rag_result` | `done` | `collection`, `chunks_found`, `top_score`, `retriever`, `reranked`, `graph_entities` (when the collection's knowledge graph expanded the chunks), `detail` (notes) | What was injected. `retriever` sets `top_score`'s scale — lexical and vector scores are not comparable. |
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

### `GET /v1/admin/mcp-servers`

Every `mcp_servers:` entry as a view: `{name, command, args, enabled,
env_set}`. `env_set` says whether the entry carries an `env:` list; the values
are never returned — a user may well have put a token in one.

### `POST /v1/admin/mcp-servers`

The twin of `apogee mcp create`. Body: `name` (required), and either
`command` with optional `args` (register an existing executable, nothing
written) or nothing else (scaffold a runnable Python server under the data
directory's `mcp/<name>/`, exactly as the CLI would); `force` replaces an
existing entry. `201` with the view plus `directory` (empty for register-only)
and `restart_required: true` — servers connect at startup. `409` (`type:
conflict`) on a name collision without `force`; `400` on a name that is not a
server name or a malformed body.

### `GET /v1/admin/mcp-servers/{id}`

One entry's view. `404` when unknown.

### `DELETE /v1/admin/mcp-servers/{id}`

The twin of `apogee config delete-mcp-server`: the entry is removed, its
files are left alone. `200 {deleted, restart_required}`; `404` when unknown.

### `PUT /v1/admin/mcp-servers/{id}`

The twin of `apogee mcp enable` and `mcp disable`: `{"enabled": true|false}`
edits exactly one line of the entry. `200` with the view and
`restart_required: true`; `400` on a body without a boolean `enabled`; `404`
when unknown.

Every one of these writes goes through the same comment-preserving edit the
CLI uses, so a server registered here leaves the file byte-identical to one
registered from the terminal.

### `GET /v1/admin/agents`

Every agent `apogee analyze --agent` can run, as a view: `{name, description,
model, prompts, schemas, output_format, tools, mcp, questions, collection,
save_dir, save_filename, save_subdir, bundled, overrides_bundled}`. The
bundled three appear with `bundled: true` unless a config entry of the same
name overrides them, in which case the entry is listed with
`overrides_bundled: true`. `tools` is the agent's permission model:
`read-only` (never prompts), `all` (gated like `chat`), or `none`.

### `POST /v1/admin/agents`

The twin of `apogee agents create`. Body: `name` (required), and optionally
`description`, `model`, `tools`, `no_schema`, `prompt_body`, `schema_body`,
`output_format`, `collection`, `questions`, `save_dir`, `save_filename`,
`save_subdir`, `mcp` (a list of server names), and `force`. The prompt is
written to `prompts/<name>.txt` and the schema to
`schemas/<name>-output.json` under the data directory -- starter texts when
no body is given -- and the entry is appended through the CLI's own edit,
so the result is byte-identical to the CLI's. `201` with the view plus
`prompt_path` and `schema_path`; `409` (`type: conflict`) when the agent or
its files exist without `force`; `400` on an invalid name, policy, format, or
body. No restart is needed: `analyze` reads the config on every run.

### `GET /v1/admin/agents/{id}`

One agent's view plus `prompt_bodies` and `schema_bodies`, each a list of
`{path, body, present}` -- the file's text, or the compiled-in text for a
bundled agent whose file has not been seeded (`present: false`). `404` when
unknown.

### `PUT /v1/admin/agents/{id}`

The twin of `apogee agents edit`: the same body as `POST`, with the name
from the path and `force` implied, so `prompt_body` and `schema_body`
replace the files in place. `200` with the view; `400` on a malformed body.

### `DELETE /v1/admin/agents/{id}`

The twin of `apogee agents delete`: the entry is removed; with `?purge=true`
its prompt and schema files are removed too. `200 {deleted, files_removed}`;
`404` when there is no entry -- including for a bundled agent that was never
overridden, which has no entry to delete.

### `POST /v1/admin/knowledge/capture`

The twin of `apogee knowledge capture`: the normalization clerk over a raw
conversation, one canonical record out, stored. Body: `raw` (required -- the
conversation), and optionally `model` (the backend that runs the clerk; a
served backend, resolved exactly as a chat request's `model` is, so a
vendor-CLI or unserved backend is a `400`), `db` (the collection; default
the config's `knowledge.db`, then `knowledge`), and the overrides that win
over the clerk for their field: `status` (`shipped` | `rejected` |
`superseded`, synonyms accepted), `discipline`, `source`, `link`,
`supersedes` (the id of the record this one replaces, which is marked
superseded), `retriever` (`lexical` | `vector` | `auto`, resolved against
the collection's pin through the same resolver `embed ingest` uses, under the
same spend rule), and `draft` (below).

`201` with:

```json
{"record": {"id": "kr-20260913T194429Z-3fa2c1", "intent": "…", "decision": "…",
            "status": "shipped", "discipline": "eng", "downstream_link": "",
            "provenance": {"source": "chat"}, "raw_ref": "…/knowledge/raw/kr-….md",
            "timestamp": "2026-09-13T19:44:29.512034Z"},
 "db": "knowledge", "retriever": "lexical", "registered": true}
```

`registered` says the collection was added under `embeddings:` by this call
(the first capture into a new name); `note` carries the resolver's fallback
reason when there is one, `notes` anything non-fatal (a `supersedes` target
that does not exist). The raw conversation is archived on the server under
`knowledge/raw/` and never returned. `400` on a missing `raw`, a bad status,
a bad retriever, or a resolver refusal (an explicit `vector` with no embedding
backend); `501` when the server serves no generation backend; `502` when the
clerk failed to produce a conforming record after its one retry.

**`"draft": true`** runs the clerk, applies the overrides, normalises and
validates -- and stores nothing: the HTTP twin of `capture --dry-run`, and
the first step of the capture → review → store flow below. `200` with
`{draft: true, record, db, retriever[, warning][, note]}`: the record has an
empty `id` and `timestamp` and no `raw_ref` (only a store mints those), `db`
and `retriever` are the store decision a real capture would make, and
`warning` says what a real capture would fail on (an explicit `vector` with
no embedding backend) rather than failing the preview. Nothing touches the
store, the archive or the config.

#### Capture → review → store

A GUI usually wants a human look between the clerk and the store. The plane
supports that as a **stateless** three-step flow: the server holds no draft
between calls -- the draft lives with the client -- and nothing touches the
store, the archive or the config until the last step.

1. **Draft** -- `POST /v1/admin/knowledge/capture` with `"draft": true`.
2. **Refine**, zero or more times -- `POST /v1/admin/knowledge/refine` with
   the draft, one instruction and the raw conversation (below). A new draft
   comes back; nothing is stored.
3. **Store** -- `POST /v1/admin/knowledge` with the reviewed draft's fields as
   the body, plus `raw` so the conversation is archived. The ordinary
   finished-record route, with no separate "store this draft" path -- which
   is what makes a draft → refine → store round trip land a record
   field-equivalent to a one-shot capture of the same conversation.

The refine loop is HTTP-only by design: a CLI user re-runs `capture --dry-run`
with different flags or input.

### `POST /v1/admin/knowledge/refine`

One bounded revision pass over a client-held draft. Body: `record` (the draft
as the client holds it, required), `instruction` (what to change, in the
reviewer's words -- required, non-empty, at most 2000 characters: the source
material belongs in `raw`, not here), `raw` (the conversation the draft was
captured from -- optional but recommended, since it is the clerk's only
grounding; without it the clerk is told to revise from the draft and the
instruction alone and never invent), and `model` (the backend that runs the
clerk, resolved as capture's is). The clerk applies the instruction, changes
nothing else, and stays grounded in `raw`; the same schema as capture, so the
result is storable through the finished-record route as-is. `supersedes` is
not a clerk field and is carried through untouched; a `provenance.source` the
clerk drops keeps the draft's value rather than a default.

`200` with `{draft: true, record}` -- a new draft, never stored. `400` for a
missing record or an empty or over-long instruction, checked **before** any
clerk call; `501` when the server serves no generation backend; `502` when
the revision is not a conforming record after the clerk's one retry.

### `GET /v1/admin/knowledge`

The records, newest first, or a query over them. Query parameters: `db` (the
collection; default the config's `knowledge.db`, then `knowledge`), `status`
and `discipline` (filters; **no default branch over HTTP** -- the CLI's
`query` defaults to shipped, a listing route shows what a client asks for),
`anonymize=true` (attribution and the local `raw_ref` stripped, the
provenance chain kept -- the shareable shape), and, for a query, `q` (the
question), `retriever` (`lexical` | `vector` | `hybrid` | `auto`), `rerank`
(a backend, or `off`), and `limit` (default 20).

A missing collection is `200` with an empty list -- never an error, and never
a created file. Without `q`: `{"object": "list", "data": [record…]}`. With
`q`, through the one retriever resolver every surface shares, the filters
applied **before** the cut so an off-branch top hit never starves the
result, and the judge under its never-fail contract:

```json
{"object": "list", "data": [{"record": {"id": "kr-…", "intent": "…", …}, "score": 0.61}],
 "retriever": "lexical", "reranked": false, "note": "…"}
```

`retriever` sets the scale of every `score` (normalised BM25, cosine, or RRF
-- not comparable across retrievers); `reranked` is set from the same place
as the ordering. `400` on a bad retriever, an unknown `rerank` backend, or an
explicit ask the resolver refuses; `501` for `retriever=vector` on a server
with no embedding backend, naming `?retriever=lexical` as the way out; `502`
when the embedder failed.

**`?graph=true`** -- the twin of `knowledge query --graph`: the knowledge
graph covering the collection is walked from the matched records (each record
is a chunk, and its `decision` node plus the entities extracted from its text
are that chunk's mentions), exactly what a `--rag` turn over the collection
would inject. Opt-in; the envelope gains
`"graph": {"context": "[Knowledge graph: knowledge]\n…", "entities": N}`, with
`context` empty when nothing related was found and a `note` **only** when no
graph covers the collection (build one with `POST /v1/admin/graph/{id}/build`),
so "no graph" and "a graph, but nothing related" stay distinguishable.
Composes with `?anonymize=true`; the section never carries a name.

### `GET /v1/admin/knowledge/{id}`

One record (`?db=` selects the collection). `404` when the collection or the
record does not exist.

### `PATCH /v1/admin/knowledge/{id}`

The twin of `apogee knowledge link` and `knowledge status`: a body with
**exactly one** of `link` (the downstream artifact) or `status` (`shipped` |
`rejected` | `superseded`, synonyms accepted), and optionally `db`. A
metadata edit in place -- the search index is built only from the record's
immutable reasoning, so a link or status change never re-embeds and never
makes a stored vector stale. `200` with the record; `400` for neither or both
fields, or a bad status; `404` when unknown.

### `DELETE /v1/admin/knowledge/{id}`

The twin of `apogee knowledge delete`: the record and its archived
conversation (`?db=` selects the collection). `200 {"deleted": "<id>"}`;
`404` when unknown.

### `POST /v1/admin/knowledge/reindex`

The twin of `apogee knowledge reindex`: re-embeds records and rewrites their
stored vectors, after an embedding-model change. Body (optional): `{db?,
id?}` -- one record, or every record when `id` is absent. **Vector-only by
design**: the text index is maintained on every write, so a collection
captured lexically needs nothing -- that case is `200 {"reindexed": 0,
"note": …}` rather than an error, as is an empty collection. A mixed
collection is reindexed whole and says so in `note`, because doing so embeds
records that were deliberately captured lexically. `404` for a collection or
record that does not exist; `501` for a collection that holds vectors on a
server with no embedding backend to rebuild them; `502` when the embedder
failed.

### `POST /v1/admin/graph/{id}/build`

The twin of `apogee graph build {id}`: the extraction clerk over every stale
chunk, into the `kg_*` tables. `{id}` is resolved **graphs-first**, exactly
as the CLI resolves it: a `graphs:` entry names a named multi-collection
graph (see `/v1/admin/graphs`), built over its member collections into the
graph's own database under `embeddings/graphs/`; anything else is a
collection, built into that collection's own database. One generation call
per chunk, so it is an **async job**: `202 {"job_id": "job_…"}` at once,
progress as `admin.job.*` events on `GET /v1/admin/events` (a named build's
progress names the member being extracted), and the finished counts at
`GET /v1/admin/jobs/{id}` (`files_planned`, `files_extracted`,
`chunks_extracted`, `chunks_failed`, `nodes_upserted`, `edges_upserted`,
`mentions_added`, `entities_embedded`, `record_nodes`, `supersedes_edges`,
`supersedes_skipped`, `limit_hit`, an `embed_error` when entity vectors
stopped early; for a collection `collection` and `enabled: true` the first
time the build set `graph.enabled` on its `embeddings:` entry -- through the
same config editor the CLI uses, byte-identical; a `config_warning` when it
could not; for a named graph `graph` and `collections`, and nothing enabled:
the entry is the enablement). `DELETE /v1/admin/jobs/{id}` cancels between
chunks; what finished stays, and the next build resumes.

Body, every field optional: `model` (the extraction backend), `force`
(re-extract every source), `limit` (stop after N chunks). The backend resolves
as the CLI resolves it -- `model` > the entry's `extract_backend` (a
collection's `graph:` block, or a named graph's own) > the extraction role
> the default -- and must be a served backend. **A full build never runs on
a metered backend on Apogee's initiative**: a fall-through to a metered
default is a `400` naming the three ways to say so. `400` for a vendor-CLI or
unserved backend (as a chat request's `model` would get), `404` when the
collection has no data or no member of a named graph has any, `501` when the
server serves no generation backend. Entity vectors follow the embedding
spend rule: a collection's through its own embedder when it is unmetered or
the collection pins `retriever: vector`; a named graph's through the default
chain -- its vectors are its own, whatever its members' chunk embedders --
otherwise the graph is full-text searchable and complete.

Builds are incremental and resumable: a source is re-extracted only when it
is stale -- no state row under its collection, its chunk count changed, its
highest chunk id moved (a same-count re-ingest is still caught), or the
extraction model changed. A named graph **rebuilds, never absorbs**: a
member's own graph is neither consulted nor migrated, and a member removed
from the entry has its rows reconciled away on the next build. `--dry-run`
is CLI-only. Every knowledge record in a collection -- or in any member -- is
materialised as a `decision` node on every build, deterministically.

### `GET /v1/admin/graph/{id}/stats`

`200` with `{nodes, edges, mentions, nodes_by_type, nodes_with_vectors,
total_chunks, chunks_with_mentions, stale_files, failed_chunks, communities[,
extract_model]}`. For a named graph also `graph`, `collections`, the graph's
own `embed_model` when entities were embedded, and `members`: one
`{collection, mentions, total_chunks, chunks_with_mentions, stale_files[,
missing: true]}` per member, coverage read from the member's own store, a
missing member reporting zero chunks. An unbuilt graph, or a collection with
no data yet, reports zeros -- never an error, so a client polling an empty
layer sees a shape.

### `GET /v1/admin/graph/{id}/entity`

`?name=` (required) resolves an entity by normalised name -- exact first, one
per type sharing the name -- then, when nothing matches exactly, the top
full-text hit with `"fuzzy": true` and `also_matched` naming the runners-up.
`200 {"data": [{name, type, description?, mentions, dim, status?, discipline?,
relations: [{relation, direction: "out" | "in", peer, peer_type, weight,
description?}], chunks: [{collection?, source, chunk, text}]}], "fuzzy":
bool}`; a `decision` node carries its record's `status` and `discipline`; in
a named graph each supporting chunk names the member `collection` it lives
in, resolved through that member's store. `400` without a name; `404` when
the graph is unbuilt, the collection has no data, or nothing matches.

### `POST /v1/admin/graph/{id}/communities`

The twin of `apogee graph communities {id}`: the graph's **global layer**.
The nodes are partitioned into thematic clusters by deterministic weighted
label propagation over the extracted relations -- no model in the detection
-- and each cluster of at least `min_size` (default 3) entities is summarised
with one generation call under a compiled-in prompt, the summary stored as
an ordinary retrievable chunk under a `graph://community/<id>` source, so
"what are the main themes?" is answered by plain retrieval on any
retriever. A community's identity is its exact member set: an unchanged
community costs nothing, a changed one is pruned and regenerated, `force`
regenerates all. One call per new or changed cluster, so it is an **async
job** (`graph-communities`): `202 {"job_id"}`, progress `summarising
communities {done, total}`, and at `GET /v1/admin/jobs/{id}` the counts
`{detected, summarized, unchanged, failed, pruned, embedded[, embed_error]}`
-- a summariser failure is soft (the community is skipped and an existing
summary kept). Summary vectors follow the same spend rule as the build's
entity vectors, and a summary still without one is vectorised on the next
run with an embedder.

Body, every field optional: `model`, `force`, `min_size`. The summariser
resolves exactly as the build's extractor does, with the same refusals:
`400` for a metered default reached by fall-through, a vendor CLI, or an
unserved backend; `404` for an unbuilt graph or a collection with no data;
`501` when the server serves no generation backend. A named graph's
summaries live in its own database and are listable here; they do not
surface through its members' retrieval.

### `GET /v1/admin/graph/{id}/communities`

The stored communities, largest first: `200 {"object": "list", "data": [{id,
size, summary, model?, top_members}]}` with the three most-mentioned member
names. An unbuilt graph lists as empty, never an error.

### `POST /v1/admin/graph/{id}/dedupe`

The twin of `apogee graph dedupe {id}`: merges same-type entities whose
entity vectors exceed `threshold` cosine similarity (default `0.92`) -- the
"K8s" versus "Kubernetes" problem the exact name key cannot catch. The
earliest-extracted node survives: edges are repointed to it (weights summed
when they collide, would-be self-loops dropped), mentions unioned and
recounted, descriptions merged first-non-empty (the survivor's vector
cleared on a text change), and the merged nodes' community memberships
removed (the next communities run recomputes). Entities without a vector are
never considered, and **`decision` nodes are never merged**. Synchronous --
storage and cosine, no generation -- and **never automatic**: `200
{"groups": [{kept, kept_type, merged: [names]}], "merged_nodes", "threshold",
"dry_run"}`; with `"dry_run": true` the groups are computed and nothing is
written. `400` for a threshold outside `(0, 1]`; `404` for an unbuilt graph
or a collection with no data.

### `DELETE /v1/admin/graph/{id}`

For a collection: clears every graph row -- nodes, edges, mentions, state,
communities and their summary chunks -- and returns `{"deleted": {nodes,
edges, mentions}}`; the real chunks, their vectors and the config entry are
untouched, and the next build starts from scratch. For a named graph:
removes the graph's own database file (everything in it is derived) and
returns the same shape; the member collections and the `graphs:` entry are
untouched -- `DELETE /v1/admin/graphs/{id}` removes the entry. `404` when
there is nothing built.

### `PUT /v1/admin/embeddings/{id}/graph`

The twin of the build's auto-enable write. Body `{"enabled": true | false}`;
sets `embeddings.{id}.graph.enabled` through the one config editor -- the
value replaced in place or a `graph:` block appended to the entry, every
other byte kept -- and answers `{"collection", "enabled"}`. What gates
retrieval-time expansion through a collection's own graph on every surface.
(A built named graph covering the collection takes precedence regardless;
the block is left as it is and resumes the moment the collection leaves.)
`404` when the entry does not exist under `embeddings:`; `400` for a body
without the boolean.

### `GET /v1/admin/graphs`

The `graphs:` entries -- named graphs spanning several collections, the
twins of `apogee config add-graph` / `delete-graph`. `200 {"object": "list",
"data": [{name, collections, extract_backend?, hops, max_entities, built}]}`,
`built` reporting whether the graph's database exists. Config only: the data
routes are `/v1/admin/graph/{id}/*` above. A collection covered by a built
entry expands through it at retrieval, cross-collection; the first entry
listing a collection wins a double-listing, and an unbuilt entry covers
nothing.

### `POST /v1/admin/graphs`

Adds an entry through the same comment-preserving transform the CLI uses,
byte-identical. Body: `name` and `collections` (a non-empty list of
collection names) required; `extract_backend`, `hops` (1 or 2),
`max_entities` optional. The CLI's rules, answered as `400`: a plain name, at
least one member, **no collision with a collection name** (resolution is
graphs-first, so a collision would make the collection's own graph
unreachable), a configured `extract_backend` when one is named, knobs in
range. A member that is not configured yet is a `warnings` entry, never a
refusal -- ingest registers collections on first use. `201 {"data": {…},
"warnings"?: […]}`; `409` when the name exists -- `PUT` replaces.

### `GET /v1/admin/graphs/{id}`

One entry, `200 {"data": {…}}` in the shape above; `404` when not
configured.

### `PUT /v1/admin/graphs/{id}`

Replaces the entry in place, under the same rules as `POST`; a body `name`,
when present, must match the path (`400` otherwise). `200 {"data": {…}}`;
`404` when not configured. The graph's database is untouched -- the next
build converges on the new membership.

### `DELETE /v1/admin/graphs/{id}`

Removes the entry and answers `{"deleted": name}`. The graph's database is
left on disk exactly as `apogee config delete-graph` leaves it --
`DELETE /v1/admin/graph/{id}` while the entry still exists removes the data.
`404` when not configured.

### `POST /v1/admin/knowledge`

Stores a finished record without running the clerk -- the store step of a
review flow, where a client has a draft it has looked at. Body: the record's
fields at the top level (`intent` required; `status` must be canonical;
`decision`, `discipline`, `downstream_link`, `provenance{source,
attribution}`, `supersedes` as the client has them), plus optionally `raw`
(archived beside it), `db` and `retriever` as above. An `id` and `timestamp`
are assigned when absent and kept when present. `201` with the same envelope
as capture; `400` when the record does not validate. It needs no generation
backend: only the embedder, when the retriever resolves to `vector`.

### `GET /v1/admin/permissions`

What the permission gate does for each destructive native tool — `ask`,
`allow`, or `deny` — as `{"object":"list","data":[{tool, level}]}`: every tool
that declares itself destructive (`write_file`, `delete_file`, `run_command`,
`write_note`, `delete_note`) at its effective level, `ask` when the config does
not list it, plus any other key the config carries (a namespaced MCP tool, once
the MCP client lands).

### `PUT /v1/admin/permissions/{id}`

The twin of `apogee config set-permission <tool> <level>`. `{id}` is a tool
name; body `{"level": "ask" | "allow" | "deny"}`. `200 {tool, level,
restart_required}`; `400` on an unknown level or a name that is not a tool
name. A served run reads its levels once at startup, so `restart_required` is
`true` when the level now differs from what this server started with. Read-only
tools have no level: they never prompt, and `permissions.read_file` is not a
key.

On a served request nobody can answer a prompt, so `ask` means deny there:
`allow` in the config is the only way a destructive tool runs under `serve`.

### `GET /v1/admin/auth`

The stored provider credentials as **metadata** — `{"object":"list","data":[{provider, stored_at}]}` — plus
`backends`: for every configured backend that takes a key, its `name`, `type`,
and which `source` currently answers for it (`config`, `store`, `env` with the
`variable`, or `none`). No key is ever in this response; the type it serializes
has no field for one. A store file that cannot be read is reported as an empty
list with a `warning`, never an error.

### `PUT /v1/admin/auth/{id}`

The twin of `apogee auth add <provider>`. `{id}` is a provider **type** with
API billing — `anthropic`, `openai`, or `google` — and the key travels in the
body only: `{"key": "sk-…"}`. `200` with the slot's metadata. Served to
**loopback peers only**: `403` (`type: forbidden`) from any other address,
whatever the bind, judged from the connection's own peer and never from a
forwarded header. A vendor-CLI type (`claude-cli`, `codex-cli`, `gemini-cli`,
`ollama-cli`) is `400` with the principle — those CLIs authenticate themselves,
and Apogee never stores, reads or proxies their credentials.

The stored key is used by every backend of that type that has no `api_key` of
its own in `config.yaml`; a configured `api_key` (literal or `${ENV}`) still
wins, and the ambient `ANTHROPIC_API_KEY` / `OPENAI_API_KEY` /
`GEMINI_API_KEY` (then `GOOGLE_API_KEY`) is the last resort. Backends read
their key when they are built, so a stored key reaches a running server after a
restart.

### `DELETE /v1/admin/auth/{id}`

The twin of `apogee auth clear <provider>`. `200 {"cleared": "<provider>"}`;
`404` when nothing was stored for it. It accepts no secret and mints none, so
the bearer alone gates it.

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

The job kinds are `graph-build` (`POST /v1/admin/graph/{id}/build`) and
`graph-communities` (`POST /v1/admin/graph/{id}/communities`); a server-local
ingest and a model pull arrive with the routes that own them -- and with the
ingest route, `"graph": true` on its body to chain the covering graph's build
after a successful ingest, the twin of `apogee embed ingest --graph`.

## What this server does not do

- **The inference plane does not authenticate.** It stays open for
  OpenAI-client compatibility; the bind policy is the guard, and a deployment
  puts its own access control in front. Only the admin plane takes a bearer.
- **It does not terminate TLS.** Plain HTTP, behind a reverse proxy that does.
- **It does not serve subscription backends.** A vendor-CLI entry is refused by
  type, with a 400 that says why.
- **It does not run tools the client defines.** The loop is the server's; a
  request with `tools` is refused rather than silently ignored.
