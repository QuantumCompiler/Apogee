# CLI-62b · Reload a running server, and show what it serves against what is on disk

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.4 |
| **Component** | `serve` (reload, the admin plane's served-against-disk reads, the config verbs' notice) |
| **Labels** | `serve` `admin-plane` `config` `live-reload` |
| **Epic** | Track 62 · the server console |
| **Blocked by** | [62a](serve-status-and-runtime-record.md) |
| **Blocks** | — |
| **Reporter** | The admin-mode and people spike (2026-11-14; its `cli-admin-surface` probe, wall A1), with the user's calls |
| **Created** | 2026-11-14 |

---

## Summary

No config verb reaches a running `serve` today, and the admin plane reports the file, not what the server enforces. This item makes a config change **apply on the spot** (the default the user took), has the server **say what still needs a restart**, and gives the owner one honest read: what the server is actually serving, against what the file says.

## Description

### User story

**As** the owner of a Mac that serves Apogee, administering it from the "Apogee Server" window or the CLI,
**I want** every config change to reach the running server at once, with anything it cannot apply named along with why,
**so that** what the admin screens show is what the server is actually enforcing, and I am never left guessing whether a change took.

### Background

The user's call, 2026-11-14: the Mac that serves gets an "Apogee Server" window that administers its server through the local CLI over pipes ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces, item 4, *Revised again 2026-11-14*; [GUI SPEC](../../../assistant/gui/SPEC.md) → Core surfaces, item 3). Every change that window makes is a config verb, and today no config verb reaches a running server. This item makes a change **apply on the spot**, the default the user took, and has the server **say what still needs a restart**. It also gives the owner one honest read: what the server is actually serving, against what the file says.

This item delivers **reload**. A running `serve` rebuilds what it serves from the file: the served backends and role pointers, the permission checker, the allowed hosts, the tool registry and the MCP servers. The swap is all or nothing, so a config that would not start the server is refused and the old set keeps serving. Each reload reports, field by field, what it applied and what still needs a restart, and why. There are three triggers:

- A config change made by any `apogee` process applies on the spot. At the end of the command that wrote it, the CLI finds the running server through [62a](serve-status-and-runtime-record.md)'s runtime record and reloads it, and an admin-plane write reloads its own server in-process.
- `apogee serve reload [--output-format json]` (and `POST /v1/admin/reload`) applies a hand edit.
- SIGHUP reloads a serve that has no terminal.

There is also **a served-against-disk read**. `GET /v1/admin/served`, and the `served` and `drift` members this item adds to 62a's `serve status --output-format json`, return the effective snapshot and a per-field diff against the file. That diff replaces `config_drifted`'s backends-only view everywhere `restart_required` is computed.

In this document "role" has one sense only: `models.*`'s role pointers (model roles). People and access are track 63's, and a reload here is per server, with nothing in it per person.

### Evidence

**A1 — a running server never sees a config change, and the admin plane reports the file, not what it enforces.** Re-measured 2026-11-14 against the branch binary (`apogee 0.2.0 (b37b0a9f)`; `git diff b37b0a9f HEAD -- lib/src/cli/source` is empty at HEAD `4acf4a2b`). The probe used a scratch `APOGEE_HOME`, the `mock` backend and `serve --bind 127.0.0.1 --port 0 --tools -v`, and the server was stopped afterwards.

