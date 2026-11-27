# CLI-59f · Paired apps, each with a scope

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.3 |
| **Component** | The harness server (`serve`): the paired registry, its gate and doors, `apogee apps` |
| **Labels** | `harness-server` `pairing` `app-scopes` `secrets` |
| **Epic** | Track 59 · the harness server (apps as clients) |
| **Blocked by** | [59a](paired-devices.md) |
| **Blocks** | [59g](capability-discovery.md), [60c](../v0.2.4/app-tools-on-every-door.md), [62e](../v0.2.4/connections-and-activity.md), [62h](../v0.2.4/local-admin-reads.md), [63a](../v0.2.4/people-on-one-server.md) |
| **Reporter** | The apps-as-clients spike (2026-11-14), on the user's goal of the same day |
| **Created** | 2026-11-14 |

---

## Summary

Other apps are to use Apogee as their AI back end, but `serve`'s clients are only the person's devices, and nothing limits what a credential can do. This item makes apps a kind of paired client: an app pairs the way a device does, the person approves its scope at pairing, every door holds it to that scope, and a scope never reaches past the config. Until the person widens it, a new app gets completions on the default backend and nothing else.

## Description

### User story

**As** a person who lets other apps (Taylor'd Portfolio first) use Apogee as their AI back end,
**I want** each app paired by name, like a device, and held to a scope I approve at pairing,
**so that** an app gets models, retrieval and tasks on my server without reaching files, shell or other apps' data I never granted, and I can list and revoke it by name.

### Background

The user's goal, 2026-11-14: other apps use Apogee as their AI back end, and the first is Taylor'd Portfolio (`QuantumCompiler/Taylor-d-Portfolio`), the user's SwiftUI job hunter. Its `LLMProvider` seam today runs Foundation Models or `claude -p`, and it is to run on Apogee's full harness instead: models, retrieval over its own documents, and autonomous tasks on the server.

The CLI SPEC was amended that day (Core surfaces 4, "Revised 2026-11-14"): the harness server's clients include other apps, "each app is paired like a device and held to a **scope** the person approves", and "it is listed and revoked by name". This item builds that.

An app pairs the way a device does (59a's code and exchange), into the same registry as a new kind, `app`, with its own name and the device it runs on. At pairing the person approves its scope: which backends, collections, tool classes, command families and kinds of task it may use. Every door holds the app to it, and a scope never reaches past the config. Until the person widens it, a new app gets completions on the default backend and nothing else.

This item delivers **apps as a kind of paired client, each held to a scope**:
- `apogee apps pair` mints a pairing code (or, with `--print-token`, a token for an app on the server itself) for an app named by the person, on a device the person names, with the scope the person approves printed in plain words before the code.
- The registry row has `kind: app`, the app's name, its device, and its scope. The token is `apa_`-prefixed, returned once, and stored as a hash.
- `apogee apps list|scope|revoke` list, edit and revoke apps by name, and `devices list` shows apps too, with their kind.
- One narrowing, in Business, turns the config into the app's view of it. Every door reads that view: the OpenAI plane (gated by 59a's `--devices-only`, which now accepts app tokens), the harness plane's sessions (59b), the command door (59d), served tasks (59e), and the tool registry each of them offers.
- What an app starts (served sessions, chats, tasks, jobs) is recorded as its own, and an app sees only its own.

### Evidence

Re-measured 2026-11-14: sources at HEAD (`231fb049`), and probes on the branch binary (`apogee 0.2.0 (b37b0a9f)`) in a scratch `APOGEE_HOME` with two `mock` backends (`mock`, the default, and `other`) under `serve --all-backends --port 0` on loopback, killed after. The review re-ran W1.3's and W2.4's probes the same day in a fresh scratch home, with the same answers.

**W1 — other apps are not a kind of client.**

1. **W1.1 — The server's clients are the person's devices.** The 2026-11-07 amendment makes `serve` "the harness server for the person's own devices. Paired devices are authenticated" ([SPEC.md](../../../assistant/cli/SPEC.md):31). 59a's registry holds devices ("name, kind, paired at, last seen, token hash", [`paired-devices.md`](paired-devices.md), its seam: the device registry over `devices.json`), mints one credential shape (an `apd_` token, [`paired-devices.md`](paired-devices.md), its open calls: the default that a device token is 32 random bytes in hex with an `apd_` prefix), and puts "every route registered behind the device gate" ([`paired-devices.md`](paired-devices.md), its description of what it delivers: pairing and the harness plane).
2. **W1.2 — A device credential reaches the whole harness, by recorded design.** Through 59d's door "a paired device sends the words a person types after `apogee`, and the server runs exactly that command" ([`reads-and-commands-over-the-network.md`](reads-and-commands-over-the-network.md), its description of the command door). 59e lets any device attach to, halt, cancel or resume any task: "A per-device owner adds nothing when every device belongs to one person" ([`tasks-outlive-the-connection.md`](tasks-outlive-the-connection.md), its open calls: the default that every paired device is the person's, so any may control any task). That holds while every holder is the person. It stops holding the day a job hunter holds one.
3. **W1.3 — The inference plane knows no caller at all.** The bearer is read only on `/v1/admin` paths (`presentation/httpserver/mux.cpp:631-641`); the plane is "unauthenticated (for OpenAI-client compatibility)" ([http-api.md](../../../reference/cli/http-api.md):25). Probed: a request carrying `Authorization: Bearer apa_0000` asked for `"model":"other"` and got `200` `"mock response"`, the bearer unread. This is also the OpenAI door's lack of login, which 59a's off-by-default `--devices-only` answers for devices only ([`paired-devices.md`](paired-devices.md), its open calls: the default that `--devices-only` is in scope and off by default, putting the inference plane behind the device gate).

