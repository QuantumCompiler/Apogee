# CLI-62f · The audit log: who changed, approved or used what

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.4 |
| **Component** | `audit` (a new Data module) · `apogee audit list` · `GET /v1/admin/audit` |
| **Labels** | `audit` `server-console` `privacy` `admin-plane` |
| **Epic** | Track 62 · the server console |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The admin-mode and people spike (2026-11-14), wall A4 |
| **Created** | 2026-11-14 |

---

## Summary

A config change, an admin write, a refused request and a served model call leave no record of who made them. This item delivers **the audit log**: an append-only `0600` record, one JSON line per act, of the time, the actor, the door, the action, the target and the outcome, refusals included, never request contents or secrets. It is kept 90 days in daily files and read with `apogee audit list` and its admin twin, `GET /v1/admin/audit`.

## Description

### User story

**As** the owner of an Apogee server shared by paired devices, apps and members,
**I want** a record of who changed, approved or used what, and when, without request contents,
**so that** I can answer "who changed this permission" or "which app used which backend when", and access rules have something to hold anyone to.

### Background

The user's calls, 2026-11-14, from the admin-mode and people spike: the server is administered on its own Mac, and "its activity and an audit trail are visible to the owner" ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4, "Revised again 2026-11-14"). The GUI SPEC's server window shows "activity and the audit trail" ([GUI SPEC](../../../assistant/gui/SPEC.md) → Core surfaces 3). The user set the audit's terms that day: **kept 90 days, a record of who, what and when, without request contents**.

Today there is nothing to show. A config change, an admin write, a refused request and a served model call leave no record of who made them. With one person that is a missing convenience. Once paired devices, apps and members share one server (tracks 59 and 63), an owner cannot answer "who changed this permission" or "which app used which backend when", and access rules have nothing to hold anyone to.

**The shape of the audit log.** It is an append-only record, `0600` in a private directory, one JSON line per act. Each line holds the time, the **actor**, the **door** and **action**, the **target** and the **outcome**, refusals included. It never holds request contents or a secret. The writers are:

