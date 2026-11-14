# Connections and activity per credential

**What / why.** The user's calls, 2026-11-14, from the admin-mode and people spike: the server is administered on its own Mac, in a separate "Apogee Server" window that drives the local CLI over pipes, and "its activity and an audit trail are visible to the owner" ([SPEC.md](../../../assistant/cli/SPEC.md):34). Once people share a server, "the owner sees members' activity and metadata, never their chat contents" (`:40`). The GUI SPEC puts "activity and the audit trail" in the server window ([GUI SPEC.md](../../../assistant/gui/SPEC.md):21). This item builds the activity half: what the running server is doing, and for whom, right now and since it started. The audit half, the record kept after the server stops, is [62f](audit-log.md)'s.

Evidence re-measured 2026-11-14: sources at HEAD (`4acf4a2b`), and a probe on the branch binary (`apogee 0.2.0 (b37b0a9f)`) in a scratch `APOGEE_HOME` with the `mock` backend under `serve --bind 127.0.0.1 --port 0 -v`, killed after. An admin-token subscriber held `GET /v1/admin/events` open while the probe sent a session-minting chat carrying `Authorization: Bearer apd_fake`, then a chat asking for `"model":"nope"`.

**A3 — no view of who is connected or what each client is doing.**
1. **The server tracks no connection.** The listener's only hook on a request is the `-v` logger, which prints method, path and status to stderr and keeps nothing (`presentation/httpserver/serve.cpp:179-184`). The peer's address is copied into each request (`serve.cpp:58`), and only two loopback checks ever read it (`admin_auth_routes.cpp:72`, `admin_config.cpp:326`). Probed with the admin token: `GET /v1/admin/connections`, `/activity`, `/status` and `/clients` each answered `404`.
2. **Sessions name no caller.** Probed: `GET /v1/sessions` answered `{"last_active":"2026-11-14T20:03:26Z","model":"mock","session_id":"20261114-200326-1598","turn_count":1}`. That is the whole row ([http-api.md](../../../reference/cli/http-api.md):282). The bearer on the minting request was never read: the chat answered `200`. A saved session names no caller either: its one `origin` field is an ACP host's name (56c), left empty for a session `serve` mints (`data/logger/session.h:227-232`), and it has no owner field (`:137-253`).
3. **Events name no caller.** The probe's stream carried `session.created` with `{model, session_id}`, `agent.run.started` with `{model, session_id, stream, tools}` and `agent.run.completed` with `{model, ok}`. None carried a caller, an address or a token count. The catalog says the same ([http-api.md](../../../reference/cli/http-api.md):1577-1583), and so do the emitters (`presentation/httpserver/session.cpp:52-54`; `handler.cpp:534-556`).
4. **Nothing is counted.** Each served turn writes one log line naming its backend, session, tools and latency (`handler.cpp:582-586`): the probe's log was `chat mock session 20261114-200326-1598 1ms`. The refused chat (`400`, `no backend named 'nope' (serving: mock)`) left no log line, only the `-v` stderr line `POST /v1/chat/completions -> 400`, which is never kept. The handler holds each turn's token usage (`handler.cpp:558`), and when the backend does not report it, it already computes an estimate for the token-count meta-frame, marked `"estimated"` (`:615-620`). The mock reports none: the response had no `usage`. Nothing adds any of this up per caller.
5. **The queued siblings each know a piece.**
   - [59a](../v0.2.3/paired-devices.md) keeps `last_seen` per device, in memory, written at most once a minute (`:88`).
   - [59f](../v0.2.3/paired-apps-with-scopes.md) resolves a bearer to a credential on every plane (`:48`), records the credential that minted a served session (`:54`), and gives an app's view as name, kind, device, scope, paired, last seen (`:30`).
   - [59b](../v0.2.3/machine-mode-over-the-network.md) lists the devices attached to each network session (`:28`).
   - [59c](../v0.2.3/many-sessions-at-once.md) tells a waiting turn its place in line (`:19`).

   None of them answers "who is connected now, from where, doing what, and how much have they used".