**W2 — nothing limits what a credential can do.**

4. **W2.1 — Every served backend is every caller's.** `resolve_served` checks a request's `model` against one server-wide list, `options_.served` (`presentation/httpserver/handler.cpp:252-295`, the check at `:285`). Probed: an anonymous `GET /v1/models` listed both `mock` and `other`; `?rerank=other` on a `mock` request answered `200`, so a query parameter reaches a second backend too.
5. **W2.2 — Every served tool is every caller's.** `serve --tools` builds one registry for the server's life (`presentation/cli/serve_cmd.cpp:358-375`), and `tool_mode` takes only `all` or `none` (`handler.cpp:677-681`). The reference says it outright: "any client of the port can ask for any of them" ([http-api.md](../../../reference/cli/http-api.md):27). The probe's start line was `tools: 25 (files under /Users/taylor/Data/Development/Projects/Apogee, shell, git, notes, rag, graph, fetch_url to listed hosts, todo_write)`: the files root was simply the folder `serve` was started in.
6. **W2.3 — Retrieval is the server's, not the caller's.** `--rag NAME` is "fixed for the server's lifetime" ([http-api.md](../../../reference/cli/http-api.md):28), and a request can only override the retriever and the reranker (`:147-150`).
7. **W2.4 — One caller reads another's conversation.** Probed: a session minted with `"session_id":"new"` under the bearer `apa_appA` (its message `"my resume text"`) was listed by an anonymous `GET /v1/sessions`, and `GET /v1/sessions/<id>` under the bearer `apa_appB` returned `200` with both messages. The reference lists "every live session" to any caller (http-api.md:280-282).
8. **W2.5 — The admin plane is one key for everything.** With the install's admin token, `GET /v1/admin/backends`, `/v1/admin/tasks` and `/v1/admin/permissions` each answered `200`. There is no narrower key to hand an app.
9. **W2.6 — The narrowing pieces exist, keyed to agents and tasks, never to a caller.** `apply_tool_policy` filters the registry so "a tool that is not registered is not advertised, whatever the model asks" (`presentation/cli/helpers.h:198-203`). A task's grants are held to the config and the agent by `grant_refusal` (`business/tasks/policy.h:93-100`, its scope struct `:77-91`). Nothing applies either per credential.

## Acceptance criteria

