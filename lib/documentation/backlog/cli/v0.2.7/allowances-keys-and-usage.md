# What each person may use: allowances, whose keys pay, and usage

**What / why.** The user's calls, 2026-11-14: the harness server serves **people**, an owner and members in a household or a small team. Each member has an **allowance** of what they may use, and "a member uses the server's local models unless the owner grants a cloud key with a limit" ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4, "Revised again 2026-11-14"). [63a](people-on-one-server.md) gives every credential a person and holds each one to its person's allowance (its row field and verb are `access`), and it refuses a metered backend for a member "until 63c" ([people-on-one-server.md](people-on-one-server.md):31, :68, :206). [63b](personal-data-has-an-owner.md) makes each person's records theirs. This item answers the rest of what a person may **use**: whose keys pay for their cloud calls and how far, which folders their tools work in, and how much each person used, as one JSON read for the owner's screens (darwin [10c](../../darwin/macos/v0.2.7/people-screens.md)).

It closes wall **P4** of the admin-mode and people spike (2026-11-14): members spend the owner's keys and one workspace, and nothing counts usage per person. Evidence re-checked 2026-11-14: sources at HEAD (`4acf4a2b`), and probes on the branch binary (`apogee 0.2.0 (b37b0a9f)`) in a scratch `APOGEE_HOME` under the scratchpad, killed after. One home had a `mock` backend and an `anthropic` entry `cloudy` with no key, under `serve --port 0 --all-backends --tools -v` on loopback. The other had `mock` and `household`, a `mock` entry whose script says `"metered": true`, standing in for a cloud backend with no cloud call, under `serve --port 0 --all-backends`.

**P4.1 — a key belongs to the install, and a served backend spends it for whoever calls.**
1. **One key per vendor, for everyone.** The store is "one slot per API-billing provider type" (`data/secrets/store.h:9-17`). The one resolver's chain is the entry's `api_key`, then that slot, then the conventional variable (`data/secrets/resolve.h:24-30`). It is resolved per backend entry when the backend is built (`data/backends/factory.cpp:60`, called at `:97`, `:144`, `:156`), with no caller in sight. Probed: `apogee auth list --output-format json` answered `{"backends":[{"name":"cloudy","source":"none","type":"anthropic"}],…}`, one source per backend. `serve` resolved it once at start: `skipping cloudy: no API key found for 'cloudy'`.
2. **Every served backend is every caller's, metered or not.** `resolve_served` checks a request's `model` against one server-wide list (`presentation/httpserver/handler.cpp:252-295`, the check at `:285`). Probed: `household` answered `200` (`"paid answer"`) with no bearer at all and again under `Authorization: Bearer apd_member_bob`, and `GET /v1/models` listed `household` and `mock`.
3. **"Never spends" exists, but only for the model's own initiative.** Whether a call costs money is a fact the provider states, and unknown is metered (`data/contracts/provider.h:416-426`; `Harness::generation_is_metered`, `business/harness/harness.cpp:564-570`). A suite member consulted by the model, or a delegation, refuses a metered backend (`business/agentloop/member_call.cpp:62-66`; `business/tools/delegate.cpp:165-170`). Nothing applies it to a caller. 63a closes the hole by refusing a member every metered backend, so until this item a member cannot use the cloud at all, even when the owner wants them to.

**P4.2 — nothing counts what anyone used.**
1. **Usage exists per call and is dropped.** `harness::Usage` holds prompt and completion tokens, where zero means "the provider did not report it" (`data/contracts/types.h:289-303`). A served response carries it only when reported (`handler.cpp:718-722`). The API backends parse it (`data/backends/anthropic_wire.cpp:528-530`). The handler estimates an unreported answer for its token-count frame (`handler.cpp:614-620`), and keeps nothing.
2. **No record keeps it.** Serve's one log line per turn is the backend, the session, the tools and the milliseconds (`handler.cpp:582-586`). Probed: `chat household 0ms`, twice, with no caller and no tokens. The request logger prints method, path and status only (`presentation/httpserver/serve.cpp:179-185`). The saved session's keys are `backend, chat_id, compactions, messages, schema_version, started_at, turns, updated_at` (probed), with no tokens and no caller. [62e](../v0.2.6/connections-and-activity.md) adds counts per credential, in memory, since the server started. [62f](../v0.2.6/audit-log.md) records each inference request with its usage for 90 days, and leaves "the usage ledger and limits" to this item (`audit-log.md:108`).
3. **No money figure for a served backend.** The only cost in the source is the Claude CLI's own `total_cost_usd` (`data/backends/claude_cli_events.cpp:116`), from a vendor CLI that `serve` never dispatches to. No backend entry carries a price, and a list carried in the binary goes stale the day a vendor changes it ([ADR 0009](../../../adrs/cli/built-in-model-lists.md):7).
4. **No metering is a recorded non-goal.** "No customer accounts, no billing and no metering" ([paired-devices.md](../v0.2.3/paired-devices.md):3, :67, :116). "No tenants, quotas, accounting or metering" ([many-sessions-at-once.md](../v0.2.3/many-sessions-at-once.md):86, :129). "Rate limits, quotas, metering and billing" are out of 59f's scope ([paired-apps-with-scopes.md](../v0.2.3/paired-apps-with-scopes.md):186). This item revises that, for the owner's own budget only (Decisions made).