This item delivers **an activity table in the running server, keyed by credential**:
- One row per credential: a paired device, a paired app, the admin token, and anonymous callers (one row per peer address). Once [63a](../v0.2.7/people-on-one-server.md) ships, each row also names its person.
- Each row holds:
  - the addresses it called from;
  - whether it is connected, since when, and when it was last seen;
  - its in-flight requests (route, backend, session, state, started);
  - its live sessions (id, backend, turns, last active, state);
  - its counts since the server started: requests by outcome, and per backend (the model used) requests, tokens and when it was last used;
  - the names of the tools it ran, counted.
- `GET /v1/admin/activity` serves the table as one document.
- `apogee serve activity [NAME] [--output-format text|json]` reads the same document from the running server over loopback, through [62a](serve-status-and-runtime-record.md)'s runtime record and the admin token.
- `GET /v1/admin/events` gains a `caller` on every event a request causes, and `agent.run.completed` gains the turn's `usage`.
- The owner sees metadata, never contents. Nothing in the table, the document, the verb's output or an event can hold what was said.

**Core constraint(s).**
- **Remote mode is an add-on (the anti-goal).** Nothing here may make standalone use need a server, a network or an account. The table is composed only by `serve`. `chat`, `complete`, `execute`, `task`, `acp`, `mcp serve` and the Mac app over pipes build none and behave byte for byte as today. `serve activity` is a read of a running server. With none running it says so (62a's words) and exits 1, and nothing else changes.
- **The owner sees metadata, never contents** (the user's call, 2026-11-14), held by construction rather than by a screen that hides it. The row types have no field that can hold:
  - a message, a prompt, an answer or a retrieved passage;
  - a session's title, which a model writes from the conversation (`data/logger/session.h:146-147`), or the name its person typed for it;
  - a tool's arguments, or a question's target or text.

  What the table holds is names (credentials, backends, routes, tools), ids, times, addresses and counts. The honest limit is said in the reference and in `serve activity --help`: whoever controls the server Mac can still read its disk, and served transcripts are saved there as `chat` saves them ([http-api.md](../../../reference/cli/http-api.md):269-272).
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). A row is keyed by the credential's name and kind, resolved from the bearer by 59a's and 59f's gate before the table sees the request. The table never holds a token, a hash or a pairing code. An address is the socket's own peer (`HttpRequest::remote_address`), never a header a client can set. No route, query string or log line gains a credential name.
- **Stock clients keep their behavior; older versions keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The route, the verb, the `caller` and `usage` fields on events and the document are additive. Every existing event field, `GET /v1/sessions` and every inference-plane response stay byte-identical. The table is fed directly by the mux and the handlers, never from the bus, whose delivery is advisory and may drop (`infrastructure/events/bus.h:26-31`).
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). `serve activity` and `GET /v1/admin/activity` are twins: one builder makes the document, and the verb prints what the route returns. Both are classified: the HTTP parity table (`tests/presentation/httpserver/parity_test.cpp`, today one `serve` row carved out, `:172`), the shell's table (`presentation/cli/tui_parity.cpp:95`), and 59d's network door, which refuses `serve` ([reads-and-commands-over-the-network.md](../v0.2.3/reads-and-commands-over-the-network.md):41) until [62g](administering-local-and-marked-devices.md) decides who may administer from off the Mac.
- **Never listens; serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). The table lives in the socket-free mux and handlers and is driven through `Mux::dispatch` with no port. `httpserver/serve.cpp` stays the only object that may reference `listen`/`accept`, and the table takes no listener hook. `serve activity` makes one outbound call to loopback through 62a's client, and listens on nothing.
- **One config mutation path.** This item writes no config and no registry. The table is memory for the server's life.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). The table is the server's own state about its requests, so it sits in `httpserver` (Presentation), beside the session store and the jobs registry. The verb sits in `cli`. No new module and no new link.
- **Bounded.** Memory does not grow with traffic: anonymous rows are capped, in-flight entries end with their request, and counts are fixed-size per backend.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Probes and tests use a temporary `APOGEE_HOME` under the scratchpad and the mock backend. A live pass runs `apogee-dev serve` over `~/.apogee-dev`, never the release `apogee` or `~/.apogee`.

