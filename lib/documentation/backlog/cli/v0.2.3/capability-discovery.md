# What may I use: capability discovery

**What / why.** The user's goal, 2026-11-14: other apps use Apogee as their AI back end, Taylor'd Portfolio (the user's SwiftUI job hunter) first, and they use the **full harness**: models, retrieval over the app's own documents, and autonomous tasks on the server. [59f](paired-apps-with-scopes.md) pairs each app and holds it to a scope the person approves. An app then needs to know what that scope lets it use before it asks, so it can offer a choice of model, pick a collection, or hide a feature its scope does not grant. Today it can only find out by asking and being refused. This item adds **one read**, filtered by the caller's scope, that says what the caller may use. It answers on both doors: a harness route for a paired caller, a stdio verb for a local one, and the OpenAI door's `GET /v1/models` filtered to the same backends.

**W3 — no capability discovery.** Measured 2026-11-14 on the branch binary (`apogee 0.2.0 (b37b0a9f)`) in a scratch `APOGEE_HOME` with two `mock` backends (`mock`, the default, and `mock2`) and one collection (`resumes`, one file ingested). Every file:line below re-read at HEAD `231fb049`; points 1, 3 and 6 re-probed the same day on the same binary in a fresh sandbox home with one `mock` backend, with the same answers.

1. **The one capability announcement names the protocol, not the install.** A stdio session's first event carries `capabilities` with exactly five keys: `events`, `accepts`, `tools` (a boolean), `ask` and `schema` (`presentation/machine/json_reporter.cpp:74-93`; the field tables at `presentation/machine/protocol.cpp:17-38`, `kCapabilities` and `kSession`). Probed: `complete -q -m mock --output-format stream-json hi` announced `accepts: []`, `tools: false`, `schema: "2026-11-01"`; `chat -m mock --output-format stream-json` announced the eight inbound lines. Neither named a backend, a collection, a tool or a recipe. The reference sets this boundary on purpose: "It names what the binary can do and nothing about this machine — never a key, a path into the data directory or a config value" ([machine-mode.md](../../../reference/cli/machine-mode.md):103-105).
2. **The OpenAI door lists only served backends, with no facts about them.** `GET /v1/models` answers each served backend's `id`, `owned_by`, `apogee_backend` and `default` (`presentation/httpserver/handler.cpp:869-885`; [http-api.md](../../../reference/cli/http-api.md):226-230). Probed under `serve --port 0` on loopback: one entry, `mock`. `mock2`, configured but not served (`HandlerOptions::served`, `httpserver/handler.h:66-70`), was absent. Nothing in the entry says whether the backend embeds or reads images, and nothing filters the list by caller. There is no caller yet: the plane is open (59a's W1).
3. **There is no route to ask.** In the same probe, `GET /v1/harness/capabilities` and `GET /v1/capabilities` each answered `404 no route`. Locally, `apogee __capabilities` is an unknown argument ("The following argument was not expected", exit `109`).
4. **The facts exist, but they are spread over six reads, and some of them carry paths.** `models list`, `embed list`, `agents list`, `task recipes`, `mcp list` and `symphonies list` each take `--output-format json`. A client would have to run all six and join the results. Several carry what discovery must not:
   - `embed info resumes` prints the database's absolute `path` and the ingested file names (`sources: ["a.md"]`);
   - `task recipes` has a `path` field on each recipe;
   - `agents list` has `prompts`, `schemas` and `save_dir`.
5. **No read answers "can it generate, embed, see?".** `models list` gives `roles`, which are role assignments, not capabilities. `mock` showed `chat, embedding, extraction` and `mock2` showed `""`, though both run the same provider. The answers live in the Harness's probes: `can_embed` (`business/harness/harness.h:257`, `harness.cpp:505-516`) and `accepts_images` (`harness.h:288`, `harness.cpp:572-580`). llama.cpp answers `accepts_images` from configured state without loading weights (`data/backends/llamacpp.cpp:553-561`).
6. **A newer inbound line would go unanswered.** `printf '{"type":"capabilities"}\n' | apogee chat -m mock --output-format stream-json` printed the `session` event and nothing else, and exited `0`. That is the protocol's rule: "a line it does not recognise is ignored, never fatal" (machine-mode.md:288-289). A host that sends a new line to an older child waits forever unless it first checks `accepts`.