**P4.3 — one workspace, and shell rides with notes.**
1. **One tool registry, one folder.** `serve --tools` builds one registry for the server's life (`presentation/cli/serve_cmd.cpp:358-372`). The file tools are rooted at `tools.fs_root`, or else the folder `serve` was started in (`business/tools/toolsets.cpp:50-62`, used at `:74`). The notes tools use the layout's `notes/` (`toolsets.cpp:91-93`), a row marked not private (`data/contracts/layout.cpp:37`). Probed start line: `tools: 25 (files under …/allowance-probe-63c/work, shell, git, notes, rag, graph, fetch_url to listed hosts, todo_write)`. Every member's request would read and write the owner's launch folder.
2. **The class that grants notes also grants shell.** 60a's classes are `read`, `edit`, `write` ("any other writer"), `outbound` and `listed` ([one-tool-definition.md](../v0.2.4/one-tool-definition.md):68). So `write_note` and `run_command` are both `write`, and 59f's approval line reads "write files and run shell commands under <folder>" ([paired-apps-with-scopes.md](../v0.2.3/paired-apps-with-scopes.md):141). A member given notes is given a shell. The shell runs as the server Mac's account, so no folder confines it.

This item delivers **what each person may use, and a count of what they used**:
- **The household key.** The owner may grant a member one or more metered backends, always with a **limit**: tokens or estimated cost, per day, week or month. The key that pays is the one the resolver already finds for that backend entry. A household that wants a separate key gives the entry its own `api_key: "${HOUSEHOLD_KEY}"`, the resolver's first rung. Past the limit, the member's metered calls are refused (`429 allowance_exhausted`), and their local models keep working.
- **One meter, at the Harness.** Every model call a member's work makes is checked before it is made and counted after it returns: through every door, through tools and symphonies that call models, through embeddings, and through tasks. A door never keeps its own count.
- **A files root and a notes root per person.** A member's file tools, git walk and shell working folder use their files root, and their notes tools use their notes root (63b's folder). Both are bound per request, so one served registry serves everyone. `shell` becomes its own tool class, held apart from `write`.
- **Usage per person.** A durable ledger of requests, model calls, tokens and estimated cloud spend, per person, credential, backend and day. `apogee usage --output-format json` reads it for the owner (the server window over pipes), the admin plane serves the same document, and a member reads only their own.
- **What an allowance grants, at each kind.** 59f's five kinds, enforced by its one narrowing as 63a composes it, with the answers 63a, 63b and 63d leave to this item:
  - `backend:` local only for a member (63a's refusal); a metered backend only through `cloud`, with a limit.
  - `tools:` the classes in the access, through `narrow_tools`, with `shell` now its own class; `outbound` reaches the config's hosts.
  - `collection:` reading a shared library; writing into it also takes the writing command family.
  - `command:` stays 63a's `command_refusal` over the composed scope. This item adds that a granted command whose work calls a model (`embed ingest`, `graph build`) is metered for the person whose door ran it.
  - `task:` stays 63a's `task_refusal`. A granted `task:goal` runs on the person's backends, its metered calls draw on their limit through 63e's `started_by`, and it halts with reason `allowance` past it.

**Core constraint(s).**
- **Remote use is an add-on (the anti-goal).** Nothing here may make standalone use need a server, a network, an account or a person. `chat`, `complete`, `execute`, `task`, `acp`, `mcp serve` and the Mac app over pipes run as the owner: never metered, never limited, and they never write the ledger (only `usage` reads it). The meter is installed only by `serve` and by a child `serve` hands a scope file. With none installed, the Harness behaves byte for byte as today. The owner is never limited anywhere.
- **No accounts, no billing.** A limit is the owner's own budget for the owner's own key. No money moves, nobody is invoiced, and no member has an account. A cost figure is an estimate from prices the owner states, said as such everywhere it is shown, never "your bill".
- **Model initiative never spends** (SPEC → Suites & symphonies; `member_call.cpp:62-66`). A member's cloud grant does not loosen it: a delegation or a consulted member still refuses a metered backend whoever the person is.
- **One config mutation path** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → One config mutation path). A person's grant, limit and roots live in 63a's `people.json`, never in `config.json`. A backend's price is config (`backends.<name>.price`), written through `contracts/config_edit.h` like any other key.
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). A grant names a backend, never a key. The meter holds a backend name and counts, never a `KeyResolution`. A refusal names the backend and the limit, never where its key came from. The ledger and every usage view structurally lack a key field.
- **Never contents** (63b's rule). The ledger holds ids, names, backends, times and counts. It never holds a prompt, an answer, a session title, a task goal or a tool's arguments.
- **Older versions keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The access row's new fields, `price`, the `usage` verb and routes, the `allowance_exhausted` type, the `Retry-After` header, the `shell` class and a mock script's `usage` are all additive. A stock OpenAI client with no Apogee credential gets the same routes, bodies and errors. A scope or access stored before this item that names `write` is read as `write` plus `shell`, so nothing narrows behind anyone's back.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). `usage` answers from the CLI (`--output-format json`), on the admin plane and on the harness plane (a member's own), and is classified for the shell and for 59d's door. `people access`'s new flags reach the admin plane through 63a's `PATCH`, validated by the same functions.
- **Never listens; serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). The meter and the ledger are socket-free. `httpserver/serve.cpp` stays the one object that may reference `listen`/`accept`.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). The ledger sits in Data, in a new module `usage`. The meter's interface is declared in `contracts/provider.h` beside `TurnObserver`, so the Harness calls it without linking anything above it. Its implementation sits in 59f's Business module `scope`. The verb sits in `cli` and the routes in `httpserver`. Each new link is a deliberate edit to `cmake/modules.cmake`.
- **One layout declaration** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → One layout declaration). The ledger's folder is a row in `kDirectories`, never a path built where it is written.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Probes and tests use a temporary `APOGEE_HOME` under the scratchpad and the mock backend, with a `"metered": true` script standing in for a cloud key. No test makes a cloud call. A live pass runs `apogee-dev serve` over `~/.apogee-dev`, never the release `apogee` or `~/.apogee`.