**Seam + files.**
- `source/presentation/httpserver/caller.h/.cpp` (new): `CallerView {kind, name, address}`, with `kind` one of `device`, `app`, `admin` or `anonymous`, built from 59a's and 59f's resolved credential or the admin gate's pass. It has no field for a token. [62f](audit-log.md)'s actor reuses it for network callers: whichever of the two builds first lays it, and the other reuses it.
- `source/presentation/httpserver/activity.h/.cpp` (new): `ActivityTable`, thread-safe behind one mutex, with an injected clock.
  - `begin(caller, route, method)` returns an `InFlight` handle. It ends on destruction, so a streamed response that moves the handle into its body ends it when the stream closes.
  - `InFlight` takes `backend(name)`, `session(id)`, `state(running | queued{place} | waiting{kind, tool})`, `turn(backend, usage, estimated)`, `tool(name)` and `outcome(status, error type)`.
  - `note_session(caller, id, backend)` and `end_session(id)`.
  - `snapshot(now)` returns `ActivityDocument`, made of views only.
- `source/presentation/httpserver/mux.h/.cpp`: `Mux::dispatch` resolves the caller once the request gate and the planes' gates have run, and opens an `InFlight` for every routed request. The response status becomes its outcome. A request refused by a gate is counted as `refused` against the caller it resolved to: `anonymous` by address for a missing or unknown bearer and for the `403` request gate (57n). The activity route itself and 62a's status read are left out of the counts and in-flight (below).
- `source/presentation/httpserver/handler.h/.cpp`: a turn reports its backend, its session, the tools it ran, and its usage to the request's `InFlight`. Usage is the backend's own where reported, else the estimate the handler already computes (`:615-620`), now computed in `run_turn` for streamed and whole answers alike. 59c's `queued` status and 59b's waiting question set the state. `agent.run.started` and `agent.run.completed` gain `caller`, and `completed` gains `usage`.
- `source/presentation/httpserver/session.h/.cpp`: `session.created` and `session.evicted` gain the minter's `caller`, from 59f's recorded credential. A TTL eviction carries the minter, since no request causes it. The store tells the table when a session is minted and when it leaves memory.
- 59b's `machine_sessions`: an attached link holds an `InFlight` (state `attached`, or `waiting` while a question waits) for as long as it is open. A network session it starts is noted against its credential.
- `source/presentation/httpserver/jobs.cpp`: `admin.job.*` gain the `caller` that started the job. 59d's door jobs are counted against their device.
- `source/presentation/httpserver/admin_activity.h/.cpp` (new): `GET /v1/admin/activity`, the document, behind the admin gate.
- `source/presentation/httpserver/mux.cpp`: the route row, held to the reference by `cli.http_api_conformance`.
- `source/presentation/httpserver/serve.h/.cpp`: `ServeOptions` carries no new field for the table. `serve_cmd` owns the table and hands it to the mux and the handler, so `serve.cpp` is unchanged.
- `source/presentation/cli/serve_cmd.cpp`: `apogee serve activity [NAME] [--output-format text|json]`, with help and completion ([ADR 0007](../../../adrs/cli/tab-completion.md)) completing names from 59a's registry. It finds the server through 62a's runtime record and reads `GET /v1/admin/activity` over loopback with the admin token. `--output-format json` prints the route's body unchanged. Text prints one line per row, connected rows first, then a line per in-flight request beneath its row. `NAME` narrows to one row, and an unknown name exits 1 naming `apogee devices list`.
- `tests/presentation/httpserver/parity_test.cpp`, `source/presentation/cli/tui_parity.cpp`, and 59d's `tests/presentation/cli/remote_parity_test.cpp`: `serve activity` classified as the twin of `GET /v1/admin/activity`, a shell view, and refused on the network door with `serve`.
- Docs:
  - [http-api.md](../../../reference/cli/http-api.md): a section, "Activity", covering the route, the document, what "connected" means, what is never shown and why, the disk caveat, and that behind the operator's proxy every remote row shows the proxy's address. The events table gains `caller` and `usage`.
  - The commands reference: `serve activity`.
  - DEVELOPER.md: the file rows.