This item delivers the **capability document**: one JSON document built by one core. It holds:
- the protocol version and schema date;
- the backends, each with its kind and whether it generates, embeds and sees;
- the collections and graphs;
- the tools;
- the task recipes and agents;
- the command families allowed.

Every list is filtered to the caller's scope. Something outside the scope is **absent**, never listed as denied, so one app never learns the names of another app's collections. The document has three renderings:
- `GET /v1/harness/capabilities` on 59a's harness plane, for a paired device or app;
- `apogee __capabilities`, the local stdio form, which sees everything the config has because a local caller has no scope;
- `GET /v1/models`, filtered to the scope's backends when the caller presents an app token. 59f lands that filter; this item holds it to the document, so the two never disagree.

**Core constraint(s).**
- **Remote mode is an add-on (the anti-goal).** Nothing here may make standalone use need a server, a network, a pairing or an account. The stdio verb reads the config and the stores, and never the device registry, the scope store or the TLS identity. An unscoped local caller sees everything the config has. `chat`, `complete`, `execute`, `task`, `acp` and the Mac app over pipes neither run nor need the verb. The session event is unchanged.
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). The document is assembled from views that cannot hold a secret: names, kinds, counts and booleans. It has no slot for an API key or `api_key_set`, a model path or mmproj path, a system prompt, an MCP server's `command`/`args`/`env`, a collection's database path or source file names, a recipe's `path`, or an agent's prompt files and save folder. The data directory, the config path and the caller's token never appear. Other apps' entries are filtered out before the document is built, never removed from a finished document.
- **Additive** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md); machine-mode.md:285-290). A new verb, a new route and a new `$defs` entry, with nothing renamed. The document carries `protocol_version` and `schema`. It grows by new keys and new list fields, each moving `schema`, and a client ignores what it does not know (rule 1, machine-mode.md:214). The `session` event's `capabilities` and its "nothing about this machine" rule stay exactly as they are. The capability document is a separate read, and it names configured entries **by name only**, never by value.
- **Stock clients keep their behavior** ([59a](paired-devices.md)'s rule). With no app token on the request, `GET /v1/models` is byte-identical to today's. With an app token, its entries are today's entries, only fewer. No extension field is added to them, because the capability document is where those facts live.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md); [ADR 0007](../../../adrs/cli/tab-completion.md)). One core feeds the verb and the route, so the two cannot drift. The verb is classified in the shell's table as hidden plumbing (`cli/tui_parity.cpp:81-89`, beside `__machine-schema`) and in the HTTP parity table (`tests/presentation/httpserver/parity_test.cpp:281-282`) as a read with its route. It is classified at 59d's network door too. A hidden verb is not in `__complete ""`'s top-level words (probed), so the GUI's parity fixture ([the GUI's ADR 0003](../../../adrs/gui/parity.md):37) is unchanged.
- **Never listens; serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). The verb opens nothing. The route sits on `serve`'s port behind 59a's gate, which runs before routing, so an unpaired probe gets `401` and learns nothing about which entries exist. Building the document loads no weights, starts no child and calls no vendor.
- **Capability probes never leak a cast** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Capability probes never leak a cast). `embeds` and `sees` come from the Harness's own probes (`can_embed`, `accepts_images`). No switch over backend types is written.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). The core sits in `operations` (Presentation), which both `cli` and `httpserver` already link (`cmake/modules.cmake:70-80`). The scope, the tool definitions and the command classification are handed to it as values. It includes nothing of `cli`, and `httpserver` keeps including nothing of `cli`.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Probes and tests use a temporary `APOGEE_HOME` and the mock backend. A live pass names `apogee-dev`, never `apogee` or `~/.apogee`.