- [ ] **Given** a sandbox home with backends `mock` (the default) and `other`, **when** I run `apogee apps pair --name portfolio --on "this machine" --print-token --json`, **then** it prints the scope in plain words (backend `mock` only) and an `apa_` token, once; and `apogee apps list` and `apogee devices list` show `portfolio` with kind `app`, its device and its scope, and no token or hash.
- [ ] **Given** `serve --port 0 --tls on --devices-only` on loopback and that token, **when** the app calls the inference plane, **then** `GET /v1/models` lists `mock` only; a chat on `mock` answers `200`; a chat on `other`, and `?rerank=other`, answer `403 scope_refused` naming the `apps scope` line; a request with tools enabled is offered none; and an anonymous request is `401`.
- [ ] **Given** that server, **when** I run `apogee apps scope portfolio --allow backend:other --allow tools:read`, **then** the next request on `other` answers `200` and offers the read tools only; **and when** I run `--deny tools:read`, **then** they are taken away at the next request.
- [ ] **Given** a session minted by `portfolio`, **when** a second app's token names it or lists sessions, **then** it is `404` to that token and absent from that app's `GET /v1/sessions`.
- [ ] **Given** the harness plane (where 59b, 59d and 59e have shipped), **when** an app sends a command outside its families, lists models through the door, starts a task outside its kinds, or looks for another app's task, **then** the command is `403 scope_refused` and its runner is never called; `models list` lists the app's backends only; the task is refused; and another app's task is invisible to it.
- [ ] **Given** a paired app, **when** I run `apogee apps revoke portfolio`, **then** the next request is `401`, on a gated plane and on an open one; **and when** I run `devices revoke` of a device, **then** it takes its apps and names them.
- [ ] **Given** no registry present, **when** I run `chat`, `complete`, `execute` and `task run`, **then** they behave byte for byte as before; **and given** apps paired, **when** a stock OpenAI client with no Apogee token calls, **then** it gets the same responses as without. The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user runs `apogee-dev serve --bind 0.0.0.0 --allow-remote --devices-only`, pairs a test app from a second machine, and sees a backend outside its scope refused.

## Constraints

- **Remote mode is an add-on (the anti-goal).** Nothing here may make standalone use need a server, a network, a pairing or an account. `chat`, `complete`, `execute`, `task`, `acp`, `mcp serve` and the Mac app over pipes never read the registry or a scope, and behave byte for byte as today. A local app that spawns `apogee` over stdio runs as the person, with the person's rights, exactly as the Mac app does. A scope is the server's concept, because only the server can hold an app to one. A session or command child reads a scope only when `serve` hands it one (Technical approach), and with no scope handed it reads nothing new.
- **A scope never widens past the config.** The app's view is the config narrowed, never anything the config lacks. A backend, collection, agent or recipe the config does not hold is not reached by naming it in a scope. A tool class the scope grants still meets the config's `permissions:`, where `deny` wins. Approval refuses a vendor-CLI backend, which `serve` never serves (http-api.md:1622-1625).
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). An app token gets 59a's treatment: returned once, stored as its SHA-256, header-only, compared in constant time, refused from a query string, `0600` through `write_file_atomically(…, true)`. The app's view (name, kind, device, scope, paired, last seen) structurally lacks the hash. A scope names things, never a key. A narrowed config is built from views, so no scope file handed to a child carries a key.
- **Stock clients keep their behavior** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). With no app paired, or a request with no Apogee credential, the inference plane's routes, bodies and errors are byte-identical. The new rows, fields, flags, routes, the `scope_refused` error type and the response header are additive.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). `apps list|scope|revoke` answer from the CLI (`--json`) and on the admin plane, and are classified for the shell and for 59d's network door. `apps pair` is CLI-only, as `devices pair` is, because a pairing code is never minted over HTTP (59a). One narrowing serves every door, so no door keeps its own idea of a scope.
- **Never listens; serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). `apps pair` writes a file and opens no socket. The checks run in the socket-free mux and handlers, and `httpserver/serve.cpp` stays the one object that may reference `listen`/`accept`.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). The scope's data shape sits with the registry in Data (`secrets`). The narrowing sits in a new Business module, `scope`, which links `harness`, `agent` and `tasks`, and nothing above it. The gate and routes stay in `httpserver`, the verbs in `cli`. Each new link is a deliberate edit to `cmake/modules.cmake`.
- **UTF-8 where it enters** (CLI CLAUDE.md). An app's offered name, its device's name and a requested scope are mended and bounded before they are stored or printed.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Probes and tests use a temporary `APOGEE_HOME` under the scratchpad and the mock backend. A live pass runs `apogee-dev serve` over `~/.apogee-dev`, never the release `apogee` or `~/.apogee`.

## Technical approach

