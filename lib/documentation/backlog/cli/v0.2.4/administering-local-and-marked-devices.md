# CLI-62g · Administering is local, plus the devices you mark

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🔒 Blocked (59a, 59d) |
| **Fix version** | v0.2.4 |
| **Component** | `serve`'s admin plane (`/v1/admin`), 59d's command door, `devices` |
| **Labels** | `security` `remote-administering` `devices` `command-door` |
| **Epic** | Track 62 · the server console |
| **Blocked by** | [59a](../v0.2.3/paired-devices.md), [59d](../v0.2.3/reads-and-commands-over-the-network.md) |
| **Blocks** | — |
| **Reporter** | The admin-mode and people spike (2026-11-14), on the user's call |
| **Created** | 2026-11-14 |

---

## Summary

Today, and as 59a and 59d plan it, administering crosses the network two ways: the admin plane answers any peer holding its one token, and 59d's command door runs `config`, `auth` and host repairs for any paired device. This item makes the admin plane answer this Mac only, gives the door an `Administer` class that runs only for a paired device the owner marks at the server's Mac (`apogee devices admin NAME on|off`), and audits every mark and decision.

## Description

### User story

**As** the owner of an Apogee server,
**I want** the server administered on its own Mac, and remotely only from the paired devices I choose there,
**so that** holding the admin token or a pairing no longer lets any caller on the network change the server's settings or who may reach it.

### Background

The user's call, 2026-11-14, from the admin-mode and people spike: the server is administered on its own Mac. The CLI SPEC says it in its second revision that day: "Settings, devices, apps and their scopes, and people are changed there, through the local CLI over pipes. Remote administering is off by default, and the owner may allow it from paired devices they choose" ([SPEC.md](../../../assistant/cli/SPEC.md):32). The GUI SPEC's server window drives that local CLI over pipes, and is offered only on the Mac that serves ([GUI SPEC.md](../../../assistant/gui/SPEC.md):21). Today, and as 59a and 59d plan it, administering crosses the network two ways: the admin plane answers any peer holding its one token, and 59d's command door runs `config`, `auth` and host repairs for any paired device. This item closes both, and adds the one exception the user chose: a paired device the owner marks at the server's Mac.

This item delivers **administering that answers locally, plus the devices the owner marks**:

- The **admin plane answers this Mac only**. Every `/v1/admin` request from a non-local peer is refused before the token is looked at, whatever token it carries.
- 59d's network-door classification gains an **`Administer`** class: the families that decide what the server may do and who may reach it. A door request in that class runs only for a paired device the owner has marked. It is refused to every other device and to every app, whatever its scope.
- **`apogee devices admin NAME on|off`** marks or unmarks a device. It runs at the server's Mac only, over pipes or at its terminal, never through any door.
- Marking, unmarking and every `Administer` decision are written to the audit log ([62f](audit-log.md)).

### Evidence

**A5 — administering still crosses the network.** Re-measured 2026-11-14 at HEAD (`4acf4a2b`), with a probe on the branch binary (`apogee 0.2.0 (b37b0a9f)`) in a scratch `APOGEE_HOME` with the `mock` backend, `serve --bind 127.0.0.1 --port 0 -v` on loopback, killed after:

1. **The admin plane shares serve's listener, and its gate asks only for the token.** `Mux::dispatch` runs the host check, then, on an admin path, `bearer_valid` and nothing else (`presentation/httpserver/mux.cpp:625-641`). The reference says so: "There is no separate bind flag for this plane. A non-loopback bind already needs `--allow-remote` (above), and the bearer is required on top of it" ([http-api.md](../../../reference/cli/http-api.md):321-322).
2. **Two of 52 admin writes look at the peer.** `kRoutes` holds 89 admin rows at HEAD, 37 reads and 52 writes (`mux.cpp:31`, counted off the table). Only `PUT /v1/admin/auth/{id}` (`admin_auth_routes.cpp:72`) and a literal `api_key` on `POST /v1/admin/backends` (`admin_config.cpp:326`) call `is_loopback_host(request.remote_address)`. A grep finds no third call. `DELETE /v1/admin/auth/{id}` clears a stored key and its handler is never handed the request at all (`admin_clear_credential(context, provider)`, `admin_auth_routes.cpp:106`).
3. **Probed: the token alone changes the server's policy.** With no bearer, `PUT /v1/admin/permissions/run_command` answered `401`. With it: that `PUT` (`{"level":"allow"}`) `200`, `PUT /v1/admin/allowed-hosts/example.com` `200`, `POST /v1/admin/backends` (a second `mock`) `201`, `POST /v1/admin/backends/default` `200`, `PUT /v1/admin/auth/openai` `200` and `DELETE /v1/admin/auth/openai` `200`. Each landed in `config.json` (`"run_command": "allow"`, `"allowed_hosts": ["example.com"]`, `"default": "mock2"`). The probe ran from a loopback peer, but nothing on the five non-secret routes reads the peer (point 2), so under `--allow-remote` the same token from the LAN does the same.
4. **59d keeps it so, and adds a second path.** Its decision: "The admin plane keeps its one token for scripts and the operator" ([`reads-and-commands-over-the-network.md`](../v0.2.3/reads-and-commands-over-the-network.md), Decision log, 2026-11-07: a paired device reaches no admin route, and the admin plane keeps its one token). Its door runs `config add-backend` for any paired device (the same document's Testing, `cli.command_door_lifecycle`: `config add-backend` leaves the config byte-identical to the local command's; and its Acceptance criteria: `config add-backend` through the door leaves `config.json` byte-identical) and the host's own facts as commands, `check --fix`, `providers scan|register`, `config scan` and `config migrate|upgrade` among them (the same document's Open questions: the default that the host's own facts run as commands, because the device's person owns the host). 59f takes `apps`, `devices`, `auth`, `config`, `reset`, `uninstall` and `serve` away from every app ([`paired-apps-with-scopes.md`](../v0.2.3/paired-apps-with-scopes.md), Technical approach: the `tui_parity.cpp` and `remote_parity.cpp` row, where an app credential is refused those families whatever its scope; and its Open questions: the default that the families deciding who may do what are never an app's), and leaves the person's devices "the whole harness" (the same document's Decision log, 2026-11-14: the revision of 59d's and 59e's defaults for apps only).
5. **"Loopback" is weaker than it looks under `--allow-remote`.** That flag means "on any bind answer whatever Host a reverse proxy forwards" (`serve --help`). It turns the Host check off (`host_check_applies`, `serve.cpp:106-108`; `request_refusal`, `mux.cpp:555-578`), and no `Forwarded` or `X-Forwarded-For` header is read anywhere (grep). So behind a proxy on the same Mac, every remote caller arrives as a loopback peer. Separately, `is_loopback_host` knows `127.*`, `::1` and `localhost` but not an IPv4-mapped `::ffff:127.0.0.1` (`http_types.cpp:20-26`), which a dual-stack bind reports for a local caller.

## Acceptance criteria

- [ ] **Given** a sandbox home with the mock backend and no registry present, **when** `apogee config set-permission run_command allow`, `config add-backend` and `auth clear` run over pipes, **then** they behave exactly as before.
- [ ] **Given** `serve --port 0` on loopback, **when** the admin token's writes are sent, **then** they answer as before; **when** every admin route is driven through `Mux::dispatch` from `192.0.2.10` with that token, **then** each answers `403` with the message recorded in Open questions, and `config.json` is byte-identical afterwards. Under `allow_remote`, a loopback peer with a non-loopback `Host`, or with `X-Forwarded-For`, is refused the same way. `::ffff:127.0.0.1` is admitted.
- [ ] **Given** a device `dev` paired (59a's test pairing), **when** it sends `config set-permission run_command allow` through the door, **then** the door answers `403 refused_remotely`, naming `apogee devices admin dev on`, and the config is unchanged. `models list` through the door still answers.
- [ ] **Given** `apogee devices admin dev on` has run, **when** the same request is sent, **then** it answers `200` and the config carries it, and `devices list --json` shows `"admin": true`. **When** `devices admin dev off` runs, **then** the next request is `403`.
- [ ] **Given** an app paired with the widest scope 59f allows, **when** it sends any `Administer` row, on loopback too, **then** it is refused. `apogee devices admin <app name> on` is refused, naming why.
- [ ] **Given** a marked device, **when** it sends `devices admin`, `devices pair` or `apps pair` through the door, **then** each is refused (as for every caller), and so are bare `serve` and, where 62d has shipped, `serve service stop|restart`. Where 62a has shipped, a marked device's `serve status` through the door answers and an unmarked device's is refused.
- [ ] **Given** no device is marked, **when** `apogee check` runs, **then** it shows "Remote administering: local only"; after a mark it shows "allowed from 1 device (dev)". Both are Ok, and so is a home with no registry.
- [ ] **Given** 62f has shipped, **when** the audit is read, **then** it shows the mark on, the refused and the allowed `Administer` requests by device name, the mark off, and the remote admin-plane refusal by address. No record carries a token.
- [ ] The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user runs `apogee-dev serve --bind 0.0.0.0 --allow-remote` on the dev channel. From a second machine on the LAN: `curl` with the admin token gets `403` on `/v1/admin/backends`; a paired, unmarked device's `config set-default` through the door is refused; after `apogee-dev devices admin <name> on` on the server Mac, the same request goes through.

## Constraints

- **Remote mode is an add-on (the anti-goal).** Nothing here may make standalone use need a server, a pairing or an account. Over pipes, the CLI and the Mac app administer exactly as today: no mark, no registry and no running server are consulted. A fresh install with no keys and no models still passes `check` (SPEC → Success criteria). The new `check` row is Ok with no registry at all.
- **One config mutation path** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → One config mutation path). A marked device's `Administer` request runs the CLI's own verb as 59d's captured child, so every config write still goes through `contracts/config_edit.h`. The mark is not a config key. It lives in 59a's device registry, written under that file's lock through `write_file_atomically(…, true)`, so `config set` from a marked device can never mark another.
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). A literal secret is still accepted from a local caller only. `auth add` stays refused through the door even for a marked device (59d's row), and `PUT /v1/admin/auth/{id}` keeps its own peer check. A refusal names the device and the verb, never a token. The admin token is never compared for a non-local peer, so a remote caller cannot test guesses against it.
- **Older versions keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The registry field, the verb, the door's class and the `check` row are additive. An older `devices.json` with no `admin` field reads as unmarked. Refusing the admin plane to remote peers revises a recorded behavior ([http-api.md](../../../reference/cli/http-api.md):321-322). It lands as a dated revision there and a release-notes line, with its migration named: pair the script as a device, mark it, and send its commands through the door.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). `devices admin` answers from the CLI (`--json`) and is classified for the shell. It is a recorded carve-out in the HTTP parity table and refused in the network-door table, each with its reason: the mark is made at the server's Mac. The `Administer` class lives in one place, 59d's `remote_parity`, and 59f's never-an-app's families read it, so no door keeps its own list.
- **Never listens; serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). `devices admin` writes a file and opens no socket. The peer test is in `httpserver`, which already owns the listener. Nothing else learns about peers.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). The mark sits in Data (59a's registry). The class and the verb sit in `cli`, and the peer gate in `httpserver` (both Presentation). The door takes the caller's mark as a value wired by `serve_cmd`, as 59d takes its runner, so `httpserver` still includes nothing of `cli`.
- **UTF-8 where it enters** (CLI CLAUDE.md). The device name `devices admin` takes is matched as 59a stores it, ignoring case. Before a refusal echoes a `Host` or a device name, the name is mended and bounded.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Probes and tests use a temporary `APOGEE_HOME` under the scratchpad and the mock backend. A live pass runs `apogee-dev serve` over `~/.apogee-dev`, never the release `apogee` or `~/.apogee`.

## Technical approach

| File | Change |
|---|---|
| `source/data/secrets/devices.h/.cpp` (59a's registry) | A device row gains `admin` (bool; a missing field means `false`) and `admin_since`. `set_admin(name, on)` writes it under the registry's lock. It refuses an app row and an unknown name, each with its own message. `DeviceView` gains `admin`. A revoked device takes its mark with it, and a re-paired device starts unmarked. The registry is read fresh by mtime (59a), so `devices admin NAME off` applies at the device's next request. |
| `source/presentation/cli/devices_cmd.h/.cpp` | `apogee devices admin NAME on\|off [--json]`. It prints in plain words what the device may now do, or no longer do, and the line that undoes it. Running `on` for a device already on says so and exits 0. `devices list` gains an `admin` column and a JSON field. It has help and completion ([ADR 0007](../../../adrs/cli/tab-completion.md)), and completes device names only, never apps. |
| `source/presentation/cli/remote_parity.h/.cpp` (59d) | An `Administer` class beside `Command`, `Job`, `Session`, `Task` and `Refused`. `remote_refusal(words, caller)` takes the caller: a marked device, an unmarked device, or an app. An `Administer` row runs only for the first. `administer_families()` is the one list that 59f's `command_refusal` reads for "never an app's". `remote_law_violations` gains a law: every leaf of `config`, `auth`, `mcp`, `providers`, `devices`, `apps`, `serve`, `audit` (when 62f ships) and `people` (when 63a ships) is classified `Administer`, `Command` (a named read) or `Refused`. A new leaf in those families fails until it is classified. Flag rows (`config scan --register`, `providers scan --register`, `check --fix`) are matched by word, as `exec_refusal` matches. |
| `source/presentation/httpserver/command_door.h/.cpp` (59d) | The caller context gains `admin_capable`. An `Administer` row for any other caller is answered `403 refused_remotely` with the class's reason, and the runner is never called. `GET /v1/harness/commands` gives each row its class and, for the asking caller, whether it would run, so a device can grey out a row before asking. |
| `source/presentation/httpserver/mux.cpp` | In `Mux::dispatch`, between the host check (`:628-630`) and the bearer (`:638`), an admin path from a non-local peer gets `403 forbidden`. The test is `admin_peer_is_local(request, allow_remote)`, a new helper beside `is_loopback_host` in `http_types.h/.cpp`. The peer must be loopback. Under `--allow-remote` the `Host` must also be a loopback name (`is_loopback_name`, `mux.cpp:544-546`), and the request must carry no `Forwarded` or `X-Forwarded-For` header. The two per-route peer checks (`admin_auth_routes.cpp:72`, `admin_config.cpp:326`) stay as they are. |
| `source/presentation/httpserver/http_types.cpp:20-26` | `is_loopback_host` accepts the IPv4-mapped form `::ffff:127.*`. The bind policy (`serve.cpp:97`) reads the same helper and is correct with it. |
| `source/presentation/cli/serve_cmd.cpp` | Wires the caller's mark (59a's registry, read by mtime) into the door. On a remote bind, or under `--allow-remote`, the start notes say "/v1/admin answers this machine only" and name the remote administering in force: "off", or the marked devices by name. |
| `source/presentation/cli/check.cpp` | A "Remote administering" row: "local only", or "allowed from N devices (names)". It is Ok either way and informational, and Ok with no registry. |
| `source/presentation/cli/tui_parity.cpp` | `devices admin` classified (the shell runs it on the server's Mac). |
| The audit writer ([62f](audit-log.md)) | Records for `devices admin` on and off (actor: the OS user over pipes; target: the device), each `Administer` request through the door (actor: the device; the verb's words, never its stdin), and each refusal: an unmarked device, an app, a non-local peer on the admin plane (its address, never its token). If 62f has not shipped when this item builds, 62f adds these call sites in its own build, against this item's class. |

**The `Administer` families** (the recorded default; see Open questions). They are the ones whose purpose is settings and access:

- every `config` leaf except `path`, `get` and `scan` (`config init` stays `Refused`, the ground under the server);
- `auth clear` (`auth list` is `Command`, and `auth add` stays refused as a literal secret);
- `mcp create|enable|disable` (`mcp list|presets|test` are `Command`);
- `providers register`, and `providers scan --register` and `config scan --register`;
- `check --fix`;
- every `devices` and `apps` leaf, except `devices pair`, `devices admin` and `apps pair`, which are local-only for every caller;
- `people` when 63a ships;
- the owner's view of the server, as each ships: `serve status` (62a), `serve reload` (62b), `serve service status` (62d), `serve activity` (62e) and `audit list` (62f);
- not the lifecycle: bare `serve` and `serve service install|uninstall|start|stop|restart` stay `Refused` for every caller, the ground under the server. A restart sent through the door would end the door's own connection and the job that asked, and a change that needs one is named by `serve reload` and done at the server's Mac. (The alternative, if the user vetoes: `serve service restart` as a fire-and-forget door request answered `202` before launchd restarts the server.)

A work verb that writes its own config entry as part of its work stays its own class: `models pull` registering its backend, `embed ingest` making a collection, `graph build`, `models delete|rename`. The line is drawn at the families whose job is the server's settings and who may reach it. What a member's work may touch is track 63's allowances ([63c](allowances-keys-and-usage.md)).

**Reference.**

- Siblings:
  - [59a](../v0.2.3/paired-devices.md): the registry the mark lives in, read by mtime, and the rule that a device token opens the admin plane nowhere. The harness plane "always requires a paired device, on loopback too" (its Decision log, 2026-11-07: the harness plane always requires a paired device, so a test and a deployment exercise the same gate), which is why the door judges by credential, not by peer.
  - [59d](../v0.2.3/reads-and-commands-over-the-network.md): the classification, `remote_refusal`, `refused_remotely`, `GET /v1/harness/commands`, and the decision this item revises (its Decision log, 2026-11-07: the admin plane keeps its one token for scripts and the operator).
  - [59f](../v0.2.3/paired-apps-with-scopes.md): never-an-app's families (its Open questions: the default that `apps`, `devices`, `auth`, `config`, `reset`, `uninstall` and `serve` are never an app's, whatever its scope), which become this class.
  - [62a](serve-status-and-runtime-record.md): `serve status` shows the remote administering in force.
  - [62b](serve-reload-and-served-vs-disk.md) and [62d](serve-as-a-background-service.md): `serve reload` and the `serve service` verbs. Their rows land in their own builds, classed as this item's open calls say.
  - [62e](connections-and-activity.md): `serve activity`, a marked device's view of who is connected.
  - [62f](audit-log.md): the audit log that records the mark and every decision.
  - [62h](local-admin-reads.md): the JSON reads the server window shows, `devices list`'s `admin` field among them.
  - [63a](people-on-one-server.md): people, and whose devices may be marked.
- GUI:
  - [darwin 10b](../../darwin/macos/v0.2.4/server-window.md): the server window, where the owner flips the mark on a device's row by running `devices admin` over pipes.
  - [GUI 7a](../../darwin/v0.2.4/app-connection-local-or-server.md) and [7c](../../darwin/v0.2.4/every-place-against-a-server.md): a Mac pointed at a server sends its settings commands through the door. Unmarked, it meets this item's refusals and shows them.
- Precedents:
  - The two per-route peer checks (`admin_auth_routes.cpp:72`, `admin_config.cpp:326`): "The socket's own peer address decides, never a header a client could set".
  - The Host and Origin gate that runs before the admin gate (57n, `mux.cpp:626-630`).
  - 59a's "a pairing code is never minted over HTTP" ([`paired-devices.md`](../v0.2.3/paired-devices.md), Open questions: the default that list and revoke are served on the admin plane, and a pairing code, a live credential, is never minted over HTTP), which `devices admin` follows.
- Prior art (the spike's, from their official docs): Jellyfin's per-user administrator flag and Home Assistant's owner, admin and user. In both, administering is something the owner grants each identity, not a second credential. Here the identity is a paired device, and the grant is one switch.

**Docs when it ships:**

- [http-api.md](../../../reference/cli/http-api.md): a dated revision of the admin-plane bullet (`:321-322`), "answers this machine only", with the proxy rule and the migration. Its "The command door" chapter gets the `Administer` class.
- The commands reference gets `devices admin`.
- CLI CLAUDE.md's secrets sub-rules get "the admin token is never compared for a non-local peer".
- The 59d and 59f records (MILESTONES, or their documents if still pending) get a dated line pointing here.

## Testing

Hermetic: a temporary `APOGEE_HOME`, the mock backend; handlers driven through `Mux::dispatch` with no port, where a remote peer is an `HttpRequest.remote_address` from TEST-NET-1, `192.0.2.10`.

- **`tests/data/secrets/devices_test.cpp`** — the mark round trip; an older file with no field read as unmarked; `0600` after the write; an app row refused; revoke drops the mark; a second reader sees `off` at its next read.
- **`tests/presentation/cli/devices_cmd_test.cpp`** — `admin on|off` output and `--json`; an unknown name; an app's name refused ("an app is never admin-capable"); `on` twice.
- **`tests/presentation/cli/remote_parity_test.cpp`** — every leaf of the administering families classified; `config path|get|scan`, `auth list` and `mcp list` are `Command`; `devices admin`, `devices pair` and `apps pair` refused for every caller; `serve status` and `serve reload` `Administer`, bare `serve` and `serve service stop|restart` refused for a marked device too; a planted unclassified `config` leaf fails the law by name; 59f's never-an-app's families equal `administer_families()` plus `reset`, `uninstall` and `serve`.
- **`tests/presentation/httpserver/command_door_test.cpp`** — the matrix of {unmarked device, marked device, app with the widest scope} × {a `Command` row, an `Administer` row, a local-only row}. The scripted runner is called only for a marked device's `Administer` row and every caller's `Command` row. A mark turned off between two requests refuses the second. `GET /v1/harness/commands` answers per caller.
- **`tests/presentation/httpserver/admin_auth_test.cpp`** — every admin route (all 89, read off `kRoutes`) from `192.0.2.10` with a valid bearer answers `403`, and the handler never runs (the config byte-identical). A loopback peer is `200`, and so is `::ffff:127.0.0.1`. Under `allow_remote`, a loopback peer with `Host: apogee.example.com` is `403`, and so is one with `X-Forwarded-For`. A marked device's `apd_` token on `/v1/admin` is `401` (a device token still opens the admin plane nowhere).
- **`tests/presentation/httpserver/parity_test.cpp`** — `devices admin` carved out with its reason.
- **`tests/presentation/cli/leak_test.cpp`** — a planted admin token and device token never appear in a refusal, a start note, a `check` row or an audit record.
- **`cli.admin_local_only`** (`tests/scripts/sh/admin_local_only.sh`) — this spike's probe as the regression fixture. The real binary runs under `serve --bind 127.0.0.1 --port 0` with 59a's test pairing:
  - the six admin writes above (Evidence, point 3) answer as probed from loopback;
  - an unmarked device's `config set-permission run_command allow` through the door is `403 refused_remotely`, with `config.json` unchanged;
  - after `apogee devices admin dev on`, the same request is `200` and the config carries it;
  - after `devices admin dev off`, the next one is `403`;
  - an app token's `config get` succeeds, and its `config set` is refused.

Guardrails:

- **The admin-plane matrix** runs every route in `kRoutes`, so a new admin route cannot ship reachable from the network.
- **The classification law** runs over the live registry, so a new `config`, `auth`, `mcp`, `providers`, `devices`, `apps`, `serve`, `audit` or `people` leaf cannot ship unclassified.
- **One list** feeds both the door's `Administer` check and 59f's never-an-app's families. A test plants a family in one place only, and it fails.
- **`cli.admin_local_only`** is the spike's probe kept as a fixture. Local administering keeps answering, an unmarked device's config write is refused with the file untouched, and a mark flipped on and off flips the answer at the next request.
- **`leak_test`** covers refusals and audit records.
- **The full local suite**, run with no server, no registry and no mark, holds the anti-goal.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Which families are `Administer`? | `[default]` | The ones whose purpose is settings and access, listed in full under Technical approach › **The `Administer` families**: every `config` leaf except `path`, `get` and `scan` (`config init` stays `Refused`); `auth clear`; `mcp create\|enable\|disable`; `providers register` and the `--register` forms; `check --fix`; every `devices` and `apps` leaf except `devices pair`, `devices admin` and `apps pair` (local-only for every caller); `people` when 63a ships; the owner's view of the server as each ships (`serve status`, `serve reload`, `serve service status`, `serve activity`, `audit list`). Not the lifecycle: bare `serve` and `serve service install\|uninstall\|start\|stop\|restart` stay `Refused` for every caller (alternative if the user vetoes: `serve service restart` as a fire-and-forget door request answered `202` before launchd restarts the server). A work verb that writes its own config entry as part of its work stays its own class; what a member's work may touch is track 63's allowances ([63c](allowances-keys-and-usage.md)). |
| How fine is the mark? | `[default]` | One switch per device, all of `Administer` or none. A finer mark (some families only) waits for people and their access presets (track 63, later in v0.2.4). |
| Does the mark expire? | `[default]` | No expiry. It stays until `devices admin NAME off` or the device is revoked. Turning it off applies at that device's next request. A door job of class `Administer` that is already running finishes, and the next request is refused. |
| Does `devices admin` ask for confirmation? | `[default]` | It asks no question. It is made at the server's Mac by whoever controls it, it is undone by one line, and the server window runs it over pipes. It prints what the device may now do and that line. |
| How is a non-local peer on `/v1/admin` answered? | `[default]` | `403`, type `forbidden`, before the bearer is read. The message is: "the admin plane answers this machine only -- administer on the server's Mac, or from a device its owner marked ('apogee devices admin NAME on' there), through the command door". The plane's existence is documented, so it is not hidden behind a `404`. What matters is that the token is never compared. |
| What counts as "local" under `--allow-remote`? | `[default]` | A loopback peer that also has a loopback `Host` and no `Forwarded` or `X-Forwarded-For` header, so a reverse proxy on the same Mac does not make every remote caller local. A proxy that rewrites `Host` to `localhost` and strips those headers defeats this. The http-api.md revision says so, and says to administer such a deployment over pipes or from a marked device. Under `--tls off` on a remote bind, 59a does not mount the harness plane, so remote administering there is pipes only. |
| Does the door judge a device by its credential or its peer? | `[default]` | By its credential, not its peer. An unmarked device is refused `Administer` on loopback too, so a test and a deployment exercise the same gate (59a's rule: [`paired-devices.md`](../v0.2.3/paired-devices.md), Decision log, 2026-11-07: the harness plane always requires a paired device, on loopback too). |
| Is there a flag to reopen the admin plane to the network? | `[default]` | No. The user's call keeps it local-only except through the mark. A remote script's migration is to pair it as a device (`devices pair --print-token --name NAME`, 59a), mark it, and send its commands through the door. |
| Whose devices may be marked once people exist? | `(consumed decision)` | 63a. Until 63a ships, every paired device is the owner's. The spike's recommendation for 63a: only the owner's devices, since the administering families are never a member's. |
| The audit record's shape, retention (90 days, the user's default) and read verb | `(consumed decision)` | 62f. This item names only what it writes, and that `audit list` is `Administer` through the door. |
| The door's transport, limits, `refused_remotely`, and the classification's other classes | `(consumed decision)` | 59d. This item adds one class and a caller. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Specced from the admin-mode and people spike. The user's calls that shape it: (1) An admin mode: a separate "Apogee Server" window in the macOS app, offered only on the Mac that serves, driving the local CLI over pipes. (2) **Remote administering is local by default, and the owner may mark chosen paired devices as allowed to administer remotely.** This revises 59d's recorded default that "the admin plane keeps its one token for scripts and the operator" from any network caller ([`reads-and-commands-over-the-network.md`](../v0.2.3/reads-and-commands-over-the-network.md), Decision log, 2026-11-07). The admin plane stays local-only otherwise. (3) The server's lifecycle is the window's: it installs the server as a background service under launchd and starts, stops and restarts it ([62d](serve-as-a-background-service.md)). (4) Placement: both halves before v0.2.5, both in v0.2.4: the server console (CLI track 62, darwin/macos track 10's 10a-10b) and people and access (CLI track 63, darwin/macos 10c). Nothing already queued was renumbered. (5) People: a household or a small team, designed for both. The words are **people** and **access** (Owner, Member), never "roles", which in Apogee means model roles. The owner sees members' metadata (devices, apps, activity, which model, when), never their chat contents; whoever controls the server Mac can still read its disk, and the docs say so. A member uses the server's local models only, unless the owner grants a household cloud key with a limit. (6) Defaults the user took: live changes applied on the spot (reload), with what still needs a restart said; an audit kept 90 days of who, what and when, without request contents; collections are shared libraries handed out per person, plus a member's own private ones; notes and saved decisions personal by default; a member's apps paired by the owner at the server Mac, capped at that member's allowance; a per-person task queue, not a refusal. (7) The model (the agent's default, from the prior art): the owner is implicit (the OS account that owns `APOGEE_HOME` on the server Mac), and members each have an allowance in 59f's `AppScope` shape, set from presets (Full, Family, Limited) instead of groups. A guest is a member on Limited with nothing kept. (8) The SPEC amendments are already written (CLI [SPEC.md](../../../assistant/cli/SPEC.md):31-42, Core surfaces 4, "Revised again 2026-11-14"; GUI [SPEC.md](../../../assistant/gui/SPEC.md):21, Core surfaces 3). (9) Standing law: remote use is an add-on. The CLI alone and the Mac app over pipes stay complete and need no server. | the user |
| 2026-11-14 | Placement: CLI 62g, v0.2.4 (track 62, the server console), gated on 59a (the registry the mark lives in) and 59d (the door and its classification). It is not gated on 62f: whichever of the two builds second adds the audit call sites. It is not gated on 59f either (59f gates on 59a alone): if 59f has not shipped when this item builds, the app caller and 59f's never-an-app's reading of `administer_families()` are added in 59f's build, against this class. The server window ([darwin 10b](../../darwin/macos/v0.2.4/server-window.md)) does not gate on it, and shows the switch when both have shipped. | the spec |
| 2026-11-14 | Revisions this item makes to queued siblings, each recorded where it lands: (1) 59d's door no longer runs administering verbs for every paired device. Its index row's "a paired device runs any command the person may run" becomes "except the administering families, which need a device its owner marked". (2) 59d's host facts split (its Open questions: the default that the host's own facts run as commands): `check`, `providers scan` and `config scan` stay `Command`. `check --fix`, `providers register`, the `--register` forms, and `config migrate\|upgrade\|format` are `Administer`, because each changes the server's files or settings. (3) 59d's acceptance line for `config add-backend` through the door (its Acceptance criteria: `config.json` byte-identical to the local command's) holds for a marked device. (4) 59f's never-an-app's families are read from this class. (5) 59d refuses `serve` by word. The owner's reads of the server (`serve status`, `serve activity`, `serve service status`) and `serve reload` are carved out of that refusal as `Administer`, matched by word. This answers 62a, where a remote device's view of server status "goes through 62g's path, not this verb" (its Decision log), and 62e's "who may reach `GET /v1/admin/activity` from off the Mac" (its Open questions). A marked device sends the verb through the door, the door runs it as a child on the server's own Mac, so `serve status` still asks only that machine's server, and the admin routes stay this Mac's. (6) 62f's open question, whether a marked device may read the audit remotely (its Open questions): it may, `audit list` through the door, classed `Administer`. Unmarked devices and apps are refused. (7) 62d's "the lifecycle verbs are never run from the network" (its Open questions) holds: every `serve service` verb but `status` stays refused, a marked device included. (8) 62h's verbs (its Open questions): `devices pending\|rename\|revoke` and `apps pending\|rename\|scope\|revoke` are `Administer`, `devices list` and `apps list` are `Administer` reads, `devices pair` and `apps pair` stay local-only for every caller. (9) GUI 7c's Settings default, where every pane over `config`, `check`, `providers` and `auth` "reads and writes the server's when remote" ([`every-place-against-a-server.md`](../../darwin/v0.2.4/every-place-against-a-server.md), Open questions: the default that Settings splits by owner), writes only from a Mac its owner marked. An unmarked Mac shows the door's refusal. The line lands in 7c's record (or its document while pending) beside 59d's. | the spec |
| 2026-11-14 | The mark lives in the device registry, not the config. A marked device may change the config, so a mark kept there could be widened by the device it limits. | the spec |
| 2026-11-14 | A marked device administers through 59d's command door, never through the admin plane. The door identifies the device, runs the CLI's own verb with its own refusals, and is audited by device name. The admin token stays a per-install secret for scripts on the server's Mac. Opened to the network, it would again be "whoever holds it holds the whole plane" (59a's wall: [`paired-devices.md`](../v0.2.3/paired-devices.md), Evidence, point 2: the only credential is one token per install). | the spec |
| 2026-11-18 | Moved here from v0.2.6, when the releases were merged; the id is unchanged. | the user |

## Out of scope

- people, members, and whose devices may be marked (63a)
- a member's allowance (63c)
- the server window and its switch (darwin 10a, 10b)
- the audit log itself (62f)
- the `serve reload`, `serve service`, `serve activity` and `audit list` verbs themselves (62b, 62d, 62e, 62f; their door rows follow this item's classes)
- a finer, per-family mark
- an expiring mark
- any route that reopens the admin plane to the network
- remote administering through the admin plane with a device token
- how GUI 7c shows a refused settings place on a Mac pointed at a server

## Links

- **Blocked by:** [59a](../v0.2.3/paired-devices.md), [59d](../v0.2.3/reads-and-commands-over-the-network.md) · **Blocks:** —
- **Revises:** 59d's recorded default that the admin plane keeps its one token for scripts and the operator ([`reads-and-commands-over-the-network.md`](../v0.2.3/reads-and-commands-over-the-network.md), Decision log, 2026-11-07); the recorded admin-plane behavior in [http-api.md](../../../reference/cli/http-api.md):321-322; GUI 7c's Settings default ([`every-place-against-a-server.md`](../../darwin/v0.2.4/every-place-against-a-server.md))
- **Follows from:** the admin-mode and people spike, 2026-11-14; CLI [SPEC.md](../../../assistant/cli/SPEC.md):31-42 and GUI [SPEC.md](../../../assistant/gui/SPEC.md):21
- **Relates to:** [59f](../v0.2.3/paired-apps-with-scopes.md), [62a](serve-status-and-runtime-record.md), [62b](serve-reload-and-served-vs-disk.md), [62d](serve-as-a-background-service.md), [62e](connections-and-activity.md), [62f](audit-log.md), [62h](local-admin-reads.md), [63a](people-on-one-server.md), [63c](allowances-keys-and-usage.md), [darwin 10b](../../darwin/macos/v0.2.4/server-window.md), [GUI 7a](../../darwin/v0.2.4/app-connection-local-or-server.md), [GUI 7c](../../darwin/v0.2.4/every-place-against-a-server.md); [ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0006](../../../adrs/cli/backwards-compatibility.md), [ADR 0007](../../../adrs/cli/tab-completion.md), [ADR 0008](../../../adrs/cli/module-map.md), [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)