1. **The server keeps what it started with.** While it ran, the CLI made four changes: `config set-permission run_command allow`, `config add-allowed-host docs.python.org`, `config add-backend mock2 --type mock --model mock-2` and `config set-default mock2`. Afterwards `GET /v1/models` still listed only `mock-1`, and a chat naming `mock2` got `400` `no backend named 'mock2' (serving: mock)`. The spike's own probe found the same: of 4 CLI config changes made while serve ran, 0 reached the inference plane. The cause is in the source. `serve` reads the config once (`presentation/cli/serve_cmd.cpp:248`). From that read it builds the providers (`:288`), the served set (`:317`), the tool registry with its MCP connections (`:358-404`) and the permission checker (`:375`). Retrieval "is fixed for the server's lifetime" (`:347`). The reference says it outright: "The server does not hot-reload" ([http-api.md](../../../reference/cli/http-api.md):327), and MCP "servers connect at startup" (:500). The admin plane's own writes say the same in comments (`presentation/httpserver/admin_config.cpp:723-724` for levels, `:764-765` for config values, `:799` for allowed hosts).
2. **The admin plane reports the file and does not say it differs.** After those changes, `GET /v1/admin/permissions` answered `{"level":"allow","tool":"run_command"}`, and its only other top-level key is `object`. `GET /v1/admin/allowed-hosts` listed `docs.python.org`. Neither read carries any drift (`admin_config.cpp:687`, `:771`), while the served checker still asks and therefore denies. `GET /v1/admin/backends` did say `restart_required: true`, but only because a backend name and the role pointers moved. `config_drifted` compares backend names, `models` and `suites` and nothing else (`admin_config.cpp:196-203`), so a permission, host, MCP or tools edit made on its own never marks drift. The one statement of what the server enforces is the start summary (57n, `serve_cmd.cpp:459`). It said `runs unasked: none`, which is still true of the server and is now false of the file, and nothing sets the two side by side.
3. **Nobody is told.** `config set-permission` printed `permissions.run_command = allow` and nothing about the running server. There is no reload verb (`serve --help`), and `POST /v1/admin/reload` and `GET /v1/admin/served` both answer `404`. A restart is the only way to apply a change, and it drops the server's in-memory sessions (their transcripts stay on disk, `presentation/httpserver/session.h:30-33`).
4. **SIGHUP stops the server today.** `kill -HUP` on the probe's server ended it with status 129, the default action. No `stopped` line reached stderr or the operational log, which held only the start line. `StopOnSignals` installs SIGINT and SIGTERM only (`presentation/httpserver/serve.cpp:119-128`).
5. **What already works.** The spike also verified it. Config edits from any process interleave safely under the advisory lock (`data/contracts/config_edit.h:500-527`). The admin plane reads the file fresh on every request (http-api.md:324). 59a's device registry and 59f's scopes are already planned to apply at the next request ([paired-devices.md](../v0.2.3/paired-devices.md), [paired-apps-with-scopes.md](../v0.2.3/paired-apps-with-scopes.md)). Config is the one store that is not live.

## Acceptance criteria

- [ ] **Given** a sandbox home with the mock backend and `serve --bind 127.0.0.1 --port 0 --tools` running, **when** `apogee config add-backend mock2 --type mock --model mock-2` is followed by `config set-default mock2`, **then** the next `GET /v1/models` lists `mock-2` as the default, a chat naming `mock2` answers `200`, and each command's stderr ends with one line saying the running server applied it.
- [ ] **Given** a running serve, **when** `apogee config set-permission run_command allow` runs, **then** the served checker changes without a restart: `GET /v1/admin/served` shows `run_command: allow`, and the reprinted start summary says `runs unasked: run_command`.
- [ ] **Given** a hand edit of `config.json`, **when** `apogee serve status --output-format json` is read, **then** the edit shows in its `drift`; **and when** `apogee serve reload --output-format json` then runs, **then** it reports the edit under `applied`, and `drift` is empty afterwards.
- [ ] **Given** a hand edit that breaks the file, **when** `serve reload` runs, **then** it exits non-zero with the parse error; `POST /v1/admin/reload` answers `409`, and the server keeps answering on its old set.
- [ ] **Given** a serve started with `-m mock`, **when** `config set-default mock2` runs, **then** `models.default` is reported as `held_by_flag` (`-m`) and the server keeps serving `mock`.
- [ ] **Given** an edit not yet applied, **when** `GET /v1/admin/permissions` and `/allowed-hosts` are read, **then** they carry `drift`; **and given** a write over the admin plane in which everything applied, **then** it answers `applied` with `restart_required: false`.
- [ ] **Given** macOS or Linux, **when** `kill -HUP` is sent, **then** a serve whose stderr is a file reloads, and one on a terminal stops.
- [ ] **Given** no server running, **when** any config verb runs, **then** its output is byte-identical to today's.
- [ ] The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user runs `apogee-dev serve` and, from a second terminal, changes a permission and a backend with `apogee-dev config`, sees the notice, and sees the change in `apogee-dev serve status`.