| File | Change |
|---|---|
| `source/data/secrets/devices.h/.cpp` (59a's registry) | A row gains `kind` (`device` or `app`), and an app row gains `device` (the name of the device it runs on) and `scope`. `AppScope` is plain data: `backends`, `collections`, `tools` (permission classes), `commands` (word prefixes), `tasks` (kinds), with its JSON form, and `requested` beside it (what the app asked for, never applied). A pending pairing carries its kind and its approved scope, so the exchange mints the right row. `AppView` is the app's view with no hash. Revoking a device revokes the apps recorded on it, in the same locked write. |
| `source/business/scope/scope.h/.cpp` (new module) | The one narrowing (its functions below). |
| `source/presentation/httpserver/device_auth.h/.cpp` (59a's gate) | Resolves a bearer to a credential (`device` or `app` + its scope) for every plane. On the harness plane, an app token is admitted as a device token is. Under `--devices-only`, both kinds are admitted to the inference plane. On an open inference plane, a bearer with an Apogee prefix (`apd_`, `apa_`) is still resolved, and an unknown or revoked one is `401`; any other bearer stays anonymous, as today. |
| `source/presentation/httpserver/handler.h/.cpp` | A request's credential travels with it (the checks below). |
| 59b's `machine_sessions` (the harness plane's sessions) | `net_connect` checks `model` and `suite` against the scope; the child gets `--scope-file` (below). |
| 59d's `command_door` | `command_refusal` runs before the runner, answering `403 scope_refused`. Each command child gets `--scope-file`, so `models list` lists the app's backends and a verb naming a backend or collection outside the scope is refused by the child's own resolver. `GET /v1/harness/commands` answers the app's classification. |
| 59e's served task | `task_refusal` runs before `run_task_events`. The ledger's policy records `started_by` (additive, read as "the person" when absent), the task runs with recall off and the narrowed view, and an app attaches to, halts, cancels, resumes and lists only its own. |
| `source/presentation/cli/apps_cmd.h/.cpp` | The `apps` verbs (below), each with help and completion ([ADR 0007](../../../adrs/cli/tab-completion.md)), completing app names, backends, collections, classes, families, agents and recipes from live state. |
| `source/presentation/cli/devices_cmd.cpp` | `devices list` gains the kind column and shows apps, and `devices revoke` names the apps it takes with a device. |
| `source/presentation/httpserver/admin_devices.h/.cpp` | `GET /v1/admin/apps` (views), `PATCH /v1/admin/apps/{name}` (the same allow and deny as `apps scope`, validated by the same function), `DELETE /v1/admin/apps/{name}`. They are shaped as 59a's device routes. |
| `source/presentation/cli/serve_cmd.cpp` | The start note counts paired apps. On an open inference plane with apps paired, it says their scopes bind only requests that present their tokens, and names `--devices-only`. |
| `source/presentation/cli/tui_parity.cpp` and 59d's `remote_parity.cpp` | `apps` classified. For an app credential, `apps`, `devices`, `auth`, `config`, `reset`, `uninstall` and `serve` are refused whatever its scope. |
| `source/presentation/cli/check.cpp` | 59a's registry row covers app rows. No new file. |
| `cmake/modules.cmake` | The `scope` module in Business. `httpserver` and `cli` link it. |

**The narrowing, `source/business/scope/scope.h/.cpp`.**
- `default_scope(config)`: the backend `models.default` names at pairing time, written out, and nothing else.
- `scope_refusal_on_approve(scope, config)`: a name the config lacks (a collection is the exception: see Open questions), a vendor-CLI backend, an unknown class or family, or a family that is never an app's.
- `narrow_config(config, scope)`: the config the app sees. It keeps only the scope's backends (the default re-pointed to the first of them when the config's own default is outside it), only the scope's collections and the graphs over them, no `auto_rag` outside them, and only the agents and recipes the scope's task kinds name.
- `narrow_tools(registry, scope)`: a registry filter shaped like `apply_tool_policy`, dropping every tool whose class is not in the scope.
- `tool_class(tool)`: computed from `writes`, `edits` and `outbound` until 60a ships, and then 60a's `permission` read directly.
- `command_refusal(words, scope)`: matched by word prefix, as 59d's `remote_refusal` is.
- `task_refusal(request, scope)`: the kind, the agent, the recipe and the grants, the grants then held to the config by the existing `grant_refusal`.

**The handler's checks, `source/presentation/httpserver/handler.h/.cpp`.**
- `resolve_served` refuses a backend outside an app's scope, and so does `?rerank=BACKEND`.
- `GET /v1/models` lists the app's backends only.
- `tool_mode: all` offers `narrow_tools` of the served registry, and grants in the scope run without asking unless the config denies them.
- A server-wide `--rag` or `auto_rag` collection outside the scope is not retrieved for that request, and the response says so (`Apogee-Scope-Withheld: rag:<collection>`, and an `apogee_events` notice).
- A served session records the credential that minted it, and another credential naming it gets `404 session_not_found`, as an unknown id does. `GET /v1/sessions` lists an app's own sessions to it, and anonymous callers see only anonymous sessions.