- Tests (hermetic: a temporary `APOGEE_HOME`, the mock backend and a fake provider that answers a planted string, handlers driven through `Mux::dispatch` with no port wherever a port is not the point):
  - `tests/presentation/httpserver/activity_test.cpp`: begin and end; a stream's handle ending with its body; the connected window and `connected_since` across an idle gap, on an injected clock; reported and estimated tokens kept apart; the anonymous cap folding the oldest idle row; a revoked credential's row kept and marked; the reads of the table not counted.
  - `tests/presentation/httpserver/admin_activity_test.cpp`:
    - rows for a device, an app, the admin token and two anonymous addresses, each with its own sessions and counts;
    - a `400` and a `401` counted as refused;
    - `caller` on every request-caused event, with the existing fields unchanged;
    - `usage` on `agent.run.completed`;
    - `401` without the admin token.
  - `tests/presentation/httpserver/activity_contents_test.cpp`: a planted prompt, answer, custom name, tool argument and question target, each distinctive. Each is searched for in the document, every bus event, every log line (with `-v`) and the text and JSON of `serve activity`, and must appear in none.
  - `tests/presentation/cli/serve_activity_cmd_test.cpp`: text and JSON over a stubbed loopback reply, `NAME`, an unknown name, and no server running.
  - `leak_test.cpp` extended: the planted device, app and admin tokens are absent from the document and every event.
  - A shell case, `cli.serve_activity` (`tests/scripts/sh/serve_activity.sh`): the spike's probe as the regression fixture (Guardrails).

**Reference.**
- In-house precedents:
  - The session store and the jobs registry: per-process server state in `httpserver`, read by admin routes.
  - The bus's advisory delivery (`bus.h:26-31`). This is why the table is fed directly and the events only mirror it.
  - The token-count meta-frame's `"estimated"` mark (`handler.cpp:615-620`), which the counts keep.
  - 57n's request gate, which runs ahead of every route in the socket-free mux. The caller is resolved at the same place.
  - 59a's `last_seen` and views with no hash, and 59f's credential on every plane and the minter recorded on a session.
  - 59c's `queued` place in line, which already "never names another session, its device, its id or its content" ([many-sessions-at-once.md](../v0.2.3/many-sessions-at-once.md):27). The table holds itself to the same discipline toward contents.
- Siblings:
  - [62a](serve-status-and-runtime-record.md): the runtime record and the loopback read; this item is gated on it.
  - [62b](serve-reload-and-served-vs-disk.md): reload. A reload keeps the table and its counts.
  - [62f](audit-log.md): the audit log, which shares `CallerView`.
  - [62g](administering-local-and-marked-devices.md): who may reach the admin plane, this route included.
  - [62h](local-admin-reads.md): the other reads a local admin screen needs.
  - [59a](../v0.2.3/paired-devices.md) and [59f](../v0.2.3/paired-apps-with-scopes.md): the credentials this item is keyed by; gated on both.
  - [59b](../v0.2.3/machine-mode-over-the-network.md), [59c](../v0.2.3/many-sessions-at-once.md), [59d](../v0.2.3/reads-and-commands-over-the-network.md) and [59e](../v0.2.3/tasks-outlive-the-connection.md): the doors whose requests are counted.
  - [63a](../v0.2.7/people-on-one-server.md): adds the person to each row.
  - [63c](../v0.2.7/allowances-keys-and-usage.md): usage per person that outlives a restart, and its limits.
  - darwin [10a](../../darwin/macos/v0.2.6/design-the-server-window.md) and [10b](../../darwin/macos/v0.2.6/server-window.md): the server window's activity part, which reads `serve activity --output-format json` over pipes.