## Constraints

- **Remote mode is an add-on (the anti-goal).** Nothing here may make standalone use need a server, a network, a runtime record or an account. With no server running, every config verb prints exactly what it prints today, on stdout and stderr, with no delay and no connection attempted. A stale runtime record (a dead pid, or a record for another config) is ignored silently. `chat`, `complete`, `execute`, `task`, `acp`, `mcp serve` and the Mac app over pipes never read the record to do their own work.
- **One config mutation path** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → One config mutation path). Reload only reads (`load_config`). It never writes the config, and a refused reload never "repairs" the file. The hook that notices a write sits after `edit_config_file` has landed, and the write itself is unchanged.
- **A reload serves exactly what a restart would.** Startup and reload share one builder, so a reloaded server is, field for field, the server a fresh `serve` with the same flags would start over that file. The flags given at startup keep winning over the file (`-m`, `--all-backends`, `--rag`, `--retriever`, `--rerank`, `--tools`, `--session-ttl`, `--ignore-timeout`). Where a flag holds a field, the diff says so instead of claiming the file applies.
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). The diff, the reload report, the event and the log line name a field that holds a secret (a backend's `api_key`, a header, an MCP server's `env`) as `changed` and never carry its value. A literal `api_key` stays refused from a non-loopback peer (`admin_config.cpp:326`). The CLI reaches its own server on loopback with the 0600 admin token from beside the config, header-only. The token never goes into a path, a query, a log line or a document.
- **Serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). Only `serve` listens. A config verb reaching its own server is an outbound loopback connection made by an admin act, never a front end using a port: the Mac app still drives the CLI over pipes, and nothing in `httpserver/` becomes load-bearing for `chat` or `complete`. `cli.no_listen_symbols` stays green unchanged.
- **Older versions keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The routes, the verb, the document members and the event are additive. Three recorded behaviors change, each as a dated revision in [http-api.md](../../../reference/cli/http-api.md) and a release-notes line:
  - "The server does not hot-reload" (:327).
  - `restart_required` now means "the file differs from what is served, in something only a restart applies". The field keeps its name and its type.
  - SIGHUP reloads a serve with no terminal instead of ending it. On a terminal it still stops the server, as today.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md); [ADR 0012](../../../adrs/cli/doors-are-reachable.md)). `serve reload` and its REST twin `POST /v1/admin/reload` return the same document. `GET /v1/admin/served` and `serve status`'s `served`/`drift` are one computation. The shell's classification table (`cli/tui_parity.cpp`) gains `serve reload`.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). `httpserver` does not link `cli` or `backends` (`cmake/modules.cmake:72-74`). So the builder stays in `cli`, and `httpserver` holds the snapshot and the routes and calls the builder through a function it is handed. The pure per-field diff sits beside `Config` in Data.