**Seam + files.**
- `source/data/secrets/people.h/.cpp` (63a's registry): a member's `access` row gains three fields, each absent by default.
  - `cloud`: the metered backends granted, by name, and `limit {unit: tokens|cost, amount, period: day|week|month}`.
  - `files_root`: a folder, empty meaning the default (below).
  - `notified`: the period in which the 80% notice was last sent, so it is sent once per period.
  The view carries them. `people.json`'s lock and mtime re-read are 63a's.
- `source/data/contracts/config.h/.cpp`: `BackendConfig::price {input_per_million, output_per_million}`, optional, and `usage.currency` (default `USD`). Loaded, validated (non-negative, finite) and shown by `config get`. A price change reaches a running server by [62b](../v0.2.6/serve-reload-and-served-vs-disk.md)'s reload.
- `source/data/contracts/provider.h`: `CallGate`, beside `TurnObserver` (`:451-453`):
  - `admit(backend, request&)` returns a refusal or nothing, and may lower `request.max_tokens`;
  - `record(backend, usage, estimated)` runs after the call returns.
- `source/business/harness/harness.h/.cpp`: `set_call_gate(std::shared_ptr<CallGate>)` beside `observe_turns` (`harness.h:175-177`). `chat`, `stream_chat` and `complete` (`:231-238`) and the embedding path ask the gate before a call and report after it, the unreported case estimated with `agentloop::estimate_tokens` as the handler's frame already does (`handler.cpp:614-620`). With no gate set, nothing changes.
- `source/business/scope/meter.h/.cpp` (in 59f's module): `PersonMeter`, the `CallGate` for one caller.
  - An unmetered call is admitted and counted.
  - For a member, a metered call is refused when the backend is not in `cloud`, or when the period's total has reached the limit.
  - Otherwise it is admitted, with `max_tokens` lowered to what the limit leaves when that is smaller (for a cost limit, through the backend's output price).
  - It records into the ledger, and sends the 80% notice once per period through the run's event sink.
  - For the owner and for anonymous callers it counts and never refuses.
- `source/business/scope/scope.h/.cpp` (59f's and 63a's):
  - `access_refusal` (63a) gains the grant rules: a metered backend only through `cloud` and only with a limit; a cost limit only when every granted backend has a `price`; a `files_root` that contains `APOGEE_HOME`, another person's root or the owner's `tools.fs_root` refused.
  - `tool_class` gains `shell`: `run_command`, and any native tool that starts a process. `narrow_tools` drops it unless the scope names `tools:shell`.
  - A scope or access stored with `write` and no `shell` is read as both, when it was written before this item (a version mark on the row).
- `source/data/usage/ledger.h/.cpp` (new Data module `usage`): `UsageLedger` over `<APOGEE_HOME>/usage/<YYYY-MM>.json`, `0600`. A row is (day, person id or `owner` or `anonymous`, credential name and kind, backend, metered) with `requests`, `calls`, `prompt_tokens`, `completion_tokens`, `estimated_tokens`, `cost` and `refusals`.
  - Every write holds `<YYYY-MM>.json.lock` and lands through `write_file_atomically(…, true)`, since `serve` and its children write the same month.
  - A metered call writes at once, because a limit reads it. Unmetered counts are flushed at most once a minute and at exit.
  - `totals(person, period)` is what the meter reads.
  - Months past retention are deleted at the first write of a new month.
  - A corrupt month is read as empty with a warning and never overwritten, and a meter that cannot read its month refuses a member's metered calls (fail closed) while local calls go on.
- `source/data/contracts/layout.cpp`: a `usage` row (private, user data; `admin` in 63b's scope column). The array grows from 20 to 21, and 63b's counts move with it.
- `source/data/backends/mock.cpp`: a script turn may carry `"usage": {"prompt_tokens": N, "completion_tokens": M}` (`MockTurn::usage` exists, `mock.h:57`, but the parser reads no such key, `mock.cpp:151-155`). With `"metered": true` (`mock.h:92-94`), that makes a stand-in cloud backend for every test here.
- The doors: `presentation/httpserver/handler.h/.cpp`, 59b's `machine_sessions`, 59d's `command_door` and 59e's served task.
  - Each request builds its caller's `PersonMeter` and sets it on the Harness for that request's calls. A child gets it through 59f's `--scope-file`, which carries the person, the `cloud` grant and the roots (63a already carries the person and access there).
  - A refusal from the meter becomes `429`, type `allowance_exhausted`, with `Retry-After`, on the OpenAI door and the harness plane. In machine mode it is the turn's error event. In a task, the task halts with reason `allowance` and is resumable.
  - `resolve_served`'s refusal of a metered backend outside the grant is 59f's `403 scope_refused`, naming the `people access NAME --cloud BACKEND --limit …` line that would grant it.
- Per-person roots: the file tools, git's walk and the shell's working folder read the root at call time from the request's person, by the per-request binding 63b uses for notes (the todo board's precedent, `serve_cmd.cpp:370-372`). A member's root defaults to `files/` under 63b's `person_home(id)`, created `0700` at first use. The owner's stays `tools.fs_root` or the launch folder. 59b's `--workspace` for a member's session must lie inside their files root.
- `source/presentation/cli/people_cmd.cpp` (63a's `people access`): `--cloud BACKEND`… with `--limit tokens:N/PERIOD` or `cost:N/PERIOD`, `--no-cloud`, and `--files-root PATH` (or `--files-root default`). The plain-words access printed before the row is written gains the cloud line, the files root, the notes root, and what `tools:shell` means.
- `source/presentation/cli/usage_cmd.h/.cpp` (new): `apogee usage [--person NAME|all] [--period day|week|month] [--since DATE] [--by backend|credential|day] [--output-format text|json]`, with help and completion ([ADR 0007](../../../adrs/cli/tab-completion.md)) completing people, periods and backends.
- `source/presentation/httpserver/admin_usage.h/.cpp` (new): `GET /v1/admin/usage`, the same document, taking the person's id (never a name) and the same options. `GET /v1/harness/usage` answers the caller's own person only.
- `source/presentation/cli/serve_cmd.cpp`: the start note counts members with a cloud grant. On an open inference plane with a metered backend served, it says anonymous calls spend that key unlimited and names `--devices-only`.
- `source/presentation/cli/tui_parity.cpp` and 59d's `remote_parity.cpp`: `usage` classified. For a member's credential it answers their own row, and `--person` is refused.
- `source/presentation/cli/check.cpp`: the `usage` row's mode (`0700`, files `0600`), and a row naming a member whose cost limit covers a backend that has lost its price.
- Docs:
  - [http-api.md](../../../reference/cli/http-api.md): a chapter, "Allowances, keys and usage". It covers the household key, limits and periods, `allowance_exhausted` and `Retry-After`, the files and notes roots, the `shell` class, the usage document and its routes, the estimate's honesty, and anonymous spend on an open plane. It also gets a dated revision of the "no metering" line.
  - [machine-mode.md](../../../reference/cli/machine-mode.md): the turn error and the 80% notice.
  - The commands reference: `usage`, and `people access`'s new flags.
  - The config reference: `price` and `usage.currency`.
  - CLI CLAUDE.md: a spend sub-rule, "a member's metered call is counted and held at the Harness, never at a door". DEVELOPER.md: the file rows.
- Tests (hermetic: a temporary `APOGEE_HOME`, the mock backend with a `"metered": true` script for the cloud, handlers driven through `Mux::dispatch` with no port wherever a port is not the point, an injected clock for periods):
  - `tests/data/usage/ledger_test.cpp`:
    - a round trip;
    - `0600` after every write;
    - two writers under the lock;
    - period totals across day, week and month boundaries;
    - retention;
    - a corrupt month read as empty and never overwritten.
  - `tests/business/scope/meter_test.cpp`:
    - local admitted and counted;
    - metered outside the grant refused;
    - under the limit admitted, with `max_tokens` lowered near the edge;
    - at the limit refused, with the reset time;
    - a cost limit through the price;
    - unreported usage estimated and marked;
    - the 80% notice once per period;
    - the owner and anonymous never refused;
    - an unreadable ledger refusing metered and admitting local.
  - `tests/business/scope/scope_test.cpp`: the grant rules, `shell` split from `write`, a pre-63c `write` read as both, and the `files_root` refusals.
  - `tests/business/harness/call_gate_test.cpp`: every call path consults the gate (chat, stream, complete, embed, a delegation, a symphony stage). A planted path that skips it fails.
  - `tests/presentation/httpserver/allowance_test.cpp`:
    - a member's chat on `household` without a grant is `403`;
    - with a grant it is `200` until the limit, then `429 allowance_exhausted` with `Retry-After`, while `mock` stays `200`;
    - the files root and notes root as the tools see them;
    - `run_command` not offered under `write`;
    - `GET /v1/admin/usage` and a member's `GET /v1/harness/usage`.
  - `tests/presentation/cli/usage_cmd_test.cpp`: text and `--output-format json`, `--person`, `--by`, `--since`, an empty ledger, and an unknown person.
  - `leak_test.cpp`: a planted key and a planted prompt are absent from the ledger, every usage view, the refusal, and the start note.
  - A shell case, `cli.allowance_limits` (`tests/scripts/sh/allowance_limits.sh`): the spike's metered stand-in probe turned into a regression. The real binary runs under `serve --port 0 --tls on --devices-only` on loopback with `mock` and a metered `household`. A member is added and their device paired with `--print-token`. `household` is refused, then granted with `--limit tokens:100/day`. Scripted usage reaches the limit, and the next call is `429` while `mock` answers. `apogee usage --person bob --output-format json` shows the counts and `refusals: 1`.
  - `cli.http_api_conformance` covers the new routes; `cli.install_parity` gains the `usage` row; `cli.no_listen_symbols` is unchanged.

**Reference.**
- In-house precedents:
  - "Model initiative never spends" and `generation_is_metered` (27f, 54b): the provider's own word on cost, unknown being metered.
  - The one key resolver (`resolve.h`) and its per-entry `api_key`, which is how a household key gets its own entry with no new secret store.
  - 59f's `AppScope`, its narrowing and its scope files, and 63a's composed ceiling, which this item extends by a kind (`cloud`) and a class (`shell`).
  - The task's grants and ceiling (27i, `grant_refusal`): a standing permission held under the config.
  - The turn observer (28c, `provider.h:451-453`): the Harness already tells a watcher after each call, and the meter is the same hook with a check before.
  - The handler's token estimate (`handler.cpp:614-620`) and its `"estimated"` mark, which 62e's counts keep too.
  - 59a's registry lock and mtime re-read, the ledger's model.
- Siblings:
  - [63a](people-on-one-server.md): the person, the access row and its verb, the presets, the never-a-member families and the composed ceiling. This item is gated on it.
  - [63b](personal-data-has-an-owner.md): `person_home`, the notes root bound per request, and a member's own collections. It consumes this item's answer on `collections`.
  - [63d](consent-goes-to-its-person.md): who answers a question. It consumes which classes and hosts a person is offered.
  - [63e](task-slots-per-person.md): the per-person task queue, whose `started_by` the meter charges.
  - [59f](../v0.2.3/paired-apps-with-scopes.md), [59b](../v0.2.3/machine-mode-over-the-network.md), [59d](../v0.2.3/reads-and-commands-over-the-network.md), [59e](../v0.2.3/tasks-outlive-the-connection.md): the scope and the doors the meter rides.
  - [59c](../v0.2.3/many-sessions-at-once.md): lanes and fairness between sessions, which this item leaves alone.
  - [60a](../v0.2.4/one-tool-definition.md): the permission classes `shell` is split from.
  - [62b](../v0.2.6/serve-reload-and-served-vs-disk.md): reload, which a price change needs. [62e](../v0.2.6/connections-and-activity.md): live activity per credential, in memory. [62f](../v0.2.6/audit-log.md): the audit, which records grants, limit changes and the first refusal of a period. [62g](../v0.2.6/administering-local-and-marked-devices.md): administering local plus marked devices, which bounds who reads everyone's usage over the admin plane. [62h](../v0.2.6/local-admin-reads.md): what a local admin screen reads, which `usage` joins.
  - darwin [10c](../../darwin/macos/v0.2.7/people-screens.md): the people screens, gated on 63a and this item. They show each person's usage and remaining limit, and edit the grant.
- External prior art: the spike's notes on Plex Home (libraries shared per person) and Open WebUI (per-resource access) shaped the per-person grant. Per-key spend limits in the cloud vendors' own consoles were not checked for this document and are not relied on. The deliberate divergence: the limit is enforced by the server before the call, in the person's own words, and needs no vendor feature.

**Decisions made** (dated):
- 2026-11-14: Specced from the admin-mode and people spike. The user's calls that shape this item:
  - The server serves a household **or** a small team, and the design suits both.
  - Members use the server's **local models only**, unless the owner grants a household cloud key, **with a limit**.
  - The owner sees members' metadata (devices, apps, activity, which model, when), **never chat contents**. Whoever controls the server Mac can still read its disk, and Apogee says so.
  - The words are **people** and **access** (Owner, Member), never "roles".
  - Live changes apply on the spot, and what still needs a restart is said.
  - Collections are shared libraries handed out per person, plus a member's own private ones. Notes and saved decisions are personal by default.
  - The model, from the prior art: the owner is implicit (the OS account that owns `APOGEE_HOME` on the server Mac), and members each hold an allowance in 59f's `AppScope` shape, with presets (Full, Family, Limited) instead of groups. A guest is a member on Limited with nothing kept.
  - Members' apps are paired by the owner at the server Mac, capped at that member's allowance. Tasks queue per person rather than being refused (63e).
  - An audit is kept 90 days, of who did what and when, never request contents (62f).
  - Administering happens in a separate **Apogee Server** window of the macOS app, offered only on the Mac that serves, driving the local CLI over pipes. It installs the server as a background service (launchd) and starts, stops and restarts it. Administering is local by default, plus the paired devices the owner marks (62g, revising 59d's one admin token for any network caller). This item's `usage` is one of the reads that window makes.
  - The SPEC amendments are already written ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4, "Revised again 2026-11-14"; [GUI SPEC](../../../assistant/gui/SPEC.md) → Core surfaces 3).
  - The standing law: remote use is an add-on, and the CLI alone and the Mac app over pipes stay complete with no server.
- 2026-11-14: Placement, the user's call: both halves land before v0.3.0, the server console in v0.2.6 (CLI track 62, darwin/macos 10a–10b) and people and access in v0.2.7 (CLI track 63, darwin/macos 10c). This item is CLI 63c, v0.2.7, track 63, gated on 63a (and through it on 59f). Darwin 10c gates on it and 63a. Nothing already queued is renumbered. It collapses three walls into one item: whose key pays (P4.1), what was used (P4.2), and the one workspace (P4.3). They share one seam: what a person's request may reach, and what it costs.
- 2026-11-14: This item revises the recorded "no metering" (59a :3, :67, :116; 59c :86, :129; 59f :186), and only to this extent: `serve` counts what each person used and holds a member to the owner's limit on the owner's key. There are still no accounts, no billing, no payment and no quotas on local models. The revision is dated in [http-api.md](../../../reference/cli/http-api.md) and the release notes, and the three documents' lines get a dated note when this item builds.
- 2026-11-14: This item lifts 63a's metered refusal for members ([people-on-one-server.md](people-on-one-server.md):206) through the `cloud` grant alone. `--allow backend:<metered>` stays refused and names `--cloud`, so a metered backend can never reach a member without a limit.

**Open calls:**
- [default: a "household cloud key" is a **grant of metered backends**, not a new kind of secret. The key that pays is the one the resolver finds for each entry. To keep household spend apart from the owner's, the owner adds an entry with its own key (`"api_key": "${HOUSEHOLD_ANTHROPIC_KEY}"`) and grants that entry. The reference shows the recipe, and `people access` says which entries share a key slot with the owner's own. A member bringing their own key is a later call.]
- [default: one limit per member, across every metered backend they are granted, in **tokens** (prompt plus completion) or in **estimated cost**, per **day**, **week** (from Monday) or **month**. Periods follow the server Mac's local calendar. A metered grant without a limit is refused, so every member's cloud use has a ceiling. The owner can set a large one. Per-backend limits are a later call.]
- [default: cost is an estimate from prices the owner states per entry (`backends.<name>.price`, per million input and output tokens, in `usage.currency`). Apogee carries no price list, by ADR 0009's reasoning. A cost limit is refused at grant while any granted backend has no price. Every cost figure is labelled "estimated from your prices". Vendor server-side tools (47a) are off in a member's narrowed view, so a per-call fee Apogee cannot see never arises.]
- [default: the check comes **before** each metered call, against the period's recorded total. A call that starts under the limit runs to completion, with `max_tokens` lowered to what remains when that is smaller. So the overshoot is at most one call's prompt, plus the calls already in flight, and `usage` shows it as "over by N". There is no reservation across processes.]
- [default: when a provider reports no usage on a metered call, the call is counted and its tokens are estimated (`estimate_tokens` over the prompt and the answer), marked `estimated` in the ledger and in every read. The API backends report usage. The estimate only covers the gap.]
- [default: past the limit, a member's metered call is refused with `429`, type `allowance_exhausted`, and `Retry-After` set to the seconds until the period resets. The message names the person, the limit, what was used, when it resets, and the local backends still theirs, for example: "Bob's cloud allowance for November is used up (500,000 of 500,000 tokens). It resets on 2026-12-01. This server's local models are still Bob's: mock." Nothing switches to a local model on its own. In a turn, the loop's next metered call is refused and the turn ends with what it has, said. A task halts with reason `allowance` and resumes after the reset or a raised limit.]
- [default: at 80% of the limit the member's next turn gets one notice per period: an `apogee_events` notice on the OpenAI door, a notice event on the harness plane. `GET /v1/harness/usage` shows what is left at any time.]
- [default: usage is counted for **everyone served**, the owner included, so the owner's screens show the whole server. Standalone use is never counted. Anonymous callers on an open inference plane are counted as `anonymous` and never limited, as today's open plane is unlimited. The start note says so when a metered backend is served there, and names `--devices-only`. An app's calls count to its person, with the app named as the credential.]
- [default: what is counted: `requests` (one per request at a door: an OpenAI-door request, a harness turn, a command, a task round), `calls` (model calls, which a tool loop multiplies), prompt and completion tokens, estimated tokens, estimated cost, and refusals. Local calls are counted too, with no cost, so "which model, when" covers everything. Embedding calls count as calls with their tokens.]
- [default: the ledger keeps daily rows for **13 months**, enough for a year's view against the same month a year before. Older months are deleted at the first write of a new month. The audit keeps its own 90 days of per-request lines (62f). The ledger is the count, and the audit is the trail.]
- [default: there is one durable count. 62e's in-memory table stays its own since-start view of live activity, and its numbers are not written twice. Where the server window wants a person's month it reads `usage`. Where it wants who is connected now it reads 62e.]
- [default: `apogee usage` is owner-wide on the server Mac (the CLI and the Mac app over pipes) and on the admin plane, which 62g keeps local plus the devices the owner marks. Through 59d's door and `GET /v1/harness/usage` a member reads only their own row, and `--person` is refused. A member never sees another person's usage, nor the owner's.]
- [default: the document is `{"object":"usage","period":{"unit","start","end"},"currency","people":[{"id","name","owner","limit","used":{"requests","calls","prompt_tokens","completion_tokens","estimated_tokens","cost","cost_estimated"},"remaining","over","refusals","last_used","by_backend":[…],"by_credential":[…]}]}`. `--by day` adds `days`. A person removed by 63a stays in past months as "a removed person" under their id.]
- [default: a member's **files root** is `person_home(id)/files/`, created `0700` at first use. The owner may set another with `people access NAME --files-root PATH`: an existing absolute folder the server's account owns. It is refused when it contains `APOGEE_HOME`, another person's root or the owner's files root, so no member reaches anyone else's data through file tools. The owner's root is unchanged. Limited has no tools, so it has no root.]
- [default: the **notes root** is 63b's per-person `notes/`, bound per request. This item only shows it in the plain-words access and in `people list --json` (`files_root`, `notes_root`).]
- [default: `shell` is a tool class of its own, split from `write`, for every credential: `run_command` and any native tool that starts a process. `write` keeps file writes and notes. No preset grants `shell`, 63a's Full included. The owner grants it by name (`--allow tools:shell`), and the plain-words line says it "runs commands as <account>, which can reach anything that account can, not only <files root>". An app scope or member access stored before this item with `write` keeps `shell` (ADR 0006), and `people list`/`apps list` mark it "shell, from write" so the owner can remove it.]
- [default: command families and task kinds keep 63a's enforcement (`command_refusal` and `task_refusal` over the composed scope), and this item adds no new kind for them. What it adds is the charge: a granted command or task whose work calls a metered model draws on its person's limit, through the one meter, and is refused or halted past it like a turn.]
- [default: a person is offered the tool classes in their access through 63a's narrowing. `outbound` reaches the config's `tools.allowed_hosts`, the same list for everyone. Per-person host lists are a later call. Whether an offered tool still asks is the config's level and the person's own answers (63d).]
- [default: `collection:NAME` in an allowance grants **reading** a shared library: retrieval, `auto_rag`, and the graph tools over it. Writing into it (`embed ingest`, `knowledge share --into`) also takes that writing command family in the allowance, as 59f's rule already composes ("ingests only into the scope's collections"). A member's own collections are theirs to read and write with no grant (63b).]
- [default: an embedding through a metered embedder is a metered call for the person whose request makes it. A member without a cloud grant searching a shared collection whose embedder is metered gets its lexical index only, and the turn's retrieval line says so. Ingesting into a member's own collection through a metered embedder needs the grant and counts against the limit.]
- [default: every change applies at the next call with no restart: the grant, the limit and the roots, because `people.json` is read by mtime. A price is config, and reaches a running server through 62b's reload. `people access` and `config set backends.X.price` say which applies now and which needs the reload.]
- [default: granting or removing a cloud grant, changing a limit or a files root, and setting a price are audit actions (62f, through 63a's audited verb and the config path). Only the first `allowance_exhausted` of a person's period goes to the audit, with the count in `usage`, so a retrying phone cannot flood the trail.]
- (consumed decision: 63a) The person, the access row, `people access`, the presets and their contents, the never-a-member families, the composed ceiling, and the Limited guest who keeps nothing.
- (consumed decision: 63b) `person_home`, the per-request notes binding, a member's own collections, and the layout's scope column.
- (consumed decision: 63d) Where a person's "always" answers are kept, and who answers a question.
- (consumed decision: 63e) The per-person task queue and `started_by`, which the meter charges.
- (consumed decision: 59f) `AppScope`, `KIND:NAME`, `narrow_config`/`narrow_tools`, `--scope-file`, `scope_refused`.
- (consumed decision: 62e, 62f) Live activity per credential, and the audit's format and retention.
- (consumed decision: darwin 10c) How usage, limits and the grant look in the server window.

No **[user]** call blocks this item.

**Guardrail(s).**
- **The spike's probe is the regression fixture.** `cli.allowance_limits` replays the metered stand-in probe (a `"metered": true` mock answering an anonymous caller and `apd_member_bob` alike) as a member's device, and pins: refused without a grant, admitted with one, `429` past the limit while local still answers, and the count in `usage`.
- `call_gate_test` plants a model call path that skips the gate and fails. Every path a member's turn can take to a model goes through the one meter.
- `meter_test` pins the period arithmetic on an injected clock, the lowered `max_tokens`, the estimate mark, fail-closed on an unreadable ledger, and that the owner and anonymous are never refused.
- `leak_test` checks that no key, prompt, answer or title reaches the ledger, a usage view, a refusal or a log line.
- A test drives `chat`, `complete`, `execute` and `task run` with a metered mock and no `people.json`, ledger or scope file. They behave byte for byte as today, and no `usage/` file is written (the anti-goal).
- A conformance case shows the inference plane byte-identical for a stock client with and without people, grants and a ledger.

**Acceptance criteria:**
- [ ] In a sandbox home with `mock` (the default, local) and `household` (a `"metered": true` mock script with scripted usage), and member `bob` on Family: `apogee people access bob --allow backend:household` is refused, naming `--cloud`. `--cloud household` with no `--limit` is refused. `--cloud household --limit cost:5/month` is refused while `household` has no price.
- [ ] `apogee people access bob --cloud household --limit tokens:1000/day --json` prints, in plain words, that Bob may use `household` up to 1,000 tokens a day on the key that entry resolves, and that his local models are unlimited.
- [ ] Under `serve --port 0 --tls on --devices-only` on loopback, with Bob's device token: `household` answers `200` until the scripted usage passes 1,000 tokens. The next call is `429 allowance_exhausted` with `Retry-After` and the reset time, while `mock` still answers `200`. The owner's device is never refused.
- [ ] `apogee usage --person bob --output-format json` shows Bob's requests, calls and tokens by backend and by credential, `remaining: 0`, the overshoot, and `refusals`. `GET /v1/admin/usage` returns the same document. Bob's `GET /v1/harness/usage` returns his row alone, and `usage --person owner` through his door is refused.
- [ ] With `backends.household.price` set and the limit changed to `cost:0.01/day`, the read shows an estimated cost labelled as such, and the limit holds on it.
- [ ] A served request under Bob's device writes files under `people/<id>/files/` and notes under his notes root, never the launch folder. `run_command` is not offered to him under `write`. After `--allow tools:shell` it is, with the plain-words warning printed.
- [ ] `chat`, `complete`, `execute` and `task run` against the metered mock behave byte for byte as before, and write no `usage/` file. The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user runs `apogee-dev serve --bind 0.0.0.0 --allow-remote --devices-only` with a household entry on the dev channel's own key, grants a test member a small daily limit, and watches the limit refuse from a second machine while a local model still answers.

**Scope note.** Earmarked for v0.2.7 (track 63); gated on 63a. Darwin 10c gates on it and 63a. Out of scope:
- A member's own key, per-backend limits, and per-person host lists (later calls).
- Quotas, rate limits or fairness on local models (59c's lanes and 63e's queue).
- Billing, invoices, payment, and any vendor-side spend control.
- A price list carried in the binary.
- Personal data on disk, recall and a member's own collections (63b).
- Who answers a question, and where an "always" is kept (63d).
- The task queue (63e).
- The people screens (darwin 10c).
- Any change to standalone use.