- Prior art (the spike's survey, 2026-11-14): Open WebUI's admins can read users' chats, and its switch against that only hides them in the interface. The deliberate divergence here is structural: no field exists to hold a conversation, so no screen can show one. Jellyfin and Plex list each active device or member. Apogee's rows are the same idea, keyed by the credential that 59a and 59f mint.

**Decisions made** (dated):
- 2026-11-14: Specced from the admin-mode and people spike. The user's calls that shape this item:
  - Admin mode is a separate "Apogee Server" window in the macOS app, offered only on the Mac that serves, and it drives the local CLI over pipes. So the activity read is a CLI verb with `--output-format json`, not a route the app calls.
  - Administering is local by default. The owner may mark chosen paired devices as allowed to administer remotely ([62g](administering-local-and-marked-devices.md)), which revises 59d's "the admin plane keeps its one token for scripts and the operator" ([reads-and-commands-over-the-network.md](../v0.2.3/reads-and-commands-over-the-network.md):84). Otherwise the admin plane, this item's route included, stays local-only.
  - The owner sees members' metadata (devices, apps, activity, which model, when) and never chat contents. Whoever controls the server Mac can still read its disk, and the documentation says so honestly.
  - The window installs the server as a background service (launchd) and starts, stops and restarts it ([62d](serve-as-a-background-service.md)). The table lives in the serving process, so it is the same under the service as under a terminal's `serve`.
  - People are a household or a small team, and the design serves both. Members use the server's local models only, unless the owner grants a household cloud key with a limit ([63c](../v0.2.7/allowances-keys-and-usage.md)). So "which model" is a row's per-backend counts, the fact 63c's limits and usage build on.
  - The words are "people" and "access" (Owner, Member), never "roles", which already means model roles in Apogee. This item names no person and no access; it is keyed by credential until 63a.
  - The defaults the user took the same day, for the items that own them: live changes applied on the spot by a reload, with what still needs a restart said (62b); an audit kept 90 days of who, what and when, without request contents (62f); collections as shared libraries handed out per person plus private ones of a member's own, and notes and saved decisions personal by default (63b); a member's apps paired by the owner at the server Mac, capped at that member's allowance (63a, 63c); a task queue per person rather than a refusal (63e). The model behind them: the owner (the OS account that owns `APOGEE_HOME` on the server Mac) and members, each with an allowance in 59f's scope shape, presets (Full, Family, Limited) rather than groups, and a guest as a member on Limited with nothing kept. None changes this table, which keeps nothing past the server's run for anyone.
  - The SPEC amendments are approved and already written ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4, "Revised again 2026-11-14"; [GUI SPEC.md](../../../assistant/gui/SPEC.md) → Core surfaces 3, the server window).
  - The standing law: remote use is an add-on, and the CLI alone and the Mac app over pipes stay complete and need no server.