- **A signal handler only sets a flag** (`presentation/httpserver/shutdown.h:3-9`, the stop path's rule). SIGHUP sets an atomic, and a waiting thread does the reload.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Any live pass runs `apogee-dev serve` over `~/.apogee-dev`. It never touches the release `apogee` or `~/.apogee`, and never loads a launchd job (62d owns that).

## Technical approach

Paths under `lib/src/cli/` (`source/…`, `tests/…`).

| File | Change |
|---|---|
| `source/data/contracts/config_diff.h/.cpp` (new, pure) | `diff_configs(const Config& served, const Config& file)` returns the changed fields as dotted paths (`backends.mock2`, `models.default`, `permissions.run_command`, `tools.allowed_hosts`, `mcp_servers.<name>`, `auto_rag`, …). Each carries both values with secret-bearing fields redacted. `BackendConfig`, `McpServerConfig` and the sections it compares gain defaulted `operator==` where they lack one. |
| `source/presentation/cli/serve_build.h/.cpp` (new) | The build that today lives inline in `serve_cmd.cpp`'s callback (`:248-404`), extracted unchanged into `build_served(config, flags, previous)`. It runs the startup validations (`validate_active_suite`, `-m` naming a backend, `valid_rerank`), builds the providers, the served set, the registry with MCP, the checker, the pinned toolsets and the rankers. Given the previous snapshot, it carries over the providers of backend entries whose config is equal (the same `shared_ptr`, so a loaded model stays loaded and 59c's lane keeps its queue) and the MCP connections of unchanged servers. Startup is `build_served(config, flags, nullptr)`. |
| `source/presentation/httpserver/served.h/.cpp` (new) | `ServedSnapshot` is the immutable set a request runs under: the `Harness`, the `HandlerOptions` fields, the registry, the MCP registry, the `Config` it was built from and the flags. `SnapshotHolder` serializes reloads (a second request waits and then reloads from the file as it is then) and swaps a `shared_ptr<const ServedSnapshot>` under a mutex. `classify(diff, flags)` assigns each field `applied`, `restart` (with why) or `held_by_flag` (naming the flag). |
| `source/presentation/httpserver/handler.h/.cpp` | `Handler` takes the holder in place of its `const Harness&`, `HandlerOptions` and registry pointer (`handler.h:116-165`). A request copies the current snapshot at its start and keeps it to its end. The permission checker and the allowed hosts are read through the holder at each tool call. |
| `source/presentation/httpserver/admin.h`, `admin_config.h/.cpp` | `AdminOptions.startup` (`admin.h:40`, `admin_config.h:35`) becomes the holder's current `Config`. `config_drifted` (`admin_config.cpp:196-203`) gives way to `diff_configs` plus `classify`. Every write that today answers `restart_required` reloads its own server in-process first. Its response then carries `restart_required` (true only when something it touched needs a restart) and an additive `applied` list. `GET /v1/admin/permissions`, `/allowed-hosts`, `/backends` and `/mcp-servers` keep reporting the file and gain an additive `drift` array for their own fields. |
| `source/presentation/httpserver/admin_served.h/.cpp` (new) | `POST /v1/admin/reload` returns the reload document, `200` when applied, `409` (`type: config_refused`) when the file would not start the server, and in that case nothing is changed. `GET /v1/admin/served` returns `{served: {…effective snapshot…}, matches_file, drift: […]}`. Both are rows in `mux.cpp` behind the admin gate. |
| `source/presentation/httpserver/serve.h/.cpp` | On POSIX, `StopOnSignals` also handles SIGHUP when stderr is not a terminal. The handler sets an atomic, and a reload thread started beside the listener waits on it. Where stderr is a terminal, SIGHUP keeps its default (the terminal went away, so the server stops). Windows has no SIGHUP: the route and the verb are its trigger. |
| `source/presentation/cli/serve_cmd.cpp` | The callback calls `build_served` and hands the holder a `reload` function. After every reload that applied something, it prints the start summary again (57n: what the operator is told is what a client can make happen) and one line of what changed. It writes the result into 62a's runtime record (`snapshot` hash, `last_reload`: when, trigger, applied, needs restart, refused and why). `serve reload [--output-format json]` is a subcommand beside 62a's `serve status`. It reads the record, posts to the loopback route with the admin token, and falls back to SIGHUP plus the record's `last_reload` on POSIX when the bind does not answer on loopback. |
| `source/presentation/cli/served_notice.h/.cpp` (new) and `root.cpp` (`RootCommand::run`, `:289`) | The one seam for "applied on the spot". The config file's content hash is taken before a command and compared after it. When it changed and 62a's record names a live server over this config, the CLI reloads it once and prints one line to stderr: `the running server applied this` (with what needs a restart, if anything), or `the running server has not applied this: <why> -- apogee serve reload` (refused, unreachable or timed out). The shell's exec line and the machine mode's end of turn (a permission answered "always" writes the config) call the same seam. Stdout and every `--output-format json` document are untouched. |
| `source/presentation/cli/tui_parity.cpp` and completion | `serve reload` classified and completed ([ADR 0007](../../../adrs/cli/tab-completion.md)). |