**The harness plane's sessions (59b's `machine_sessions`).** `net_connect` checks `model` and `suite` against the scope. The child is started with a hidden root flag, `--scope-file PATH`: a `0600` file `serve` writes before the child starts and removes when it exits, as ACP hands MCP `env` to its child (CLI CLAUDE.md → Secrets, 56b). Under it the child's config is `narrow_config`, its tools `narrow_tools`, and recall is off. A session records its credential, and an app attaches to, resumes and lists only its own. An app never answers a permission card, because no card can arise (Open questions).

**The verbs, `source/presentation/cli/apps_cmd.h/.cpp`.**
- `apogee apps pair --name NAME [--on DEVICE] [--allow KIND:NAME]… [--print-token] [--address HOST]… [--port N] [--no-qr] [--json]`;
- `apps list [--json]`;
- `apps scope NAME [--allow KIND:NAME]… [--deny KIND:NAME]… [--grant-requested] [--json]`;
- `apps revoke NAME`.

**Reference.**
- In-house precedents:
  - 59a's pairing, registry, gate and `--devices-only`, which this item grows by a kind.
  - The agent policy's registry filter (Milestone X, `apply_tool_policy`): narrowing is structural, so a tool that is not registered cannot be called.
  - The task's grants and their ceiling (27i, `grant_refusal`): a grant runs without asking and is never wider than the config. An app's scope is a standing grant of the same shape.
  - `resolve_served`'s "is not served" refusal: a server-wide narrowing this item makes per credential.
  - ACP's private file for a child (56b): how a scope reaches a session child without argv or the environment.
  - 59d's word-prefix classification, which a scope's command families reuse.
- Siblings:
  - [59a](paired-devices.md), the gate; this item is gated on it.
  - [59b](machine-mode-over-the-network.md), [59d](reads-and-commands-over-the-network.md) and [59e](tasks-outlive-the-connection.md): the doors that hold an app to its scope.
  - [59c](many-sessions-at-once.md): an app's sessions queue with everyone's.
  - [59g](capability-discovery.md): tells an app what its scope lets it use; gated on this item and 59d.
  - [59h](structured-output-on-every-door.md): structured output, which an app asks for within its scope.
  - [60a](../v0.2.4/one-tool-definition.md): the permission class a scope's tool field names.
  - [60b](../v0.2.4/device-tools.md): device tools, which an app credential never declares.
  - [60c](../v0.2.4/app-tools-on-every-door.md): an app's own tools, gated on this item.
  - [60d](../v0.2.4/building-an-app-on-apogee.md): the integrators' guide, which documents scopes for app builders.
  - GUI [7a](../../darwin/v0.2.4/app-connection-local-or-server.md): the connection seam.
  - GUI [7d](../../darwin/v0.2.4/apogeekit-swift-package.md): ApogeeKit, which carries an app's pairing and token.