- 2026-11-14: Placement (the user's call): both halves before v0.3.0, the server console in v0.2.6 (CLI track 62, darwin/macos track 10's 10a and 10b) and people and access in v0.2.7 (CLI track 63, darwin/macos 10c). This item is CLI 62e, v0.2.6, gated on 59a (device credentials), 59f (app credentials and a session's minter) and 62a (finding and reaching the running server). Darwin 10b reads it. 63a adds the person in v0.2.7. Nothing already queued is renumbered.
- 2026-11-14: Three items, one caller, three lifetimes. This item is the live view, for the server's run. [62f](audit-log.md) is the kept record of who did what, 90 days, without request contents (the user's default). [63c](../v0.2.7/allowances-keys-and-usage.md) is usage per person that outlives a restart and carries limits. This item persists nothing.
- 2026-11-14: The verb is `serve activity` (the plan's name), not the spike's `serve connections`, because a row outlives its connection: a disconnected device keeps its counts until the server stops.
- 2026-11-14: "Never contents" is held by the types, not by the window. A title is left out with the messages, because a model writes it from the conversation.

**Open calls:**
- [default: a row is keyed by credential: `device:<name>`, `app:<name>`, `admin` (one row, since the token is one per install; its addresses list where it was used from), and `anonymous` with one row per peer address. Once 63a ships, each row gains `person` and the text output groups rows by person. The key stays the credential.] (consumed decision: 63a) What a person is, and which credentials are theirs.
- [default: HTTP has no "connected" of its own, so a row is **connected** while it holds an open request or stream (a streamed answer, a `/v1/admin/events` subscriber, 59b's attached link) or has finished a request within the last 5 minutes. `connected_since` is when that spell began: the first request after an idle gap longer than the window. `last_seen` is when the last request ended. A disconnected row stays, with its counts, until the server stops.]
- [default: counts cover this server run only, in memory, and the document's `since` is the server's start. A reload (62b) keeps them, and a restart starts them again. 59a's `last_seen` write to the registry is unchanged and is the only thing that outlives a restart.]
- [default: tokens are counted per backend as prompt and completion. They are the backend's reported numbers where it reports them, and otherwise the handler's estimate, kept in separate `estimated` counters so a total never mixes the two without saying so. A request with no turn (a model list, a session read) counts as a request only.]
- [default: an in-flight entry holds the method, the matched route pattern (`/v1/sessions/{id}`, with a session id as its own field, never a query string), the backend, the session id, the state and when it started. The state is `running`, `queued` with 59c's place, `attached` for 59b's link, or `waiting` with a question's kind and tool name, never its target or text.]
- [default: a row's sessions are the live sessions its credential minted (59f's record) and the network sessions it is attached to (59b): id, backend, turns, last active, state. Never the title, never the person's own name for the chat.]
- [default: the tools a row ran are counted by name (`read_file: 3`), as the operational log already names them (`handler.cpp:582-586`). Their arguments are never kept.]
- [default: reading the table does not change it. `GET /v1/admin/activity` and 62a's status read are left out of the counts and the in-flight list, though the admin row's `last_seen` moves. The server window can poll without filling its own table.]
- [default: anonymous rows are capped at 256 addresses. Past that, the oldest idle anonymous row is folded into one `anonymous (others)` row, its counts kept.]
- [default: a revoked device's or app's row stays, marked `revoked`, with its counts, until the server stops. Its next request is 59a's `401`, counted against `anonymous` at its address.]
- [default: an address is the socket's peer (`HttpRequest::remote_address`), never `X-Forwarded-For`. Behind the operator's own proxy, every remote row shows the proxy's address, and the reference says so. Reading a forwarded header would let a client choose the address it is listed under.]
- [default: `caller` on events is `{kind, name, address}`. It is added to `session.created` and `session.evicted` (the minter), `agent.run.*`, `model.load.*` (the request that caused the load) and `admin.job.*`. `agent.run.completed` gains `usage: {prompt_tokens, completion_tokens, estimated}`. Existing fields are unchanged.]
- [default: no `--follow` in this item. `serve activity` is a snapshot, and the server window polls it. A live tail over pipes, built on the events with their callers, is a later item if polling proves too coarse.]
- (consumed decision: 62a) How the CLI finds the running server (the runtime record), how it reaches it (loopback and the admin token), and its words when no server runs.
- (consumed decision: 62g) Who may reach `GET /v1/admin/activity` from off the Mac: nobody by default, and the devices the owner marks.
- (consumed decision: 62f) Refusals and admin acts as audit entries, and the actor for a CLI process over pipes.
- (consumed decision: darwin 10b) How the server window shows activity, and how often it polls.

No **[user]** call blocks this item.

**Guardrail(s).**
- `activity_contents_test` plants a distinctive prompt, answer, custom name, tool argument and question target. It fails if any of them appears in the document, an event, a log line or the verb's output. This is the user's "never contents", held by a test as the secrets rule is held by `leak_test`.
- `leak_test` covers the document and the new event fields for every planted token.
- A conformance case shows every existing event field, `GET /v1/sessions` and the inference plane byte-identical for a stock client, with and without the table populated.
- `activity_test` pins the connected window, the cap and the reported and estimated split on an injected clock, so no test sleeps.
- A test drives `chat`, `complete`, `execute` and `task run` with no server running and checks they build no table and behave as before (the anti-goal).
- The spike's probe is the regression fixture. `cli.serve_activity` runs the real binary under `serve --bind 127.0.0.1 --port 0` in a scratch home with the mock backend and holds an admin-token subscriber on `/v1/admin/events`. It sends a session-minting chat with a planted prompt, then a chat asking for `"model":"nope"`. `apogee serve activity --output-format json` must then show the session against its caller, one `ok` and one `refused` request, and every event must carry `caller`. The planted prompt must appear nowhere, and the server is killed after. The probe's 404 on `/v1/admin/activity` is what it replaces.
- `cli.no_listen_symbols` and `cli.complete_opens_no_listening_socket` stay green, and `serve.cpp` is untouched.

**Acceptance criteria:**
- [ ] In a sandbox home with the mock backend, under `serve --port 0` on loopback with a device `phone` and an app `portfolio` paired (each with `--print-token`): `phone` mints a session and chats twice, `portfolio` chats once, and an anonymous request asks for `"model":"nope"`. Then `apogee serve activity --output-format json` lists `device:phone`, `app:portfolio`, `admin` and `anonymous` at `127.0.0.1`. `phone`'s row shows its address, `connected_since`, `last_seen`, its session with backend `mock` and 2 turns, 2 `ok` requests, and tokens on `mock` (estimated, since the mock reports none). The anonymous row shows 1 `refused`.
- [ ] `GET /v1/admin/activity` with the admin token returns the same document the verb printed, byte for byte. Without the token it is `401`.
- [ ] While `phone` holds a streamed chat open, its row lists one in-flight `POST /v1/chat/completions` on `mock`, state `running`, with its session. Once 59c ships, a turn waiting for a busy lane shows `queued` with its place.
- [ ] On `/v1/admin/events`, `session.created` and both `agent.run.*` events carry `caller` (`kind: device`, `name: phone`, the address), and `agent.run.completed` carries `usage`. Every field the probe saw is still there, unchanged.
- [ ] A planted prompt, answer and custom chat name appear in no activity document, event, log line or `serve activity` output.
- [ ] After `apogee apps revoke portfolio`, its row stays, marked `revoked`, with its counts. After the idle window (an injected clock in the test), `phone`'s row is no longer connected and keeps its counts.
- [ ] With no server running, `apogee serve activity` exits 1 with 62a's words. `chat` and `complete` behave as before. The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user runs `apogee-dev serve --bind 0.0.0.0 --allow-remote`, chats from a paired second machine, and sees its row, address and counts in `apogee-dev serve activity` on the server Mac.

**Scope note.** Earmarked for v0.2.6 (track 62); gated on 59a, 59f and 62a. Darwin 10b reads it, and 63a extends it. Out of scope:
- Usage that outlives a restart, per-person usage, limits and whose keys pay (63c).
- The audit log and its retention (62f).
- The person on each row (63a).
- Who may read the route from off the Mac (62g).
- The server window itself (darwin 10a, 10b).
- A live `--follow` tail.
- A per-request history beyond the counts.
- Addresses taken from forwarded headers.
- The contents of any conversation, on any surface, ever.