- every config change, caught at the one config mutation path, so all of its callers are covered;
- the `auth` verbs and the admin credential routes (the provider type, never the key);
- the device and app registry (pairing, exchange, scope change, revoke);
- every admin-plane write and every request the admin gate refuses;
- every request on the inference plane and the gated planes (59a's harness plane, 59d's command door, 59e's served tasks);
- the permission gate's decisions on served and gated requests.

The actor is one of:

- the **OS user** over a terminal or pipes;
- the **admin token**, with the peer address;
- a **device** or an **app** by name;
- **anonymous**, with the peer address.

When [63a](people-on-one-server.md) lands, each line also gets the **person**. The log keeps **90 days** by default (`audit.keep_days`), in daily files pruned as the days roll over. It is read with `apogee audit list [filters] --output-format json` and its admin twin, `GET /v1/admin/audit`.

### Evidence

**A4 — there is no audit trail.** Re-measured 2026-11-14 on HEAD (`4acf4a2b`) and the branch binary (`apogee 0.2.0 (b37b0a9f)`) in a scratch `APOGEE_HOME` under the scratchpad, with the mock backend and `serve --bind 127.0.0.1 --port 0 -v` killed after:

1. **Nothing records an edit, a refusal or a caller.** Seven `apogee config` edits (`add-backend` ×2, `set-default`, `set-permission write_file allow`, `add-allowed-host`, `set memory.recall false`, `delete-backend`) left `logs/` empty: no file at all. Under `serve`, two chats (one refused `400`, model `nope`), two admin requests refused `401` (no token, a one-byte-off token), one admin read and two admin writes (`PUT /v1/admin/permissions/run_command` `{"level":"deny"}` → `200`, `DELETE /v1/admin/allowed-hosts/example.com` → `200`) left **three** log lines: the start line, `chat mock 0ms`, and `stopped`. None names an edit, a refusal, a caller or an address. The spike's run measured the same thing: 7 edits and 4 chats (one refused) left 6 lines.
2. **The operational log answers a different question, by design.** It records "what the program did", one file per day, "no levels beyond a tag" (`data/logger/operational.h:8-19`). A line is a timestamp, a level, a component and a message (`data/logger/operational.cpp:59-73`), appended through a plain `std::ofstream` (`:100`), so the file is `-rw-r--r--` in a `drwxr-xr-x` directory (probe; the `logs` layout row is not private, `data/contracts/layout.cpp:25`). Nothing ever prunes it: no retention exists for `logs/` (grep). `logger::log` is called from 11 files, none of them a config verb, an auth verb or an admin route (grep). The served turn's line names the backend, the session and the tools, never who asked (`presentation/httpserver/handler.cpp:582-586`).
3. **The request log is stderr only and names no caller.** `serve -v` prints `METHOD path -> status` (`presentation/httpserver/serve.cpp:179-185`; wired only under `-v`, `presentation/cli/serve_cmd.cpp:451-455`). It is never persisted, and it carries no peer address or credential. The probe's stderr showed all ten requests, the `401`s included, and they were gone when the process ended.
4. **The pieces to name a caller exist but are not used for it.** The socket's own peer address is on every request (`HttpRequest::remote_address`, `presentation/httpserver/http_types.h:31`, filled at `serve.cpp:58`), and two routes already judge it (`admin_auth_routes.cpp:72`, `admin_config.cpp:326`). The admin gate runs before routing (`presentation/httpserver/mux.cpp:631-641`). Locally there is no OS-user lookup at all: `getuid`, `getlogin`, `getpwuid`, `USER` and `USERNAME` appear nowhere in `lib/src/cli/source` (grep). The one `geteuid` call only compares a file's owner with the process (`infrastructure/platform/platform.cpp:682`); it never names anyone. A task's ledger records authority only as the anonymous words `config`, `grant`, `person`, `nobody` and `declared` (`business/tasks/task.h:150-161`).
5. **"Always" rewrites the shared policy without a trace.** An `always` answer writes `permissions.<tool>: allow`, or adds the host to `tools.allowed_hosts`, through the config editor (`presentation/cli/permissions.cpp:79-97`), and `serve` then honors it for every client (`serve_cmd.cpp:373-375`). Neither the answer nor who gave it is recorded.
6. **The queued server items add no audit.** 59a and 59f record a device's or an app's `last_seen` only, kept in memory and written at most once a minute ([`paired-devices.md`](../v0.2.3/paired-devices.md) (Open questions, the device token's default: `last_seen` is kept in memory and written at most once a minute per device)). A grep of `backlog/cli/v0.2.3` finds no audit design.

## Acceptance criteria

- [ ] **Given** a sandbox home, **when** I run `apogee config set-permission write_file allow` and then `apogee audit list --output-format json`, **then** it shows one entry: door `cli`, action `config set-permission`, target `permissions.write_file`, detail `{"level":"allow"}`, outcome `ok`, actor `local` with the OS user and `via`.
- [ ] **Given** `serve` on loopback, **when** `PUT /v1/admin/permissions/run_command` is sent with the admin token, **then** it is recorded with actor `admin` and peer `127.0.0.1`.
- [ ] **Given** `serve` on loopback, **when** a tokenless admin request arrives, **then** it is recorded as `refused`.
- [ ] **Given** `serve` on loopback, **when** a chat is served, **then** it is recorded with its backend, session id and status, and no entry holds the prompt, a key or a token.
- [ ] **Given** an audit log, **when** `GET /v1/admin/audit?outcome=refused` is called, **then** it answers the same document as `apogee audit list --outcome refused --output-format json`.
- [ ] **Given** day files older than `audit.keep_days` (90 by default), **when** the first write of a new day lands, **then** they are gone, and the prune is itself an entry.
- [ ] **Given** an audit log, **when** `check` runs, **then** it shows the Audit row private, with its oldest and newest day.
- [ ] **Given** 59a and 59f have shipped, **when** a device pairs, an app's scope changes or a credential is revoked, **then** each is recorded by name, and a request with a device or app token carries that actor.
- [ ] **Given** 59a and 59f have shipped, **when** a command runs through 59d's door, **then** it is recorded with its verb words and the device as actor, never its arguments.
- [ ] **Given** the audit directory is unwritable, **when** `chat` and `complete` run, **then** they work.
- [ ] The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user runs `apogee-dev serve` on the dev channel, changes a permission with `apogee-dev config`, and reads both acts back with `apogee-dev audit list`.

## Constraints

- **Remote use is an add-on (the anti-goal).** Nothing here makes standalone use need a server, a network, a pairing or an account. The audit is a file the CLI writes, over pipes as on a server, and a write failure never fails the act it records (Open questions). `chat`, `complete`, `execute`, `task`, `acp`, `mcp serve` and the Mac app over pipes behave as today, with the audit directory absent, unwritable or full.
- **Nothing in the audit is content or a secret** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). An entry is built from a type that has no field for a prompt, a message, a tool argument, a command's arguments, a file path, a config value outside an allow-list, a key, a token, a pairing code or a query string. The `AuditEntry` view is the only thing serialized, and it carries names: a backend, a tool, a key path, a provider type, a device, an app, a session id. A refusal is recorded by its error **type** (`authentication_error`, `scope_refused`, `permission_denied`), never its message, which can echo the request. The owner sees members' metadata and never their chat contents (the user's call). The honest limit is said in the reference: whoever controls the server Mac's account can still read its disk.
- **One config mutation path** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → One config mutation path). A config change is recorded where every config change already passes: `edit_config_file` (`data/contracts/config_edit.h:524`), which 51 call sites in 25 files reach (`git grep` at HEAD, comments excluded). Recording it at each caller instead would be a second path to keep in step. The recorder never writes the config, and changing `audit.keep_days` is itself a config edit, so it is recorded too.
- **Older versions keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The new layout row, config key, verb, route, schema entry and fields are all additive, and an entry's shape only grows (63a's `person` is the first addition). No existing log line, route, body or document changes.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). `audit list` answers from the CLI (`--output-format json`), on the admin plane (`GET /v1/admin/audit`, the same filters and the same document), and in the shell's classification table. For 59d's command door it is classed in the admin family, never a member's or an app's.
- **Never listens; serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). Recording opens no socket. The HTTP recording sits in the socket-free mux and handlers, and `httpserver/serve.cpp` stays the one object that may reference `listen`/`accept`.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). The writer and reader are a Data module, `audit`, linking `contracts` and `platform` the way `logger` does. `contracts` does not link it. Instead the config editor exposes an observer, which the composition roots (the CLI root and `serve`) point at the recorder, so the dependency still points down. The OS-user lookup and the private append are `platform` helpers.
- **UTF-8 where it enters** (CLI CLAUDE.md). Names that reach an entry from the wire (a device's offered name, a route's `{id}`, a backend named in a request) are mended and bounded (256 bytes per field) before they are written.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Probes and tests use a temporary `APOGEE_HOME` under the scratchpad and the mock backend. A live pass runs `apogee-dev` over `~/.apogee-dev`, and never touches the release `apogee` or `~/.apogee`.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/infrastructure/platform/platform.h/.cpp` | `current_user()`: the OS account name and id (`getpwuid_r(geteuid())` on POSIX, `GetUserNameW` on Windows), or `unknown` when the lookup fails. `append_private(path, bytes, error)`: `O_WRONLY\|O_APPEND\|O_CREAT\|O_CLOEXEC` at `0600`, one `write` per line, the sibling of `write_new_file` (`platform.h:239-240`); on Windows it opens with `FILE_APPEND_DATA` and modes are skipped |
| `source/data/contracts/layout.cpp` | a 21st row, `{"audit", "who changed, approved or used what, one file per day, kept audit.keep_days", true, true}`, private and user data, so `reset` and `uninstall` ask before removing it; the row count in `kDirectories` (`layout.cpp:17`) and `audit_dir()` follow |
| `source/data/contracts/config.h` and the template | `audit.keep_days` (default 90, a whole number from 1 to 3650), settable with `config set` and listed in its help |
| `source/data/contracts/config_edit.h/.cpp` | `ConfigEditObserver` and `set_config_edit_observer(fn)`: called once by `edit_config_file` after an edit lands (with the target path and the text before and after), and once when an edit is refused or throws (with the reason's type); with no observer set (tests, and any library caller), nothing changes |
| `source/data/audit/audit.h/.cpp` (new module, Data) | `Actor`, `AuditEntry`, the scope's actor, `record`, `config_key_diff`, `read` and the failure report (below) |
| `cmake/modules.cmake` | `set(APOGEE_LINKS_audit contracts platform)` |
| `source/presentation/cli/` (the root and composition) | the root sets the process actor (`local`, from `current_user()`; `via` from whether stdin and stdout are a terminal, or `machine` under the machine mode) and installs the config observer; the command child's caller (below) |
| `source/presentation/cli/auth_cmd.cpp` and `presentation/httpserver/admin_auth_routes.cpp` | `auth add` and `auth clear` (and their routes) record the provider type around `CredentialStore::put` and `clear` (`data/secrets/store.h:59`, `:62`); `serve --print-admin-token` records `admin-token print`, never the token |
| `source/presentation/httpserver/mux.cpp` and `handler.cpp` | `dispatch` (`mux.cpp:625`) resolves the caller and the peer, opens a `ScopedActor`, and records after the route answers (below) |
| `presentation/cli/permissions.cpp`, and the served checker `make_permission_checker` | the permission gate's decisions on served and gated requests (below) |
| `data/secrets/devices.cpp` | 59a's and 59f's registry writes, when they have shipped (below) |
| `source/presentation/cli/audit_cmd.h/.cpp` | `apogee audit list` (below) |
| `source/presentation/httpserver/admin_audit.h/.cpp` | `GET /v1/admin/audit` with the same filters as query parameters, answering the same document; one more row in `kRoutes` (97 rows at HEAD, `mux.cpp:31`; the queued items before it add their own), and `cli.http_api_conformance` holds it to the reference |
| `source/presentation/cli/check.cpp` | an **Audit** row (the directory and files private, `keep_days`, the oldest and newest day kept, `skipped` on Windows), with `--fix` tightening the modes |
| `source/presentation/cli/tui_parity.cpp` | `audit list` classified |
| the machine schema (`apogee __machine-schema`) | a `$defs` entry for the `audit list` document `{entries:[…], keep_days, oldest, skipped, truncated}` |

**The `audit` module** (`source/data/audit/audit.h/.cpp`):

- `Actor`: `kind` (`local`, `admin`, `device`, `app`, `anonymous`, `unknown`), `name`, `device` (an app's), `user` and `via` (`terminal`, `pipe`, `machine`) for a local actor, `peer` for an HTTP one, and `task` when the act ran inside a task;
- `AuditEntry`: `v`, `time` (UTC, milliseconds), `actor`, `door` (`cli`, `admin`, `inference`, `harness`, `command`, `task`), `action` (the command's words without arguments, or the method and route pattern), `target` (names, list), `detail` (allow-listed values only), `outcome` (`ok`, `refused`, `error`), `status` (the HTTP code, over HTTP), `reason` (the error type) and `tokens` (when a backend reports usage);
- the scope's actor: `set_process_actor(Actor)` for a command, and a thread-local `ScopedActor` for a served request, read by every recording made beneath it;
- `record(AuditEntry) noexcept`: takes a lock beside the directory (`audit/.lock`, `platform::FileLock`), runs `append_private` into `audit/audit-YYYY-MM-DD.jsonl`, and on the first write of a new UTC day removes day files older than `keep_days`, recording that as `audit prune`;
- `config_key_diff(before, after)`: the dotted key paths that changed, three levels deep (`backends.<name>`, `permissions.<tool>`, `tools.allowed_hosts`). Values go into `detail` only for an allow-list: permission levels, allowed hosts, role and default pointers, and `audit.keep_days`;
- `read(Filter)`: newest first, a malformed line skipped and counted, never rewritten;
- a write failure is reported once per process on stderr and in the operational log, and never thrown.

**A command child's caller.** A command child that `serve` starts reads its caller from the `0600` file that 59f's `--scope-file` hands it ([`paired-apps-with-scopes.md`](../v0.2.3/paired-apps-with-scopes.md) (Technical approach, 59b's `machine_sessions` and 59d's `command_door`: the hidden `--scope-file PATH`, a `0600` file `serve` writes before each child starts)). That file gains the caller's kind and name, and a device's child gets one too, with no scope. With no file handed, the actor is the OS user.

**What the mux records.** `dispatch` (`mux.cpp:625`) resolves the caller (59a's credential when it has shipped, the admin token, or anonymous) and the peer, and opens a `ScopedActor`. The network caller is [62e](connections-and-activity.md)'s `CallerView {kind, name, address}` (`presentation/httpserver/caller.h`): whichever of the two items builds first lays it, the other reuses it, and the mux maps it onto the Data module's `Actor`, so `audit` never links the presentation layer. After the route answers, it records:

- every non-`GET` admin row, with its `{id}` as the target;
- every admin-gate refusal (`401`, `403`), as actor `unknown`;
- every inference request: the action, the backend served, the session id, the status and the usage;
- every harness-plane and command-door request: for a command, its classified verb words, never its arguments.

Admin reads (`GET`) are not recorded. The query string is never read into an entry.

**The permission gate** (`presentation/cli/permissions.cpp`, and the served checker `make_permission_checker`). On a served or gated request, every decision on a tool that writes, edits or reaches out records the tool, the decision and the authority (`config`, `grant`, `nobody`). It never records the target, the detail or a host: those are what the conversation reached for, contents by another name ([63d](consent-goes-to-its-person.md)'s rule for its answer entries). A host appears only when an `always` writes it into `tools.allowed_hosts`, because it is then shared policy in the config. At a terminal, only `always` is recorded, and it already arrives through the config observer with `via` set. `yes`, `session` and `no` at the person's own terminal are not recorded.

**The registry writes** (`data/secrets/devices.cpp`, 59a's and 59f's, when they have shipped): pairing minted, exchanged, refused (a wrong code burns an attempt), renamed, cancelled while pending ([62h](local-admin-reads.md)), scope changed and revoked, and a device marked or unmarked to administer (`devices admin`, [62g](administering-local-and-marked-devices.md)). Each records the name and kind (a rename the old and new names), never the code or the token.

**`apogee audit list`** (`source/presentation/cli/audit_cmd.h/.cpp`):

```
apogee audit list [--since WHEN] [--until WHEN] [--actor KIND[:NAME]] [--door DOOR] [--action PREFIX] [--target NAME] [--outcome ok|refused|error] [--limit N] [--output-format text|json]
```

`WHEN` is a date or a duration (`7d`, `12h`). The default limit is 200, newest first. Help and completion follow [ADR 0007](../../../adrs/cli/tab-completion.md).

**Reference.**

- In-house precedents:
  - The operational log (`data/logger/operational.h`): one file per day and never-throws, both kept. Its world-readable mode and its lack of retention are not.
  - The secrets rule's views (`backend_view`, `CredentialMetadata`): a type that cannot carry the secret.
  - `edit_config_file`'s lock and atomic write (`config_edit.h:478-537`): the one path the observer sits in.
  - The task ledger's authority words (`task.h:150-161`), reused as the permission decision's `authority`.
  - 59f's `--scope-file` hand-off to a child, extended with the caller.
  - The peer-address rule (`admin_auth_routes.cpp:72`): the socket decides, never a header, and the audit's `peer` comes from the same field.
- Siblings:
  - [62a](serve-status-and-runtime-record.md), what the running server is doing.
  - [62b](serve-reload-and-served-vs-disk.md), reload. A reload is recorded here as `serve reload`, with what changed and what still needs a restart named in `detail`.
  - [62e](connections-and-activity.md), the live view: who is connected now, with counts and the event tail. It keys activity by the same credential, and this item's `Actor` is meant to be that key. The audit is the durable record, and 62e is the live one.
  - [62g](administering-local-and-marked-devices.md): a marked device administers through 59d's command door, never the admin plane, so its acts are recorded with actor `device` and door `command` (the verb's words, never its stdin). The mark on and off, each refused `Administer` request and a non-local peer refused on the admin plane (by address) are entries too. Whichever of the two builds second adds the call sites.
  - [62h](local-admin-reads.md), what the local admin screen reads, of which `audit list` is one. Its pairing rename and cancel are audited acts, with the row's id and never a code.
  - [59a](../v0.2.3/paired-devices.md), [59d](../v0.2.3/reads-and-commands-over-the-network.md), [59e](../v0.2.3/tasks-outlive-the-connection.md), [59f](../v0.2.3/paired-apps-with-scopes.md): the credentials and gated doors the audit names.
  - [63a](people-on-one-server.md) adds `person`. [63c](allowances-keys-and-usage.md) owns the usage ledger and limits. [63d](consent-goes-to-its-person.md) records who answered.
  - Darwin [10a](../../darwin/macos/v0.2.4/design-the-server-window.md) and [10b](../../darwin/macos/v0.2.4/server-window.md), the server window that shows it. Its connection to the CLI is [GUI 7a](../../darwin/v0.2.4/app-connection-local-or-server.md)'s local half.
- Prior art:
  - Open WebUI's audit logging levels (`NONE`, `METADATA`, `REQUEST`, `REQUEST_RESPONSE`; its `utils/audit.py`). This item is fixed at the metadata level, by the user's call, with no level that records bodies. Its rotation is by size; this item rotates by day, because retention is in days.
  - Jellyfin's dashboard activity log, kept a set number of days (30 by default, community-reported). Here the user chose 90.
  - Home Assistant's and Plex's owner-facing activity views shaped the actor-first reading: who, then what.

**Docs when it ships:**

- [machine-mode.md](../../../reference/cli/machine-mode.md)'s reads list gains the document.
- [http-api.md](../../../reference/cli/http-api.md) gains the route and a paragraph on what is recorded and what never is, with the honest limit.
- The commands reference gains `audit`.
- CLI CLAUDE.md's secrets sub-rules gain "an audit entry is a view: names, never contents or secrets", and its config-path rule gains "the observer records, and never writes".

## Testing

Hermetic: a temp `APOGEE_HOME`, the mock backend, handlers driven through `Mux::dispatch` with no port wherever a port is not the point.

- **`tests/data/audit/audit_test.cpp`** — the line format, `0600` and `0700` after the first write, two processes appending 1,000 lines each with no torn line, the prune at the day's rollover (an injected clock), a malformed line skipped and counted, oversized and invalid UTF-8 fields mended and bounded, and an unwritable directory never throwing.
- **`tests/data/contracts/config_edit_observer_test.cpp`** — one call per landed edit with the key diff, one on a refused edit, none with no observer, and the golden round trips unchanged.
- **`tests/infrastructure/platform/current_user_test.cpp`**, and `append_private` under `platform_test`.
- **`tests/presentation/httpserver/admin_audit_test.cpp`** — an admin write recorded with actor `admin` and its peer; a `401` recorded as `unknown`, with no token text; a `GET` not recorded; a query string never present; the route's filters and document.
- **`tests/presentation/httpserver/handler_audit_test.cpp`** — an inference request recorded with backend, session and status, and a `400` recorded as `error` with its type.
- **`tests/presentation/cli/audit_cmd_test.cpp`** — every filter, the text table and the JSON document, an empty log, and `--since 7d`.
- **`cli.install_parity`** — for the new row.
- **The spike's probe is the regression fixture** — a shell case, `cli.audit_probe` (`tests/scripts/sh/audit_probe.sh`), in a temp `APOGEE_HOME` with the mock backend. It makes the seven config edits from Evidence 1, then runs `serve --bind 127.0.0.1 --port 0` (killed after) with two chats (one refused `400`), two refused admin requests, one admin read and two admin writes. It then requires `audit list --output-format json` to hold: seven `cli` entries with actor `local` and the changed key paths; two `admin` writes with actor `admin` and peer `127.0.0.1`; two `refused` entries with reason `authentication_error`; two `inference` entries, one `error` with status `400`; no entry for the admin read; the directory `0700` and the files `0600`.
- **`leak_test.cpp` extended** — it gives a distinctive prompt, a tool argument, a file path, a provider key (via `auth add --stdin`), the admin token, a one-byte-off token sent as a bearer, a token in a query string and a pairing code (once 59a has shipped). It then searches every audit file for each.
- **The anti-goal** — a test drives `chat` and `complete` with `audit/` absent, read-only and on a full-disk stub. Each still answers, and its exit code is unchanged.
- **The config golden suite** stays byte-identical with the observer installed.
- **`harness.layering`** holds `audit` to `contracts` and `platform`, and holds `contracts` free of `audit`.
- **`cli.no_listen_symbols`** stays green.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Where does the audit live? | `[default]` | In a **new private layout row, `audit/`** (`0700`, files `0600`). It is not under `logs/`, which is `0755` with `0644` files, and not in `config/`, which the config engine owns. It is marked user data, so `reset` and `uninstall` ask before removing it. |
| What format, rotation and retention? | `[default]` | **JSON Lines, one file per UTC day** (`audit-YYYY-MM-DD.jsonl`). Rotation is the day boundary, and retention is `audit.keep_days` (default 90), applied by whichever process makes the first write of a new day. No daemon or timer runs. There is no size cap: the days are the bound, and `check` shows the size. Lowering `keep_days` prunes at the next day's first write. It is not retroactive within the day. |
| Is there an off switch? | `[default]` | **No off switch.** A record its subject could switch off from the config it records is weak. It costs one line per act, and the smallest `keep_days` is 1. |
| How are config changes caught? | `[default]` | **By an observer on `edit_config_file`**, which reaches all 51 call sites, including `embed ingest`'s registration and `always` answers. The action comes from the scope's context: the command's words, or the route. Each entry records the changed key paths, plus the values on the allow-list only. |
| Are admin reads recorded? | `[default]` | **No**; only writes and refusals are. Reading the audit is not recorded either. |
| How is an inference request recorded? | `[default]` | **Every inference request is one entry**: the action, the backend served, the session id, the status and the token counts when the backend reports them. These are metadata, the "which model, when" the owner may see. 63c's usage ledger and limits are 63c's. |
| What does the permission gate record? | `[default]` | **Decisions on served and gated requests** for tools that write, edit or reach out, never the read-only ones, with the authority word and never the target or a host (63d's rule). At the person's own terminal only `always` is recorded, because it rewrites the shared policy. |
| What happens to a flood of refusals? | `[default]` | **A flood is collapsed.** Identical refusals from one peer on one route within a minute become one entry with a `count`, so a scanner's `401`s cannot push the record out or fill the disk. |
| What happens when a write fails? | `[default]` | **A failed write never fails the act.** It is reported once per process on stderr, written to the operational log, and shown by `check`. The anti-goal outranks completeness: a full disk must not stop the owner from fixing a config. |
| Is the audit tamper-proof? | `[default]` | **Not tamper-proof, and said so.** Append-only holds for every door, because no verb or route edits or deletes a line. The OS account that owns `APOGEE_HOME` can still edit the files. A hash chain and shipping to syslog are later calls. |
| How is the local actor's `via` set? | `[default]` | `terminal` when stdin and stdout are a terminal, `machine` under the machine mode, and `pipe` otherwise. That is how the server window's acts over pipes are told apart from the owner at a prompt. |
| May a marked device read the audit remotely, and how are its admin acts named? | `(consumed decision)` | [62g](administering-local-and-marked-devices.md)'s. Until it ships, the audit route sits behind the admin token like every admin row. |
| The `person` field, and who may read the audit (the owner only, never a member)? | `(consumed decision)` | [63a](people-on-one-server.md)'s, as are the `people` verbs and `--for` bindings as audited acts. If 63a has shipped when this item builds, this build carries the field. Who answered a question is recorded as [63d](consent-goes-to-its-person.md) routes it. |
| The live tail of these acts and per-credential counts? | `(consumed decision)` | [62e](connections-and-activity.md)'s. |
| The server window's audit screen, its filters and its words? | `(consumed decision)` | Darwin [10a](../../darwin/macos/v0.2.4/design-the-server-window.md)'s and [10b](../../darwin/macos/v0.2.4/server-window.md)'s, reading `audit list --output-format json` over pipes. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Specced from the admin-mode and people spike (wall A4). The user's calls that shape it: an admin mode, a separate "Apogee Server" window in the macOS app, offered only on the Mac that serves, driving the local CLI over pipes ([GUI SPEC](../../../assistant/gui/SPEC.md) → Core surfaces 3); administering is local by default, and the owner may mark chosen paired devices as allowed to administer remotely (62g), and otherwise the admin plane stays local-only, which revises 59d's default that the admin plane keeps one token for any network caller; the window installs the server as a background service (launchd) and starts, stops and restarts it (62d); the server console (CLI track 62, darwin/macos 10a-10b) and people and access (CLI track 63, darwin/macos 10c) both sit in v0.2.4, before v0.2.5, with nothing already queued renumbered; people are a household or a small team, designed for both, and the owner sees members' metadata (devices, apps, activity, which model, when) and never their chat contents, and the docs say honestly that whoever controls the server Mac can still read its disk; members use the server's local models only, unless the owner grants a household cloud key with a limit (63c); the words are "people" and "access" (Owner, Member), never "roles", which means model roles in Apogee; the defaults the user took: live changes applied on the spot by a reload, with what still needs a restart said (62b), **an audit kept 90 days of who, what and when, without request contents** (this item), collections as shared libraries handed out per person plus a member's own private ones, notes and saved decisions personal by default, a member's apps paired by the owner at the server Mac, capped at that member's allowance, a per-person task queue, not a refusal; the model, from the prior art (the agent's default): the owner is implicit, the OS account that owns `APOGEE_HOME` on the server Mac, which is why this item's local actor is the OS user, and members each hold an allowance in 59f's scope shape, with presets (Full, Family, Limited) instead of groups, and a guest is a member on Limited with nothing kept; remote use is an add-on, and the CLI alone and the Mac app over pipes stay complete and need no server. | the user |
| 2026-11-14 | The SPEC amendments are already written (CLI SPEC Core surfaces 4, "Revised again 2026-11-14"; GUI SPEC Core surfaces 3). This item is the CLI's half of "an audit trail visible to the owner". | — |
| 2026-11-14 | The item sits in v0.2.4 as 62f in track 62, the server console, and it is **ungated**. It is useful before any person exists, because local, admin-token and anonymous actors are real today and the spike's probe shows them going unrecorded. People (track 63, later in v0.2.4) get an accountable record to build on rather than retrofitting one. 59a and 59f sit in v0.2.3, an earlier release, so by the gate convention they will usually have shipped when this is built, and their verbs and gates are wired here. If either has not shipped, it wires its own rows, and a line in its document says so. | the spec |
| 2026-11-14 | The audit is a separate file from the operational log. That log answers "what the program did", is world-readable and is never pruned. The audit answers "who did what", is private and keeps 90 days. Merging them would change a recorded format and mode (ADR 0006). | the spec |
| 2026-11-18 | Moved here from v0.2.6 when the releases were merged; the id is unchanged. | the user |

## Out of scope

- the live view, connections and the event tail (62e);
- the person field and the owner-only read rule (63a);
- the usage ledger and limits (63c);
- consent routing (63d);
- the server window's screen (darwin 10a, 10b);
- tamper-proofing (a hash chain, signing) and shipping the audit off the machine (syslog, a remote collector);
- recording reads;
- any level that records request bodies;
- retention or a private mode for the operational log, which stays as it is.

## Links

- **Blocked by:** — · **Blocks:** —
- **Relates to:** [62a](serve-status-and-runtime-record.md), [62b](serve-reload-and-served-vs-disk.md), [62e](connections-and-activity.md), [62g](administering-local-and-marked-devices.md), [62h](local-admin-reads.md) (track 62 siblings); [59a](../v0.2.3/paired-devices.md), [59d](../v0.2.3/reads-and-commands-over-the-network.md), [59e](../v0.2.3/tasks-outlive-the-connection.md), [59f](../v0.2.3/paired-apps-with-scopes.md) (the credentials and gated doors the audit names); [63a](people-on-one-server.md), [63c](allowances-keys-and-usage.md), [63d](consent-goes-to-its-person.md) (people and access); darwin [10a](../../darwin/macos/v0.2.4/design-the-server-window.md), [10b](../../darwin/macos/v0.2.4/server-window.md) and [GUI 7a](../../darwin/v0.2.4/app-connection-local-or-server.md) (the server window and its connection); [CLI SPEC](../../../assistant/cli/SPEC.md) → Core surfaces 4; [GUI SPEC](../../../assistant/gui/SPEC.md) → Core surfaces 3; ADRs [0001](../../../adrs/cli/layer-enforcement.md), [0002](../../../adrs/cli/mode-parity.md), [0006](../../../adrs/cli/backwards-compatibility.md), [0007](../../../adrs/cli/tab-completion.md), [0008](../../../adrs/cli/module-map.md), [0010](../../../adrs/cli/the-shell-is-a-mode.md).