- External prior art: OAuth scopes and per-app API keys with allowed models (OpenAI's project keys, for example). Neither was checked for this document and neither is relied on. The deliberate divergence is that a scope here is approved on the server's own terminal, never through a consent page served over the network.

**Docs when it ships:**
- [http-api.md](../../../reference/cli/http-api.md): a chapter, "Apps and their scopes": pairing an app, the token, what each door checks, `scope_refused`, the withheld header, sessions that belong to their minter, and `--devices-only` with app tokens.
- [machine-mode.md](../../../reference/cli/machine-mode.md): a line that a served session's view may be an app's.
- The commands reference: `apps`.
- CLI CLAUDE.md's secrets sub-rules: "an app token is a device token's twin: hashed, returned once".
- DEVELOPER.md: the file rows.

## Testing

All hermetic: a temporary `APOGEE_HOME`, the mock backend, handlers driven through `Mux::dispatch` with no port wherever a port is not the point.

- **`tests/data/secrets/devices_test.cpp`** — an app row round trip with its scope; hash only; `0600`; a device revoke cascading to its apps; an older 59a file with no `kind` read as devices.
- **`tests/business/scope/scope_test.cpp`** — `narrow_config`'s table (backends, the default re-pointed, collections, graphs, `auto_rag`, agents, recipes); `narrow_tools` over every native tool, its class matching `gated()`; `command_refusal`'s prefixes, and the never-an-app's families refused for any scope; `task_refusal`, with grants past the config refused by `grant_refusal`'s words; approval refusing a name the config lacks and a vendor-CLI backend; the default scope. It pins the narrowing table, the default scope, and the never-an-app's families. Every door calls the one narrowing, and a test plants a door path that skips it and fails.
- **`tests/presentation/httpserver/app_scope_test.cpp`** — the matrix that pins every refusal, the withheld header and the ownership filter through `Mux::dispatch`, with no port: an app token on `/v1/models` lists its backends only; `model: other` and `?rerank=other` refused `403 scope_refused`; `tool_mode: all` offers its classes only, and a granted `write` tool runs where the config says `ask` and is refused where it says `deny`; a server-wide collection outside the scope withheld, with the header; app A's session is `404` to app B, and `GET /v1/sessions` filtered per credential; under `--devices-only`, an app token admitted and an anonymous call `401`; on an open plane, a revoked `apa_` token is `401` and a `sk-` dummy bearer is anonymous and unchanged.
- **The harness-plane tests of 59b, 59d and 59e** (where they exist when this item builds) — an app's `net_connect` outside its backends refused; a command outside its families refused with the runner never called; a task kind outside its scope refused; another app's session, job and task invisible.
- **`tests/presentation/cli/apps_cmd_test.cpp`** — `pair` output (the plain-words scope, then the code), `--json`, `--print-token`, `list`, `scope` (allow, deny, `--grant-requested`), `revoke`, and an unknown name.
- **`leak_test.cpp`** — a planted app token searched in every listing, `check` row, admin response, scope file, log line (with `-v`) and bus event; it covers the planted app token and the scope files.
- **A conformance case** — the inference plane byte-identical for a stock client (no bearer, and a dummy `sk-` bearer) with and without apps paired.
- **The anti-goal** — a test drives `chat`, `complete`, `execute` and `task run` with the registry absent, then unreadable, and checks they are unchanged. A child started without `--scope-file` reads no registry.
- **A shell case, `cli.apps_scope` (`tests/scripts/sh/apps_scope.sh`)** — pins the whole flow on the real binary under `serve --port 0 --tls on --devices-only` on loopback, with two mock backends. An app is paired with `--print-token`, curl asks for each backend and lists models, `apps scope` widens it and the next request sees the change, and `apps revoke` turns the next request into `401`.
- **`cli.http_api_conformance`** — covers the new routes; `cli.no_listen_symbols` is unchanged; `cli.install_parity` stays green, since nothing is seeded.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What are the app verbs, and does `devices list` show apps? | `[default]` | The verbs are `apogee apps pair\|list\|scope\|revoke`, over 59a's one registry. `devices list` shows apps too, with their kind, so nothing paired is hidden from either list. "Devices" is hardware in the person's words, and an app's scope has no device equivalent, so the app verbs stand apart while the file stays one. |
| Where and how does the person approve an app's scope, and what becomes of the scope an app asks for? | `[default]` | The person approves the scope on the server's own terminal. `apps pair` prints, before the code, what the app may and may not do in plain words, for example: "Taylor'd Portfolio on Taylor's Mac may use backend 'mock' for completions. It may not use tools, read collections, run commands or start tasks." The scope comes from `--allow` flags, or the default scope when none are given. The app's exchange may carry a `requested_scope`, which is recorded beside the scope, shown by `apps list` and `apps scope NAME` as "asks for: …", and never applied until the person runs `apps scope NAME --grant-requested` or allows each piece. |
| What flag shape sets each part of a scope? | `[default]` | One flag shape for every part of a scope: `--allow KIND:NAME` and `--deny KIND:NAME`, repeatable. The kinds are `backend:`, `collection:`, `tools:` (a class: `read`, `edit`, `write`, `outbound`, `listed`), `command:` (a word prefix, such as `command:embed ingest` or `command:models list`) and `task:` (`task:goal`, `task:recipe:NAME` or `task:agent:NAME`). `apps scope NAME` with no flags prints the scope, and `--json` gives the view. |
| Is the default backend stored by name, or as "whatever the default is"? | `[default]` | The default backend is written out as its name at pairing, never stored as "whatever the default is". Changing `models.default` later never widens what an app reaches. |
| May a scope name something the config does not hold? | `[default]` | A scope may name a collection that does not exist yet, so an app can be granted its own collection before its first `embed ingest`. It reaches that name only. Every other kind must name something the config holds at approval. A name the config later drops is shown as missing by `apps list` and reaches nothing. |
| What does a tool class in the scope mean, and does an app ever answer a permission card? | `[default]` | A tool class in the scope is a **standing grant**, like a task's `--allow`: its tools run without asking unless the config's `permissions:` say `deny`, which wins. A class outside the scope is not offered at all. So an app never answers a permission card, and no card reaches the person's devices on an app's behalf. The approval line names what each class means on this server: "read files under <folder>", "write files and run shell commands under <folder>", "reach the hosts in tools.allowed_hosts". The folder is the one `serve` gives served tools (`tools.fs_root`, or 59b's `--workspace`). A folder of the app's own is a later call. |
| What do `command:` and `task:` grant, and which families are never an app's? | `[default]` | `command:` and `task:` grant nothing beyond the narrowed view. A granted `embed ingest` ingests only into the scope's collections, and a granted `task:goal` runs on the scope's backends with the scope's classes as its ceiling. The families that decide who may do what (`apps`, `devices`, `auth`, `config`), and `reset`, `uninstall` and `serve`, are never an app's, whatever its scope. An app that could edit the config could widen its own ceiling. |
| What may an app see of what others started? | `[default]` | An app sees only what it started. Served sessions, harness-plane sessions, command jobs and tasks record their credential, and an app's listings, attaches and controls reach its own only. Its sessions and tasks run with recall off, so the person's earlier chats never enter its context. Its saved chats are the person's to see, as any chat is. Reading the person's other records (`chats`, `knowledge`, `task list`) is a `command:` grant, said as such at approval. |
| Is something outside an app's scope withheld or refused? | `[default]` | A server-wide retrieval collection outside an app's scope is withheld from that app's request, not refused. The app did not ask for it, and refusing would lock a default-scope app out of every request on a `--rag` server. The withholding is said, in the `Apogee-Scope-Withheld` header and in `apogee_events`. Anything the app asks for outside its scope is refused, never silently dropped: `403`, type `scope_refused`, the message naming the thing, the app, and the `apps scope` line that would grant it. The message is the same whether or not the thing exists, so a refusal tells an app nothing about what else the server holds. |
| What is an app token, and how does an open inference plane treat a bearer? | `[default]` | An app token is `apa_` plus 32 random bytes in hex, beside 59a's `apd_`. An open inference plane resolves a bearer with either prefix: a valid one is held to its scope, so an app gets the same answers on an open server as on a gated one, and an unknown or revoked one is `401`, so a revoked app is never quietly served as anonymous. Every other bearer (a stock client's dummy key) stays anonymous, byte-identical to today. Only `--devices-only` makes a scope binding on everyone, and the start note says so. |
| What does `--on DEVICE` name, and how are app names kept unique? | `[default]` | `--on DEVICE` names a paired device, or `this machine` for an app on the server itself (which `--print-token` serves, printed once on the server's terminal). The device is a label and a cascade, not a check on where the token is presented: revoking the device revokes its apps and says which. App names are unique across the registry, ignoring case, with 59a's " (2)" de-duplication when no name is given. |
| When does a scope change apply, and what happens to an app's live work when it narrows? | `[default]` | A scope change applies at the next request, read fresh by mtime as 59a's revocation is. A **narrowed** scope also closes the app's live harness sessions at their next event (59b's revocation path, `net_refused` with "scope changed"), and halts its running task through `request_task_stop`. Resuming is then held to the new scope. A widened scope applies to what the app starts next. |
| Are the person's devices scoped in this item? | `[default]` | The person's devices are not scoped in this item. Their rows have no scope and keep the whole harness. A scoped device is a later call, and the row shape allows it. |
| How is a tool's class derived until 60a ships? | `[default]` | Until 60a ships, `tool_class` derives the class from `writes`, `edits` and `outbound` exactly as 60a's derivation table does (`read`, `edit`, `write`, `outbound`; `listed` for a served call). A table test holds it to `gated()`, and 60a replaces its body with the definition's `permission`. |
| The exchange, TLS, the pin, the registry's lock and the gate | `(consumed decision)` | 59a. An app pairs over the same `POST /v1/pair`. |
| Each door's transport, frames, limits and refusals | `(consumed decision)` | 59b, 59d, 59e. This item adds only the scope check and the ownership filter to each. |
| How an app learns its scope and what it holds (models, collections, tools, recipes) | `(consumed decision)` | 59g |
| An app's own tools on either door, and how they sit beside the scope's classes | `(consumed decision)` | 60c |
| Where an app keeps its token and pin, and how ApogeeKit pairs | `(consumed decision)` | GUI 7d |

No **[user]** call blocks this item.

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Specced from the apps-as-clients spike. The user's calls that shape this item follow in the next rows. | — |
| 2026-11-14 | Taylor'd Portfolio uses the full harness (models, retrieval over its documents, autonomous tasks on the server), not just model calls. So a scope covers every door, not only the OpenAI one. | the user |
| 2026-11-14 | Apps reach Apogee through both doors: the OpenAI-compatible endpoints for plain model calls, and Apogee's own protocol (stdio locally, the paired connection remotely) for the harness. | the user |
| 2026-11-14 | Each paired app is held to a scope the person approves, and never reaches files, shell or other apps' data unless granted. | the user |
| 2026-11-14 | The CLI SPEC amendment is approved and already written ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4, "Revised 2026-11-14"). | the user |
| 2026-11-14 | On the OpenAI door, an app's own tools work the standard way (Apogee returns `tool_calls`, the app runs them, the turn continues), within its scope. This reverses serve's recorded rule "It does not run tools the client defines" ([http-api.md](../../../reference/cli/http-api.md):1626-1627), and the reference changes with 60c, which builds it. This item gives it the scope. | the user |
| 2026-11-14 | Apogee ships a Swift package other apps embed (GUI 7d), factored from the Mac app's client core, and the Mac app depends on it too: one implementation, tested by Apogee. It carries this item's pairing and token. | the user |
| 2026-11-14 | Taylor'd Portfolio's own side is not specced now. | the user |
| 2026-11-14 | The standing law: remote use is an add-on, and the CLI alone and the Mac app over pipes stay complete with no server. | the user |
| 2026-11-14 | Placement, accepted with "execute the spike": CLI 59f, v0.2.3, track 59, gated on 59a. 59g and 60c gate on it. It collapses two walls into one: an app as a kind of client (W1) and its scope (W2). The OpenAI door's lack of login is answered here too, by 59a's `--devices-only` accepting app tokens. | the user |
| 2026-11-14 | The default scope for a new app is completions on the default backend only (the spike's plan). On either door that means a model call or a session with history on that one backend, with no tools, no retrieval, no recall, no commands and no tasks. | the spike |
| 2026-11-14 | This item revises two recorded defaults, for apps only. 59d's door, where the server runs exactly the command a paired device sends (its index row: "a paired device runs any command the person may run"), becomes "a device does, and an app runs the families its scope names". 59e's "any of them may attach to, halt, cancel or resume any task" stays true of the person's devices, and an app reaches only the tasks it started. The person's devices keep the whole harness. | the spec |
| 2026-11-14 | Every door that exists when this item is built takes the scope check in this item. A door that ships after it (59b, 59d or 59e, if any is still pending) takes the check as part of its own build, against this item's narrowing. | the spec |

## Out of scope

- Taylor'd Portfolio's own side (its `LLMProvider` engine on Apogee), not specced now.
- What an app may use, discovered at run time (59g).
- Structured output on any door (59h).
- An app's own tools on either door (60c).
- The Swift package an app embeds (GUI 7d), and the integrators' guide and compatibility promise (60d).
- Scoping the person's own devices.
- A folder of an app's own for its file tools.
- Holding a local app that spawns `apogee` over stdio to a scope. It runs as the person, as the Mac app does.
- Consent pages, OAuth flows and pairing without the server's terminal.
- Rate limits, quotas, metering and billing.

## Links

- **Blocked by:** [59a](paired-devices.md) · **Blocks:** [59g](capability-discovery.md), [60c](../v0.2.4/app-tools-on-every-door.md), [62e](../v0.2.4/connections-and-activity.md), [62h](../v0.2.4/local-admin-reads.md), [63a](../v0.2.4/people-on-one-server.md)
- **Relates to:** [59b](machine-mode-over-the-network.md), [59c](many-sessions-at-once.md), [59d](reads-and-commands-over-the-network.md), [59e](tasks-outlive-the-connection.md), [59h](structured-output-on-every-door.md), [60a](../v0.2.4/one-tool-definition.md), [60b](../v0.2.4/device-tools.md), [60d](../v0.2.4/building-an-app-on-apogee.md), GUI [7a](../../darwin/v0.2.4/app-connection-local-or-server.md), GUI [7d](../../darwin/v0.2.4/apogeekit-swift-package.md); [SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4; [ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0006](../../../adrs/cli/backwards-compatibility.md), [ADR 0007](../../../adrs/cli/tab-completion.md), [ADR 0008](../../../adrs/cli/module-map.md), [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md); Milestone X (`apply_tool_policy`), 27i (`grant_refusal`), 56b (ACP's private file for a child)
- **Revises (for apps only):** 59d's "a paired device runs any command the person may run" and 59e's "any of them may attach to, halt, cancel or resume any task" (Decision log, 2026-11-14)