**Events.** `server.reloaded` on `/v1/admin/events` (`{applied, needs_restart, refused}`, field names only). An operational-log line through `logger::log`. [62f](audit-log.md)'s audit record when it has landed.

**Reference.**

- In-house precedents:
  - The admin plane's fresh read per request and its `restart_required` (CLI MILESTONES → Milestone U), which this item widens rather than replaces.
  - 59a's registry read fresh by mtime ([paired-devices.md](../v0.2.3/paired-devices.md)) and 59f's scope change at the next request ([paired-apps-with-scopes.md](../v0.2.3/paired-apps-with-scopes.md)): the "next request" rule config now joins.
  - The stop path's signal-safe flag (`shutdown.h`).
  - 57n's start summary, the operator's statement of what a client can make happen, which a reload reprints.
  - The config's advisory lock (`config_edit.h:500-527`), which keeps a reload from reading a half-written file.
- Prior art: SIGHUP as "re-read your configuration" for a daemon without a terminal (nginx, sshd, the BSD `syslogd` convention), and `systemctl reload` / `launchctl kickstart` as the service manager's twin ([62d](serve-as-a-background-service.md)'s). Apogee diverges in two ways. A reload reports, field by field, what applied and what did not, instead of logging and hoping. A terminal's hangup still stops a foreground serve, so closing a window never leaves an orphan holding the port.
- Siblings: listed under Links.

**Docs when it ships:** [http-api.md](../../../reference/cli/http-api.md) gets the two routes, the event, `drift` and `applied`, and the dated revisions of :324-329, :500 and the per-route `restart_required` lines. The commands reference gets `serve reload`. CLI CLAUDE.md's serving sub-rules get "a reload serves exactly what a restart would, through one builder".

## Testing

All hermetic: a temp `APOGEE_HOME`, the mock backend, handlers driven through `Mux::dispatch` with no port wherever a port is not the point.