**Seam + files.**
- `source/presentation/operations/capabilities.h/.cpp` (new): the one core.
  - `CapabilityInputs` holds the config, the Harness to probe, the tool definitions on offer, the command families with their leaves, the set the OpenAI door serves, and an optional `Scope` (59f's type). No scope means unscoped.
  - `capability_document(const CapabilityInputs&) -> nlohmann::json` builds the document. It filters each list by the scope before building any entry.
  - Its pieces reuse what exists:
    - `commands::collection_names()` (`operations/collections.h:20`), with chunk and source counts as `embed list` reads them;
    - the named graphs from the config's `graphs` section, and each collection's graph where one is built;
    - `tasks::recipe_book` (`business/tasks/recipe.h:114`), projected to name, description, input and tools, and never through `to_json` (`:119`), which carries `path`;
    - the agents as `agent_view` (`operations/read_views.h:23`) reads them, projected to name, description and whether an output schema exists.
  - `operations` gains a link to `tasks` in `cmake/modules.cmake`, pointing down. The tools arrive as values, so no link to `agent` is needed.
- `source/presentation/cli/capabilities_cmd.h/.cpp` (new): `apogee __capabilities`, hidden as `__machine-schema` is (`cli/machine_schema_cmd.cpp`). It writes the unscoped document to stdout and nothing else. The tools are the set `chat` builds with no flags from this config. The command families are every family, since a local caller runs anything it can type.
- `source/presentation/httpserver/capabilities_route.h/.cpp` (new): `GET /v1/harness/capabilities`, registered through 59a's harness-plane registration helper. The caller's credential resolves to its scope through 59f. It is served from serve's own Harness and serve's served set, with the scope applied. The tools are the ones serve's loop offers (none without `--tools`), and the families come from 59d's classification as the scope opens it.
- `source/presentation/httpserver/handler.cpp:869-885`: `list_models` is already filtered by the app's scope when this item starts; 59f lands that ("`GET /v1/models` lists the app's backends only", [59f](paired-apps-with-scopes.md)'s seam and acceptance). This item adds no second filter. It holds the two renderings to one narrowing: the route builds `backends` from the same scoped view 59f hands `list_models`, and a test pins that, for one token, the document's `openai: true` names equal `/v1/models`' `apogee_backend` values.
- `source/presentation/httpserver/mux.cpp`: the route row, so `cli.http_api_conformance` holds it to the reference.
- `source/presentation/machine/protocol.h/.cpp`: `$defs/capability_document` joins `machine_schema()`, so a client (the Swift package, darwin [7d](../../darwin/v0.2.4/apogeekit-swift-package.md)) generates its types. `kMachineSchemaVersion` (`protocol.h:47`) moves on.
- `source/presentation/cli/tui_parity.cpp`: `__capabilities` as `RunnerCovered` hidden plumbing. `tests/presentation/httpserver/parity_test.cpp`: its row, served by the route. `cli/remote_parity.cpp` (59d): `Refused`, naming `GET /v1/harness/capabilities`, the same way `chat` is refused naming `/v1/machine`.
- Docs:
  - [machine-mode.md](../../../reference/cli/machine-mode.md) gets a section, "What may I use: the capability document", with the shape, the filtering rule and the growth rule, placed after `capabilities` (:89-105) and saying how the two differ.
  - [http-api.md](../../../reference/cli/http-api.md) gets the route, plus a line under `GET /v1/models` (:226-230) on scope filtering.
  - 60d's guide (`reference/cli/apps.md`) links both sections.
  - DEVELOPER.md gets the file rows.
- Tests (hermetic: a temp `APOGEE_HOME`, the mock backend, a planted scope, handlers through `Mux::dispatch` with no port wherever a port is not the point):
  - `tests/presentation/operations/capabilities_test.cpp`:
    - an unscoped document lists every configured backend, collection, graph, recipe and agent;
    - a scope naming one backend and one collection yields only those, with every other list's out-of-scope entries absent (not `denied`);
    - `mock` reports `generates`, `embeds` and `sees` from the Harness probes; an `llamacpp` entry with `mmproj_path` reports `sees: true` and one without reports `false`, neither loading a model;
    - a provider that fails to construct is left out and named in `problems`;
    - `protocol_version` and `schema` equal `kMachineProtocolVersion` and `kMachineSchemaVersion`;
    - the document validates against `$defs/capability_document`.
  - `tests/presentation/httpserver/capabilities_route_test.cpp`:
    - `401` without a paired credential, before routing;
    - an app's token yields its scope's document;
    - a revoked token yields `401` on the next request;
    - for one app token, the route's `openai: true` backends are exactly `GET /v1/models`' entries (59f's filter), and without a token `/v1/models` is byte-identical to today's.
  - `tests/presentation/cli/capabilities_cmd_test.cpp`: stdout is one JSON object; the registry and scope files absent, and then unreadable, change nothing.
  - `tests/presentation/cli/leak_test.cpp`: a planted API key, model path, system prompt, MCP `env` value, collection path, source file name, recipe path and device or app token, none of them in the verb's output or the route's body.
  - `tests/presentation/machine/machine_mode_test.cpp`: the schema's new `$defs` entry and the moved date.
  - `cli.capabilities_discovery` (`tests/scripts/sh/capabilities.sh`), the real binary in a sandbox home. `__capabilities` lists both mock backends and the ingested collection. Under `serve --port 0` with 59f's test pairing, an app scoped to `mock` and `resumes` sees exactly those on the route and `mock` alone on `/v1/models`. A sample of the process tree shows no child started by the read.

**Reference.**
- Siblings:
  - [59f](paired-apps-with-scopes.md), paired apps and their scopes: the scope vocabulary this item filters by, and how a credential resolves to one.
  - [59d](reads-and-commands-over-the-network.md), the command door and its network-door classification: the leaves each command family opens.
  - [59a](paired-devices.md), the harness plane, its gate and `/v1/harness/`.
  - [59b](machine-mode-over-the-network.md), sessions over the paired connection, whose `session` event this item leaves unchanged.
  - [59h](structured-output-on-every-door.md), structured output on every door.
  - [60a](../v0.2.4/one-tool-definition.md), the one tool definition, whose JSON form the `tools` entries adopt.
  - [60c](../v0.2.4/app-tools-on-every-door.md), an app's own tools.
  - [60d](../v0.2.4/building-an-app-on-apogee.md), the integrator's guide, gated on this item.
  - [60e](../v0.2.4/maintaining-the-app-integration.md), the integration's law: a new capability's "discovery entry" is this document's.
  - Darwin [7d](../../darwin/v0.2.4/apogeekit-swift-package.md), the Swift package that decodes it.
  - Darwin [7a](../../darwin/v0.2.4/app-connection-local-or-server.md), whose server schema read is the Mac app's nearest precedent.
- In-house precedents:
  - The `session` event's `capabilities` and `__machine-schema` (machine-mode.md:89-105, :580-605): announce, never assume. A schema date lets a client compare its generated types with the binary.
  - The GUI's launch-time capability read (GUI SPEC: "its capabilities at launch -- never assumed").
  - `backend_view`'s `api_key_set` without `api_key`: a view that cannot carry the secret.
  - `task recipes`' `problems` list, which reports an entry that could not be read without failing the read.
- External prior art:
  - OpenAI's `GET /v1/models`, an unfiltered list of model ids. Divergence: Apogee filters by the caller's scope and keeps that route's shape for stock clients.
  - MCP's `initialize` capabilities and `tools/list`. Divergence: one document for the whole install, rather than one negotiation per feature.

**Decisions made** (dated):
- 2026-11-14: Specced from the apps-as-clients spike. The user's calls that shape it:
  - Taylor'd Portfolio uses the full harness: models, retrieval over its documents, and autonomous tasks on the server. So discovery covers collections, graphs, tools, recipes, agents and commands, not only models.
  - Apps reach Apogee through both doors: the OpenAI-compatible one for plain model calls, and Apogee's own protocol (stdio locally, the paired connection remotely) for the harness. So discovery answers on both doors.
  - Each paired app is held to a scope the person approves, and never reaches files, the shell or other apps' data unless granted. So the read is filtered, and what is out of scope is absent.
  - On the OpenAI door, a client app's own tools work the standard way: Apogee returns `tool_calls`, the app runs them, and the turn continues, within the app's scope ([60c](../v0.2.4/app-tools-on-every-door.md)). This reverses serve's recorded rule "It does not run tools the client defines" ([http-api.md](../../../reference/cli/http-api.md):1626-1627), and http-api.md changes with 60c, not with this item. So the document's `tools` lists the tools **Apogee** runs inside the scope, never the app's own, which the app already holds.
  - Apogee ships a Swift package other apps embed, factored from the Mac app's client core, and the Mac app depends on it too (darwin 7d). So the document has a schema the package generates types from.
  - The CLI SPEC amendment is approved and already written ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4, "Revised 2026-11-14").
  - Remote use is an add-on, and the CLI alone and the Mac app over pipes need no server. So the local stdio form is unscoped and reads nothing of pairing.
  - Taylor'd Portfolio's own side is not specced now.
- 2026-11-14: Placed as v0.2.3 (track 59), letter g, accepted with "execute the spike". Gated on 59f (there is no scope to filter by before it) and on 59d (the command families are its classification). It sits in v0.2.3 because 60d's guide (v0.2.4) gates on it, and a paired app's first act is to ask what it may use.
- 2026-11-14: The session event stays as it is. Its "nothing about this machine" rule is right for a stream that may be logged or replayed. The new document is a separate read that names configured entries by name only, never by value.

**Open calls:**
- [default: the stdio form is a hidden verb, `apogee __capabilities`, JSON on stdout and nothing else. It is a protocol read, as `__machine-schema` is, not a person's feature: a person sees an app's scope through 59f's own listing, and the install through `models list`, `embed list` and the rest. No new inbound machine-mode line is added. A paired caller has the route on the same connection's host, a local caller runs the verb, and a line would need `accepts` checked first (W3.6) for nothing the verb does not already give.]
- [default: the document's shape. Every list is `[]` when empty and never absent, and every entry is an open object (rule 1).
  ```json
  {"object":"capabilities","protocol_version":1,"schema":"<date>","version":"<apogee version>",
   "caller":{"kind":"local|device|app","name":"…"},
   "backends":[{"name":"mock","type":"mock","kind":"local|api|subscription","model":"…","default":true,
                "generates":true,"embeds":true,"sees":false,"openai":true}],
   "collections":[{"name":"resumes","chunks":1,"sources":1,"graph":false}],
   "graphs":[{"name":"…","collections":["…"]}],
   "tools":[{"name":"read_file","description":"…","input_schema":{…},"read_only":true}],
   "mcp_servers":[{"name":"…"}],
   "recipes":[{"name":"digest-folder","description":"…","input":"…","tools":true}],
   "agents":[{"name":"security-review","description":"…","output_schema":true}],
   "commands":[{"family":"models","leaves":["models list","models info"]}],
   "problems":[]}
  ```
  `caller` is `{"kind":"local"}` on the stdio form, with no name.]
- [default: `kind` is `local` (llamacpp, mlx; `mock` reports `local`, as `models list` sections it), `api` (a direct HTTPS entry) or `subscription` (a vendor CLI). It is a string, so a later kind is added without a break. `generates` is false only for an embedding-only entry (an `mlx` entry over an embedding model, 53h, `data/backends/mlx_embed.h`). `embeds` is `Harness::can_embed`. `sees` is `Harness::accepts_images`, which is true for a provider that does not declare the capability (`harness.h:281-288`), and the reference says so. `openai` is whether the OpenAI door serves the entry, so a caller knows which names `/v1/chat/completions` takes. Backends the harness plane can open but `serve` does not serve are still listed, with `openai: false`.]
- [default: until [60a](../v0.2.4/one-tool-definition.md) ships (v0.2.4), a `tools` entry holds the fields today's registry has (`name`, `description`, `input_schema`, `read_only` from the gate flags), spelled as 60a's JSON form spells them. When 60a lands, each entry becomes a `ToolDefinition` in its JSON form, a superset, with nothing renamed.]
- [default: an enabled MCP server is named in `mcp_servers` and its tools are not enumerated. Listing them means starting the server, and building the document starts no child. A client that needs them gets them when a session's tools are offered.]
- [default: suites, symphonies and the training reads are not listed in this item. Each is a later additive key, and 60e's census names a new capability's discovery entry.]
- [default: the document is computed per request with no cache. The route reads 59f's scope fresh, as 59a reads the registry by mtime, so a narrowed scope shows on the next request. Collections are the ones on disk at that moment.]
- [default: `GET /v1/models` with an app token is filtered (59f) and otherwise unchanged; this item adds no field to it. A request naming a backend outside the scope is 59f's refusal, not this item's.]
- [default: once [60c](../v0.2.4/app-tools-on-every-door.md) ships, the document gains `client_tools` (a boolean, additive, moving `schema`): whether this caller's scope holds 60c's `tools:client` class, so an app knows before it declares a tool whether the declaration will be refused. 60c hands this call here. On the stdio form it is `true`, since a local driver has no scope (60c's own rule). Before 60c the key is absent, and a client reads absence as false. The app's own tools are never listed in `tools`.]
- [default: at 59d's door, `__capabilities` is `Refused`, naming `GET /v1/harness/capabilities`. A door child runs under serve's root with no scope of its own, so it would answer the unscoped document. Refusing it keeps one answer per caller.]
- (consumed decision: 59f) The scope's vocabulary (which backends, collections, tools, commands and kinds of task), how a device or app credential resolves to a scope, and what a device's own scope is.
- (consumed decision: 59d) The network-door classification, which turns a scope's command families into leaves.
- (consumed decision: 60a) The tool definition's JSON form.
- [default: the document says nothing per backend about structured output. [59h](structured-output-on-every-door.md) hands this call here, and its own design settles it: every backend holds a per-request schema (natively where it can, by prompt elsewhere, the validator always), so the native mode is how Apogee holds it, not whether. A driver learns it from the `session` event's `capabilities.structured_output` and each `result`'s verdict (59h). A per-backend mode would need a new Harness probe (no cast), and it can join `backends` later as an additive field.]
- (consumed decision: 60c) The `tools:client` scope class and its default grant, which `client_tools` reports.
- (consumed decision: 60d) Where the integrator's guide documents the document and its compatibility promise.

**Guardrail(s).**
- `leak_test.cpp`'s planted values (key, model path, prompt, MCP env, collection path, source name, recipe path, tokens) are searched for in every rendering.
- A scope test pins absence, not denial, for every list.
- A byte-identity case pins `GET /v1/models` without an app token.
- A schema-validation case holds the document to `$defs/capability_document`.
- A process-tree sample proves the read starts no child.
- The parity tables (shell, HTTP, 59d's door) fail on a missing row.
- A run with the device registry and the scope store absent, and then unreadable, proves the stdio verb needs neither (the anti-goal).

**Acceptance criteria:**
- [ ] In a sandbox home with `mock`, `mock2` and the collection `resumes`, `apogee __capabilities` prints one JSON object listing both backends (`generates`, `embeds`, `sees` set), `resumes` with its counts, the shipped recipes and agents, and every command family, with `caller.kind` `local`. It carries no path, key or prompt.
- [ ] Under `serve --port 0` with 59f's test pairing, an app scoped to `mock` and `resumes` gets exactly those from `GET /v1/harness/capabilities`. `mock2` and every other collection are absent. Without a credential the route is `401`.
- [ ] With that app's token, `GET /v1/models` lists `mock` only. Without a token its body is byte-identical to today's.
- [ ] After the person narrows the app's scope (59f), the next request's document reflects it, and after revocation the route is `401`.
- [ ] `apogee __machine-schema` carries `$defs/capability_document` under a moved schema date, and the verb's and the route's output validate against it.
- [ ] `chat` and `complete` behave exactly as before, with the registry and scope store absent. The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user runs `apogee-dev serve --bind 0.0.0.0 --allow-remote`, pairs a scoped test app from a second machine with curl (59f), and reads its capability document over TLS.

**Scope note.** Earmarked for v0.2.3 (track 59); gated on 59f and 59d. [60d](../v0.2.4/building-an-app-on-apogee.md) gates on it. Out of scope:
- enforcing the scope on a request, and the scope filter on `GET /v1/models` itself (59f);
- structured output (59h);
- an app's own tools (60c);
- the Swift package's decoding (darwin 7d);
- enumerating an MCP server's tools;
- suites, symphonies and training in the document;
- any change to the `session` event;
- new fields on `GET /v1/models` entries;
- Taylor'd Portfolio's own side, not specced now (the user's call, 2026-11-14).