- **`tests/data/contracts/config_diff_test.cpp`** — each field family is detected, unchanged configs give an empty diff, and secret-bearing fields are redacted.
- **`tests/presentation/cli/serve_build_test.cpp`** — reload equals a fresh start over the same file and flags (the served set, the levels, the hosts, the tool names), an unchanged backend's provider is carried over by identity, a flag-held field is reported `held_by_flag`, and an invalid file is refused with the old snapshot kept.
- **`tests/presentation/httpserver/admin_served_test.cpp`** — the reload document and the drift read, `409` with nothing changed, a write's `applied` and widened `restart_required`, and the `drift` arrays on the four reads.
- **`tests/presentation/httpserver/handler_test.cpp`** — a turn begun before a reload finishes on its snapshot. A permission tightened mid-turn denies at the next tool call. A session whose backend left the served set keeps its history and gets the `400` naming what is served.
- **`tests/presentation/cli/served_notice_test.cpp`** — no record means byte-identical output, a dead pid means silence, a live server means one line, and a refusal names `apogee serve reload`.
- **Existing suites extended** — `admin_config_test.cpp`, `serve_cmd_test.cpp`, `tui_parity_test`, `leak_test.cpp` and `parity_test.cpp`.
- **`cli.serve_reload`** (`tests/scripts/sh/serve_reload.sh`), a shell case — the spike's probe kept as the regression fixture. It runs the real binary under `serve --bind 127.0.0.1 --port 0 --tools` in a temp home with the mock backend and makes the probe's four changes. After each change it asserts the stderr notice, and that `/v1/models`, a chat naming `mock2` and `GET /v1/admin/served` show the change applied with an empty drift. Next it makes a hand edit of `config.json`, checks that the drift shows it and that `serve reload --output-format json` applies it, and then checks that a broken file is refused with the old set still answering. On POSIX it then runs serve with stderr redirected, makes a hand edit followed by `kill -HUP`, and checks that the server is alive and applied it. A second serve on a pseudo-terminal must still stop on SIGHUP. On Windows the SIGHUP phase is skipped, and the run says so.
- **Reload equals restart** — a test compares the snapshot view of a reloaded server with that of a fresh start over the same file and flags, for every field family.
- **The anti-goal** — with no runtime record, then with a dead pid's record, then with a record for another config, `config set-permission` gives byte-identical stdout and stderr to today and opens no connection. `chat` and `complete` never read the record.
- **`leak_test.cpp`** — gains a backend with a literal `api_key` changed across a reload. The key never appears in the reload document, the drift, the event, the log line or the stderr notice.
- **`cli.http_api_conformance`** — covers both routes and the additive members. `cli.no_listen_symbols` and `cli.install_parity` stay green: no new seeded file.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Is a reload all or nothing? | `[default]` | **All or nothing.** A reload builds the whole new snapshot before swapping. Any startup refusal (a parse error, a suite member naming nothing, `-m` naming a removed backend, no servable backend) is a `409 config_refused` naming why. The old snapshot keeps serving, and the runtime record's `last_reload` keeps the refusal. |
| What applies on reload, and what needs a restart? | `[default]` | Applied: backend entries added, changed or removed, with the served set re-derived exactly as at startup (the default alone unless `--all-backends` or `-m`, never a vendor-CLI backend); `models.*` role pointers, `suites` and `default_suite`; permission levels; `tools.allowed_hosts` and the other `tools.*` fields the registry is built from; MCP servers added, changed or removed; `auto_rag` where no `--rag` was given; a backend's `idle_unload_seconds` unless `--ignore-timeout`. Needs a restart: the listener (bind, port, `--allow-remote`), 59a's TLS identity and `--devices-only`, the admin token file, and every flag given at startup (reported `held_by_flag`, naming the flag). 62c's `serve:` fields join this table when 62c lands (consumed decision below). |
| What happens to in-flight work? | `[default]` | A request or turn keeps the snapshot it started under, including its backend, registry and MCP connections. A removed MCP server or backend is closed when its last turn ends. Permission levels and allowed hosts are read at each tool call, so a tightened level or a removed host applies mid-turn and a loosened one at the next call. A live session keeps its history. If its backend left the served set, its next turn gets today's `400` naming what is served. |
| What carries over a reload? | `[default]` | **Carry over what did not change.** A backend entry equal in every field keeps its provider, so a loaded local model stays loaded and its 59c lane keeps its queue. An MCP server equal in every field keeps its connection. Rankers are rebuilt only when their registry changed. |
| What triggers a reload: the write, or a file watcher? | `[default]` | **The trigger is the write, not a file watcher.** The command that wrote the config reloads the server once, at its end. A hand edit is applied by `serve reload` or SIGHUP. Serve does not watch the file: an editor's half-saved file would be applied or refused at random, and a multi-step command would be applied piece by piece. |
| When does SIGHUP reload? | `[default]` | **SIGHUP reloads only a serve whose stderr is not a terminal**: a background service, `nohup`, or a redirected log. On a terminal it keeps today's stop, because a hangup there means the terminal closed, and a reload would leave an orphan holding the port. Windows has no SIGHUP, and the verb and route are its trigger. |
| How does the CLI reach its server? | `[default]` | 62a's record gives the pid, the address and the config it serves. The verb posts to `POST /v1/admin/reload` on loopback with the admin token, with a 2-second connect bound and a 30-second reload bound. Where the bind does not answer on loopback (a single non-loopback address), POSIX falls back to SIGHUP and reads `last_reload` from the record. Otherwise it says the server could not be reached and names a restart. |
| Whose are the record's shape and the loopback client? | `(consumed decision)` | [62a](serve-status-and-runtime-record.md)'s. |
| What is the reload document? | `[default]` | `{reloaded: bool, trigger, applied: [{field, from, to}], needs_restart: [{field, served, on_disk, why}], held_by_flag: [{field, flag}], refused: null \| {why}}`, values redacted where they hold a secret. `serve reload` prints it as text: one line per field, then "nothing else differs" or the restart line. `--output-format json` prints the document byte for byte as the route returns it. |
| What is the drift read? | `[default]` | `GET /v1/admin/served` and `serve status`'s additive `served` and `drift` members. `served` holds the backends with their models and kinds, the default and the model roles, the permission levels, the allowed hosts, the MCP servers with their connect state, the tools count and the flags. `drift` is the same per-field list as the reload document's `needs_restart` plus any field the file changed since the last reload. Each admin read keeps reporting the file, its document unchanged, and gains a `drift` array for its own fields. |
| What happens when reloads overlap? | `[default]` | **Reloads are serialized and coalesced.** A reload requested while one runs waits, then reloads once from the file as it is then. |
| What does serve say after a reload? | `[default]` | After a reload that applied anything, `serve` prints the start summary again (57n) and one changed-fields line to stderr. The operational log gets one line, and `/v1/admin/events` gets `server.reloaded` with field names only. |
| Which of 62c's `serve:` fields apply on reload, and which need a restart? | `(consumed decision)` | [62c](serve-section-in-config.md)'s to classify in this item's table. Whichever of the two lands second adds them. |
| How is the server restarted for what a reload cannot apply? | `(consumed decision)` | [62d](serve-as-a-background-service.md)'s (`serve service restart`). Until it lands, "needs a restart" names stopping and starting `serve`. |
| Who may call `POST /v1/admin/reload` from off the machine? | `(consumed decision)` | [62g](administering-local-and-marked-devices.md)'s. The route is an admin route like any other. |
| What is the audit record of a reload (actor, trigger, outcome)? | `(consumed decision)` | [62f](audit-log.md)'s. |
| Can a reload widen a running task's grants or an app's scope? | `(consumed decision)` | A running task keeps the grants it started with ([59e](../v0.2.3/tasks-outlive-the-connection.md)). A reload never widens one. An app's scope is held "never past the config" ([59f](../v0.2.3/paired-apps-with-scopes.md)) against the served set as it is after the reload. |
| Does a config verb run through 59d's command door reload the server? | `(consumed decision)` | 59d's "the door does not reload anything" ([`reads-and-commands-over-the-network.md`](../v0.2.3/reads-and-commands-over-the-network.md), Open questions, "How is `restart_required` computed?": as the admin plane computes it; the door does not reload anything) stays true of the door. A config verb run through it reloads through the verb, as from a terminal, and whether a device may run one is 62g's. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Specced from the admin-mode and people spike (its `cli-admin-surface` probe; wall A1). | — |
| 2026-11-14 | Admin mode is a separate "Apogee Server" window in the macOS app, offered only on the Mac that serves, driving the local CLI over pipes. | the user |
| 2026-11-14 | Administering is local by default, and the owner may mark chosen paired devices as allowed to administer remotely ([62g](administering-local-and-marked-devices.md)). This revises 59d's default, where the admin plane keeps one token for any network caller; otherwise the admin plane stays local-only. | the user |
| 2026-11-14 | The window installs the server as a launchd background service and starts, stops and restarts it ([62d](serve-as-a-background-service.md)). | the user |
| 2026-11-14 | Both land before v0.2.5, both in v0.2.4: the server console (CLI track 62, darwin/macos track 10's 10a-10b) and people and access (CLI track 63, darwin/macos 10c). Nothing already queued is renumbered. | the user |
| 2026-11-14 | People: a household or a small team, designed for both. The words are **people** and **access** (Owner, Member), never "roles", which in Apogee means model roles. That is the only sense "role" has in this document (`models.*`'s role pointers). The owner sees members' metadata (devices, apps, activity, which model, when), never their chat contents, and whoever controls the server Mac can still read its disk. Members use the server's local models only, unless the owner grants a household cloud key with a limit. All of this is track 63's. A reload here is per server, and nothing in it is per person. | the user |
| 2026-11-14 | The other defaults the user took (track 62 and 63's, not this item's): an audit kept 90 days of who, what and when without request contents ([62f](audit-log.md)); collections as shared libraries handed out per person plus a member's own private ones; notes and saved decisions personal by default; members' apps paired by the owner at the server Mac, capped at that member's allowance; a per-person task queue, not a refusal. | the user |
| 2026-11-14 | The SPEC amendments are already written (CLI SPEC Core surfaces 4, *Revised again 2026-11-14*; GUI SPEC Core surfaces 3). | — |
| 2026-11-14 | Remote use stays an add-on: the CLI alone and the Mac app over pipes stay complete and need no server. | the user |
| 2026-11-14 | **Live changes are applied on the spot, and what still needs a restart is said** (the user took the default offered by the spike's question). So a config change reaches a running server without a separate step, from the CLI, the shell, the machine mode or the admin plane. `serve reload` stays for hand edits and scripts. A refused or unreachable reload is said, never silent. | the user |
| 2026-11-14 | Second in track 62, gated on [62a](serve-status-and-runtime-record.md). Every trigger finds the server through 62a's runtime record, and the drift is reported inside 62a's `serve status` document. It sits ahead of the server window (darwin 10b) because an admin screen over today's reads would show policy the server is not applying, with nothing saying so. | the spec |
| 2026-11-14 | This revises three recorded behaviors under ADR 0006, each as a dated revision in http-api.md and a release-notes line: "The server does not hot-reload" (http-api.md:327), the meaning of `restart_required`, and SIGHUP for a serve with no terminal. | the spec |
| 2026-11-18 | Moved here from v0.2.6, when the releases were merged; the id is unchanged. Earmarked for v0.2.4 (track 62); gated on 62a. | the user |

## Out of scope

- Watching the config file.
- Making the listener, TLS or the admin token live: a restart remains their path.
- The `serve:` config section ([62c](serve-section-in-config.md)).
- The service and its restart verb ([62d](serve-as-a-background-service.md)).
- Who may administer remotely ([62g](administering-local-and-marked-devices.md)).
- The audit log's storage and reads ([62f](audit-log.md)).
- Connections and activity ([62e](connections-and-activity.md)).
- The server window's screens (darwin [10b](../../darwin/macos/v0.2.4/server-window.md)).
- Anything per person (track 63).

## Links

- **Blocked by:** [62a](serve-status-and-runtime-record.md), the runtime record and `serve status`, which this item is gated on, reports into and finds the server through · **Blocks:** —
- **Relates to (siblings):**
  - [62c](serve-section-in-config.md), the `serve:` config section, whose fields this item's classification takes in.
  - [62d](serve-as-a-background-service.md), the background service, whose restart verb is where "needs a restart" leads.
  - [62f](audit-log.md), the audit log, which records each reload.
  - [62g](administering-local-and-marked-devices.md), who may reach the admin routes, this one included.
  - [62h](local-admin-reads.md), the JSON policy reads over pipes.
  - [59c](../v0.2.3/many-sessions-at-once.md), the lanes a carried-over provider keeps.
  - [59d](../v0.2.3/reads-and-commands-over-the-network.md), the command door.
  - [59e](../v0.2.3/tasks-outlive-the-connection.md), whose tasks keep the grants they started with.
  - [59f](../v0.2.3/paired-apps-with-scopes.md), scopes never past the config.
  - darwin [10b](../../darwin/macos/v0.2.4/server-window.md), the server window, which shows the reload line and the drift.
- **Relates to:** [CLI SPEC](../../../assistant/cli/SPEC.md) (Core surfaces 4), [GUI SPEC](../../../assistant/gui/SPEC.md) (Core surfaces 3), [http-api.md](../../../reference/cli/http-api.md), CLI MILESTONES → Milestone U, 57n's start summary, [ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0006](../../../adrs/cli/backwards-compatibility.md), [ADR 0007](../../../adrs/cli/tab-completion.md), [ADR 0008](../../../adrs/cli/module-map.md), [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md), [ADR 0012](../../../adrs/cli/doors-are-reachable.md).
- **Revises:** 59d's default (one admin token for any network caller), through 62g; and three recorded behaviors in http-api.md (hot-reload, `restart_required`, SIGHUP).
