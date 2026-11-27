# CLI-62c · How the server connects, kept in the config

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.4 |
| **Component** | `serve` and the config (`config set` / `config get`, the admin twin, the starter config) |
| **Labels** | `serve` `config` `server-console` |
| **Epic** | Track 62 · the server console |
| **Blocked by** | — |
| **Blocks** | [62d](serve-as-a-background-service.md), [darwin 10b](../../darwin/macos/v0.2.4/server-window.md) |
| **Reporter** | The admin-mode and people spike (2026-11-14), on the user's calls |
| **Created** | 2026-11-14 |

---

## Summary

How clients connect to `serve` (bind, port, `allow_remote`, TLS, `devices_only`, the OpenAI door, `tools`, `session_ttl`, remote administering) lives only on the command line that started the process. This item adds a `serve` section to the config that `serve` reads as its defaults, with its flags still winning for one run, edited only through the one config editor, read with `config get serve` and its JSON form, carried as a commented block in the starter config and brought by `config upgrade`. A bad value there never stops anything but `serve`.

## Description

### User story

**As** the owner of the Mac that serves Apogee,
**I want** how clients connect kept as stored settings that `serve` reads as its defaults,
**so that** the Apogee Server window can show and change them over pipes, and a background service can run `serve` without freezing them into its launch arguments.

### Background

The user's calls, 2026-11-14 (the admin-mode and people spike): the Mac that serves gets an "Apogee Server" window that administers the server through the local CLI over pipes, and installs, starts, stops and restarts it as a background service. Both need **how clients connect** to be stored settings. Today the only place they live is the command line that started the process, which the window does not hold and a service would freeze into its launch arguments.

This item adds a `serve` section to the config: the bind, the port, `allow_remote`, TLS, `devices_only`, the OpenAI door on or off, `tools`, `session_ttl`, and remote administering on or off. `serve` reads it as its defaults, and its flags still override it for one run. It is edited through the one config editor with `config set serve.<key>` and its admin twin, read with `config get serve` (`--output-format json` too), carried in the starter config, and brought to older files by `config upgrade`.

### Evidence

**A6 — how clients connect is command-line flags, not settings.** Re-measured 2026-11-14 at HEAD (`4acf4a2b`) against the branch binary (`apogee 0.2.0 (b37b0a9f)`), a scratch `APOGEE_HOME` under the session scratchpad and the `mock` backend, `serve` on loopback with `--port 0`, killed after each run:

1. **Every transport setting is a flag.** `ServeFlags` holds the bind, port, `allow_remote`, `tools` and `session_ttl` with their defaults written in the struct (`presentation/cli/serve_cmd.cpp:45-64`; `session_ttl = 60` at `:61`), registered as options at `:190-241`. The callback loads the config once (`:248`) and reads no serve section, because `Config` has none (`data/contracts/config.h:1163`). The binary's `serve --help` lists 16 flags. The queued siblings add more flags of the same kind: 59a adds `--tls`, `--tls-cert`, `--tls-key` and `--devices-only` ([`paired-devices.md`](../v0.2.3/paired-devices.md) (Seam + files: `serve_cmd.cpp` gains `--tls auto|on|off`, `--tls-cert PATH`, `--tls-key PATH`; Open calls: the default that `--devices-only` is in scope and off by default)), and 59c adds `--cloud-parallel` and `--max-sessions` ([`many-sessions-at-once.md`](../v0.2.3/many-sessions-at-once.md) (Open calls: the defaults for a cloud lane's width, `serve --cloud-parallel N`, and at most 16 live network sessions, `serve --max-sessions N`)).
2. **A hand-written section is silently ignored.** Probed: with `"serve": {"bind": "127.0.0.1", "port": 0, "allow_remote": false, "tools": true, "session_ttl": 5}` added to the config:
   - `config get models.default` still answered `mock`;
   - `check` said `config.json parses cleanly`;
   - `config upgrade` said the file `is current`;
   - `serve --port 0` started with `tools: none ... sessions idle out after 60 min`, and only `--tools` on the command line gave `tools: 25 (...)`.

   Nothing said the section was ignored.
3. **The one editor cannot reach it.** `config set serve.port 9000` was refused with the nine keys the table moves (`data/contracts/config_edit.cpp:3362-3397`). With the admin token, its twin `PUT /v1/admin/config/serve.port` answered `400` with the same message, while `PUT /v1/admin/config/ui.markdown` answered `200` with `"restart_required":true`. `config get serve` and `config get serve.port` answered `no such config key`. `config get models.default --output-format json` was refused, and the binary's own list of verbs that take `--output-format json` does not include `config get`.
4. **The OpenAI door has no off switch.** Its routes are always mounted (`presentation/httpserver/mux.cpp:32-63`: chat completions, completions, models, model status, health and the three session routes). 59a's `--devices-only` only puts it behind the device gate ([`paired-devices.md`](../v0.2.3/paired-devices.md) (Open calls: the default that `--devices-only` puts the inference plane behind the same device gate)).
5. **The app cannot hold the command line.** It carves `serve` out as "a daemon the app would orphan when it quits -- run it at a prompt, or under a service manager" (`lib/src/darwin/source/presentation/shell/ViewModels/Parity.swift:83`). A service ([62d](serve-as-a-background-service.md)) whose transport lives in its launch arguments would have to be rewritten for every change.

**What already works, and this item builds on it:**

- **One editor, locked.** Every write goes through `edit_config_file` under the config's advisory lock (`config_edit.h:500`, `:524`). `config set` and its admin twin call the same `set_scalar` (`presentation/cli/config_cmd.cpp:1509`; `presentation/httpserver/admin_config.cpp:734-768`). `config set` builds its key words, help and completion from the table (`config_cmd.cpp:1488-1512`), so a key added to the table is offered everywhere at once.
- **The flag beats the config, and can switch it off.** `choose_rag_collection` is the precedent: "the flag beats the config", and "A key that cannot be switched off for a single invocation is a key people stop using" (`presentation/operations/retrieval.h:39-47`, called by `serve` at `serve_cmd.cpp:347-350`).
- **Upgrade carries commented blocks.** `config upgrade` adds the starter config's commented options by release (`data/contracts/config_migrate.h:56-78`). A key set into a file that holds only a section's commented example lands as a live section and keeps the example. Probed: `ui.markdown` set over the admin twin landed as `"ui": {"markdown": true}` at line 535, with the commented `// "ui": {` block still at 658.
- **The loader tolerates an unknown top-level section** (probe above), so a config that gains `serve` still loads in an older binary.

## Acceptance criteria

- [ ] **Given** a sandbox home with the mock backend, **when** `apogee config set serve.port 0`, `serve.tools true` and `serve.session_ttl 5` are run, **then** each lands through the one editor with comments kept; **and when** `apogee serve` (no flags) then starts, **then** it listens on a free loopback port, with tools on and a 5-minute idle limit, and its start note names those three as coming from `config.json`.
- [ ] **Given** those stored values, **when** `apogee serve --session-ttl 30 --no-tools` runs, **then** the flags override the stored values for that run, and the file is unchanged.
- [ ] **Given** any config, with or without a `serve` section, **when** `apogee config get serve` runs, **then** it prints every key with its value; **when** `apogee config get serve --output-format json` and `config get serve.port --output-format json` run, **then** they answer the documents in Open questions, with each value's source; **when** `config get models.default --output-format json` runs, **then** it is refused by name (until 62h).
- [ ] **Given** the admin token, **when** `PUT /v1/admin/config/serve.session_ttl` is sent, **then** it lands, the same bytes the CLI would write; **when** `PUT /v1/admin/config/serve.allow_remote` with `true` comes from a non-loopback peer, **then** it is `403`, and from loopback it is `200`.
- [ ] **Given** `serve.bind 0.0.0.0` without `serve.allow_remote`, **when** `serve` starts, **then** it is refused at start with the key named; **given** `serve.allow_remote true`, **then** it starts as `--bind 0.0.0.0 --allow-remote` does today; and `config set` printed what each of those exposes.
- [ ] **Given** `serve.openai_door false`, **when** `serve` starts, **then** the inference plane's routes are unmounted (`404`), `/health` is kept, and a start note says so.
- [ ] **Given** a config seeded by a binary without the `serve` section (0.2.3 or earlier), **when** `apogee config upgrade` runs, **then** it adds the commented `serve` block and names it; `check` counts it missing before and not after.
- [ ] **Given** a malformed `serve` section, **when** `chat` and `complete` run, **then** they work, and `check` warns naming each bad key. The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user sets `serve.bind`/`serve.allow_remote` on the dev channel (`apogee-dev config set ...`), runs `apogee-dev serve` with no flags, and reaches it from a second machine on the LAN.

## Constraints

- **Remote use is an add-on (the anti-goal).** Nothing here may make standalone use need a server or an account. Only `serve` (and the service verbs of [62d](serve-as-a-background-service.md)) act on the section. Every other command loads it as data, and **a bad value there never stops `chat`, `complete`, `execute`, `task`, `acp` or the Mac app over pipes** (see Open questions). A fresh install with no keys and no models still passes `check`, and its config writes no live `serve` key.
- **One config mutation path** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → One config mutation path). Every `serve.*` write goes through `set_scalar` in `config_edit.h`, from `config set`, its admin twin and the server window alike. No `serve`-specific writer, and no load-modify-save. Comments are kept, and the edit re-parses before it lands.
- **Serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). A stored `allow_remote` or `bind` opens nothing by itself. Only `serve` reads them to listen, `httpserver/serve.cpp` stays the only object that may reference `listen`/`accept`, and the bind policy still fails closed (`bind_refusal`, `presentation/httpserver/serve.cpp:90-104`): a non-loopback bind from the config needs `serve.allow_remote` (or `--allow-remote`), exactly as a flag does.
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). The section holds no secret. `serve.tls_cert` and `serve.tls_key` are **paths** (59a's keys). The key file is never read into the config, printed, or returned by `config get`. `scalar_refusal` keeps refusing secret-shaped keys (`config_edit.cpp:3418-3433`), and the admin token stays a file beside the config, never a key.
- **Older versions keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). Every key is new and absent-safe, and its default is today's flag default, so a config without the section serves exactly what it serves today. The flags keep their names and meanings. The starter config changes only by a commented block. An older binary reading a newer config ignores the section, as it does today.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)). `config set serve.*` and `config get serve*` answer from the CLI, from the admin twin (`PUT /v1/admin/config/{key}`), and from the shell's existing `config` place (`cli/tui_parity.cpp:26`). No new verb, so no new parity row. The server window reaches them over pipes.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). The section's type and its parse sit in Data (`contracts`). The precedence (flag over config over default) sits in Presentation's `operations`, beside `choose_rag_collection`. `serve_cmd` and `admin_config` call it. No new module.
- **No silent install drift.** The starter config is `lib/src/cli/assets/config.json`, with `config_template.cpp` generated from it and held byte-identical. Every install path carries it, and `cli.install_parity` and `cli.config_lifecycle` stay green.
- **UTF-8 where it enters** (CLI CLAUDE.md). A `bind` or path value arriving over the admin twin is mended before it is stored or printed.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). A live pass runs `apogee-dev` over `~/.apogee-dev`. It never touches the release `apogee` or `~/.apogee`, and never loads a service.

## Technical approach

| File | Change |
|---|---|
| `source/data/contracts/config.h/.cpp` | `ServeConfig`, a `serve` member of `Config`. Each field is optional (absent means the default) and holds what the loader found, plus a list of problems (a bad value, an unknown key) that the loader records instead of failing. It grows 59a's `tls_cert`/`tls_key` if 59a built them (see Open questions). |
| `source/data/contracts/config_edit.cpp` | The `serve.*` rows in `settable_scalars()`, each with its kind, words or range, and its `about` line (the keys table below). `set_scalar` already writes all four kinds. A key that widens exposure carries a mark the admin twin reads (see Open questions). |
| `source/presentation/operations/serve_settings.h/.cpp` (new) | `ServeSettings resolve_serve_settings(const ServeConfig&, const ServeOverrides&)`. Every field comes out with its value and its source (`flag`, `config` or `default`), from one function, as `RagChoice` carries its source. `serve`, [62a](serve-status-and-runtime-record.md)'s runtime record and [62d](serve-as-a-background-service.md)'s service all read this one resolution. It also returns the refusals (a bad config value, a non-loopback bind without `allow_remote`), each naming the key and the file. |
| `source/presentation/cli/serve_cmd.cpp` | `ServeFlags` keeps each option's `CLI::Option*` (as `rag_option` does, `:54`), so a given flag can be told apart from its default. Booleans gain `--no-` forms (`--no-allow-remote`, `--no-tools`, `--no-devices-only`, `--openai-door`/`--no-openai-door`). The callback resolves the settings before anything binds. The start note names every value that came from the config: `-- from config.json: bind 0.0.0.0, allow_remote, session_ttl 5`. |
| `source/presentation/httpserver/serve.cpp` | `bind_refusal`'s message names where the bind came from. From the config it reads "`serve.bind` in `<file>` is `0.0.0.0` ... set `serve.allow_remote` true (or pass `--allow-remote`)". From a flag it keeps today's text. |
| `source/presentation/httpserver/mux.h/.cpp`, `handler.cpp` | `HandlerOptions` gains `openai_door`. With it off, the inference plane's routes are not mounted (see Open questions) and answer `404`, as an unmounted harness plane does under 59a's `--tls off`. `/health` stays. |
| `source/presentation/httpserver/admin_config.cpp` | The twin `PUT /v1/admin/config/serve.*` goes through `set_scalar`. It refuses a write that widens exposure from a non-loopback peer, judged from the socket's own address, as a literal `api_key` is (`admin_config.cpp:324-331`). Its `restart_required` stays `true` under a running server. |
| `source/presentation/cli/config_cmd.cpp` | `lookup` answers `serve` (every key, one per line, as an embedding entry prints) and `serve.<key>`. An unset key answers its default, as an unlisted permission answers `ask` (`:326-330`). `config get` takes `--output-format json` (see Open questions). `config set serve.allow_remote true`, or a non-loopback `serve.bind`, prints what it exposes and that it applies the next time `serve` starts. |
| `source/presentation/cli/check.cpp` | A `serve` row in the Config group. It shows defaults, the values the file sets, a non-loopback stored bind called out, and each recorded problem as a warning that names the key. |
| `lib/src/cli/assets/config.json` → `config_template.cpp` (regenerated) | A commented `// "serve": { ... }` block that teaches each key and its flag. |
| `config_migrate.cpp` | A `commented_options()` entry for release `0.2.4`, key `serve`, so `config upgrade` brings the block and `check` counts it missing. |

**The keys** (the recorded default in Open questions; each mirrors its flag, which takes exactly what the key takes and is checked by the same check):

| Key | Kind | Default | Flag |
|---|---|---|---|
| `serve.bind` | text, a host | `127.0.0.1` | `--bind` |
| `serve.port` | integer 0-65535 (0 picks a free one) | `8080` | `--port` |
| `serve.allow_remote` | boolean | `false` | `--allow-remote` / `--no-allow-remote` |
| `serve.tls` | word `auto\|on\|off` | `auto` | `--tls` (59a) |
| `serve.tls_cert`, `serve.tls_key` | text, a path | unset | `--tls-cert`, `--tls-key` (59a) |
| `serve.devices_only` | boolean | `false` | `--devices-only` / `--no-devices-only` (59a) |
| `serve.openai_door` | boolean | `true` | `--openai-door` / `--no-openai-door` |
| `serve.tools` | boolean | `false` | `--tools` / `--no-tools` |
| `serve.session_ttl` | integer minutes, 0 keeps them | `60` | `--session-ttl` |
| `serve.remote_admin` | boolean | `false` | [62g](administering-local-and-marked-devices.md)'s |

`openai_door` is the SPEC's own word: an app reaches Apogee "through either door", the OpenAI-compatible one or the harness's ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4), and the reference calls that door's routes the inference plane ([http-api.md](../../../reference/cli/http-api.md)). There is no `role` in any name: in Apogee that word means model roles.

**Reference.**

- In-house precedents:
  - `choose_rag_collection` and `RagChoice` (`presentation/operations/retrieval.h:28-47`): one function decides flag over config, an explicit flag switches the config off, and the result carries its source.
  - The 52e table of settable scalars (`config_edit.h:386-420`): one declared table that `config set`, its admin twin, the help and completion all read.
  - 55g's commented options (`config_migrate.h:50-78`): how a release teaches an option the fresh install leaves unset.
  - The literal-`api_key` loopback rule (`admin_config.cpp:324-331`) and `PUT /v1/admin/auth` (`presentation/httpserver/admin_auth_routes.cpp:72`): a write that matters more than most is accepted from the machine itself only, judged from the socket's own address.
  - 59a's `--tls off`, which leaves the harness plane unmounted with a start note saying so. It is the shape of the door-off switch.
- Prior art: llama.cpp's `llama-server`, vLLM and Ollama take their host and port from flags or environment variables (`OLLAMA_HOST`), and Jellyfin and Home Assistant keep theirs in stored settings that a restart applies. Apogee keeps both: the stored setting is the default, and the flag wins for one run.
- Siblings:
  - [62a](serve-status-and-runtime-record.md): the runtime record carries the effective settings with their sources from `resolve_serve_settings`.
  - [62b](serve-reload-and-served-vs-disk.md): reload, and what still needs a restart.
  - [62d](serve-as-a-background-service.md): the service runs `serve` with no transport flags and reads this section, gated on this item.
  - [62g](administering-local-and-marked-devices.md): what remote administering means and which devices may.
  - [62h](local-admin-reads.md): JSON reads for permissions and allowed hosts.
  - [59a](../v0.2.3/paired-devices.md): TLS, the certificate keys and `--devices-only`.
  - [59c](../v0.2.3/many-sessions-at-once.md): `--cloud-parallel` and `--max-sessions`.
  - [59d](../v0.2.3/reads-and-commands-over-the-network.md): the command door, through which a device could run `config set`.
  - GUI [7a](../../darwin/v0.2.4/app-connection-local-or-server.md): the app's side of a connection.
  - darwin [10a](../../darwin/macos/v0.2.4/design-the-server-window.md) and [10b](../../darwin/macos/v0.2.4/server-window.md): the window's "how clients connect" pane, which reads and writes this section over pipes.
- The SPECs: [CLI SPEC](../../../assistant/cli/SPEC.md) → Core surfaces 4, revised again 2026-11-14 ("The server is administered on its own Mac", "can run as a background service"). [GUI SPEC](../../../assistant/gui/SPEC.md) → Core surfaces 3, the server window ("how clients connect").

**Docs when it ships:**

- [http-api.md](../../../reference/cli/http-api.md): the flag table gains a "Stored settings" paragraph (each flag's `serve.*` key, and the precedence), the OpenAI door's off switch with the routes it unmounts, and the admin twin's loopback-only widening keys.
- The commands reference's `config set`/`config get`.
- CLI CLAUDE.md's one-config-mutation-path enforcement line names the `serve` rows.

## Testing

Hermetic: a temp `APOGEE_HOME`, the mock backend, handlers through `Mux::dispatch` with no port wherever a port is not the point.

- **`tests/data/contracts/config_test.cpp`** — absent section gives defaults; each key parses; a bad value and an unknown key are recorded as problems while the rest of the config loads; a YAML config's `serve:` reads the same.
- **`tests/data/contracts/config_edit_test.cpp` and `config_jsonc_test.cpp`** — `set_scalar` on every `serve.*` key over the comment-dense fixtures, both formats; a byte-identical round trip; a word outside its set and an out-of-range port refused, the file untouched; a file with only the commented block gaining a live section and keeping the block.
- **`tests/data/contracts/config_migrate_test.cpp`** — the block taken out of the template and upgraded back; a file that writes `serve` itself (59a's keys alone) is not counted as lacking it.
- **`tests/presentation/operations/serve_settings_test.cpp`** — the precedence matrix for every key (flag, config, default), the `--no-` forms beating a config `true`, and each source reported.
- **`tests/presentation/cli/serve_cmd_test.cpp` and `config_cmd_test.cpp`** — the start note's "from config.json" line; `config get serve` text and JSON; the exposure note on `config set`.
- **`tests/presentation/httpserver/mux_test.cpp`** — with the door off, every inference route `404` and `/health` `200`; with it on, byte-identical to today.
- **`tests/presentation/httpserver/admin_config_test.cpp`** — the twin sets a `serve` key; a widening write from a non-loopback peer `403`, and from loopback `200`.
- **`tests/presentation/cli/check_test.cpp`** — the `serve` row.
- **The spike's probe as a fixture** — the regression fixture, a phase of `cli.serve_lifecycle` (`tests/scripts/sh/serve_e2e.sh`) on the real binary, loopback, `--port 0`:
  - With `config set serve.tools true` and `config set serve.session_ttl 5`, `serve --port 0` starts with `tools: 25 (...)`, `sessions idle out after 5 min` and a `from config.json` line. `--no-tools` turns tools off for that run.
  - `config set serve.bind 0.0.0.0` without `allow_remote` makes `serve` refuse, naming `serve.allow_remote`.
  - `config set serve.openai_door false` gives `404` on `/v1/models` and `200` on `/health`.
  - Every `config set` is followed by `config get serve --output-format json` showing the value with `"source":"config"`.
- **The anti-goal** — a test drives `chat` and `complete` against a config with `"serve": {"port": "abc", "frobnicate": 1}`. Both answer, and `check` warns with both keys named.
- **Stock clients unchanged** — a conformance case shows the inference plane byte-identical with no `serve` section, with one holding every default, and with the door on.
- **The one path** — the golden files in `config_edit_test` cover every `serve.*` row in both formats, and `config_lifecycle`'s literal equality over the template holds after a `config set serve.port`.
- **Install parity** — `cli.install_parity` and the template drift test stay green with the new commented block.
- **No listening socket elsewhere** — `cli.no_listen_symbols` and `cli.complete_opens_no_listening_socket` stay green. No file but `serve.cpp` gains a socket call.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Which keys does the section hold, with what kinds and defaults? | `[default]` | The keys table under Technical approach: `serve.bind`, `serve.port`, `serve.allow_remote`, `serve.tls`, `serve.tls_cert`, `serve.tls_key`, `serve.devices_only`, `serve.openai_door`, `serve.tools`, `serve.session_ttl`, `serve.remote_admin`, each mirroring its flag, which takes exactly what the key takes and is checked by the same check. `openai_door` is the SPEC's own word, and no name uses `role` (in Apogee that word means model roles). |
| When is each key declared, given 59a's and 62g's keys? | `[default]` | **A key is declared with the behavior it sets.** 59a's four keys are added here when 59a has shipped, as expected (v0.2.3 ships before v0.2.4), and by 59a otherwise. 59a already records `serve.tls_cert`/`serve.tls_key` as config keys ([`paired-devices.md`](../v0.2.3/paired-devices.md) (Open calls: a supplied certificate comes from `--tls-cert`/`--tls-key` or the config's `serve.tls_cert`/`serve.tls_key`)). If 59a built them, they are the section's first keys and this item joins them and never moves them. `serve.remote_admin` is added by whichever of this item and 62g lands second, so the key never exists before it does something. Each is one table row and one flag line. |
| What is the precedence between flag, config and default? | `[default]` | **Precedence is flag, then config, then the built-in default**, field by field, from `resolve_serve_settings` alone. Every boolean has a `--no-` form, so a command line can turn off what the config turns on (the `RagChoice` rule). The start note names every value the config supplied, so a non-default transport is never a surprise. |
| What does a bad `serve.*` value stop? | `[default]` | **A bad value never stops anything but `serve`.** The loader records a bad `serve.*` value or an unknown key inside `serve` as a problem and loads the rest of the config. `serve` refuses to start, naming the key, the file and what it takes. `check` warns with the same words. Every other command ignores it. An unknown key is likely a newer release's key read by an older binary, or a typo, so it is warned about and never fatal (ADR 0006 in both directions). The editor still refuses a bad value before it lands, so only a hand edit can make one. |
| Is the starter config's `serve` block live or commented? | `[default]` | **The block in the starter config is commented**, as `ui` and `knowledge` are, because a fresh install leaves every key at its default. It is declared in `commented_options()` for `0.2.4`, so `config upgrade` adds it and `check` counts it missing. `config set serve.<key>` on a file with only the commented block writes a live `serve` section beside it, as `ui.markdown` does today. |
| What does turning the OpenAI door off do? | `[default]` | **The OpenAI door off unmounts the inference plane**: `POST /v1/chat/completions`, `POST /v1/completions`, `GET /v1/models`, `GET /v1/model/status` and the three `/v1/sessions` routes, along with whatever later items mount on that door (59h's `response_format`, 60c's client tools). They answer `404`. `/health` stays for 62a and 62d's liveness checks. The admin plane and 59a's harness plane are unaffected. The start note says the door is off. Under `devices_only` with the door off, it says `devices_only` has nothing to gate. Default on, which is today's behavior. |
| Does `config get` gain `--output-format json`, and for which keys? | `[default]` | **`config get` gains `--output-format json` for the `serve` keys in this item.** `config get serve --output-format json` answers `{"key":"serve","value":{"bind":"127.0.0.1",...},"source":{"bind":"default","port":"config",...}}`. `config get serve.port --output-format json` answers `{"key":"serve.port","value":8080,"source":"default"}`, the shape the admin twin's `PUT` answers. Values are JSON-typed (a number, a boolean, a string). Asked for any other key, the flag is refused by name until [62h](local-admin-reads.md) widens it. |
| The JSON documents for `permissions` and `tools.allowed_hosts`, byte for byte the admin plane's? | `(consumed decision)` | [62h](local-admin-reads.md)'s. |
| Who may make a write that widens exposure over the admin twin? | `[default]` | **Over the admin twin, a write that widens exposure is accepted from a loopback peer only**, judged from the socket's address, as a literal `api_key` is. Such writes: `allow_remote` true, a non-loopback `bind`, `tls` off, `devices_only` false, `openai_door` true, `remote_admin` true. The rest (`port`, `session_ttl`, `tools`, narrowing writes) follow the admin plane's own access ([62g](administering-local-and-marked-devices.md)). A device that may administer remotely can still close the server down, but only the server's own Mac can open it up. The CLI over pipes is local by definition and takes every write. |
| Whether a marked device may administer at all, and through which door (the admin plane or 59d's command door running `config set`)? | `(consumed decision)` | [62g](administering-local-and-marked-devices.md)'s. |
| Which keys need a restart? | `[default]` | `bind`, `port`, `allow_remote`, `tls`, `tls_cert` and `tls_key` shape the listener and always need one. The table marks them, so [62b](serve-reload-and-served-vs-disk.md) and `config set`'s note read one answer. Until 62b ships, every `serve.*` change says it applies the next time `serve` starts. |
| Whether `tools`, `session_ttl`, `devices_only`, `openai_door` and `remote_admin` apply on reload? | `(consumed decision)` | [62b](serve-reload-and-served-vs-disk.md)'s. |
| Does what the server serves move into the section too? | `[default]` | **What the server serves stays flags in this item**: `-m`/`--all-backends`, `--rag` and its siblings, `--search`, `--preload`, `--ignore-timeout`, `-v`, and 59c's `--cloud-parallel`/`--max-sessions`. The section is how clients connect and the two per-client settings named. Any of the rest becomes one more table row when an item asks for it. |
| Whether the service needs any of them stored (for example, serving every backend)? | `(consumed decision)` | [62d](serve-as-a-background-service.md)'s. |
| The effective settings and their sources in the runtime record, and `serve status` reporting them? | `(consumed decision)` | [62a](serve-status-and-runtime-record.md)'s. This item hands it `ServeSettings`. |
| How the window lays out "how clients connect"? | `(consumed decision)` | darwin [10a](../../darwin/macos/v0.2.4/design-the-server-window.md)'s and [10b](../../darwin/macos/v0.2.4/server-window.md)'s. They read `config get serve --output-format json` and write `config set serve.<key>` over pipes, and add no second path. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Specced from the admin-mode and people spike; the user's calls in the rows below shape it. | the spec |
| 2026-11-14 | **An admin mode**: a separate "Apogee Server" window in the macOS app, offered only on the Mac that serves. It drives the local CLI over pipes, so every setting it shows or changes is a CLI read or a CLI edit. | the user |
| 2026-11-14 | **The server's lifecycle**: the window installs the server as a background service (launchd) and starts, stops and restarts it. The service's settings have to live somewhere other than its launch arguments, which is this item. | the user |
| 2026-11-14 | **Remote administering is local by default.** The owner may mark chosen paired devices as allowed to administer remotely, and the admin plane is local-only otherwise. This revises 59d's recorded default that the admin plane keeps its one token for scripts and the operator ([`reads-and-commands-over-the-network.md`](../v0.2.3/reads-and-commands-over-the-network.md) (Decisions made, 2026-11-07: a paired device may not reach admin routes; the admin plane keeps its one token for scripts and the operator)), a token any network caller may present while the admin plane shares `serve`'s listener under `--allow-remote`. The on/off switch is stored here, and what it means is [62g](administering-local-and-marked-devices.md)'s. | the user |
| 2026-11-14 | **Live changes are applied on the spot (reload), and what still needs a restart is said** ([62b](serve-reload-and-served-vs-disk.md)). | the user |
| 2026-11-14 | **The standing law**: remote use is an add-on, and the CLI alone and the Mac app over pipes stay complete and need no server. | the user |
| 2026-11-14 | **People and access** (the words "people" and "access", Owner and Member, never "roles"), for a household or a small team: the owner sees members' metadata and never their chat contents (whoever controls the server Mac can still read its disk), and members use the server's local models unless the owner grants a household cloud key with a limit. These are track 63's (v0.2.4). This item adds nothing per person: the section is the server's, one per install, and it is changed only by the owner, at the server Mac or from a device the owner marked ([62g](administering-local-and-marked-devices.md)). The audit of those changes is [62f](audit-log.md)'s. | the user |
| 2026-11-14 | The SPEC amendments are already written (CLI SPEC Core surfaces 4, "Revised again 2026-11-14"; GUI SPEC Core surfaces 3, the server window). | — |
| 2026-11-14 | Placement. CLI 62c, v0.2.4, track 62, **ungated**: it needs no other pending item, since the config, the editor and the flags all exist. [62d](serve-as-a-background-service.md) and darwin [10b](../../darwin/macos/v0.2.4/server-window.md) gate on it. It sits in v0.2.4 with the server console (CLI track 62, darwin/macos track 10's 10a-10b), ahead of people and access later in the same release (CLI track 63, darwin/macos 10c), both before v0.2.5, and nothing already queued is renumbered. | the spec |
| 2026-11-14 | The flags stay, with their names and meanings, and win over the config for one run. A script or a terminal that runs `serve --port 9000` today keeps working, and a stored setting is a default, never a lock. | the spec |
| 2026-11-14 | A stored `allow_remote` is the same deliberate act as the flag: the bind policy and its refusal are unchanged, and only the message learns to name the key. Storing it moves the decision into the file the owner edits from the window, with `config set` and the start note both saying what it exposes. | the spec |
| 2026-11-18 | Moved here from v0.2.6 when the releases were merged; the id is unchanged. | the user |

## Out of scope

- Applying a change to a running server (62b's reload).
- The runtime record and `serve status` (62a).
- The service itself (62d).
- What remote administering means and which devices are marked (62g).
- JSON reads for keys outside `serve` (62h).
- Storing what the server serves (the backends, retrieval, search, preload, verbosity, 59c's lane and session limits).
- An unset verb: setting a key to its default value is the way back.
- Any environment-variable form of these settings.

## Links

- **Blocked by:** — · **Blocks:** [62d](serve-as-a-background-service.md), [darwin 10b](../../darwin/macos/v0.2.4/server-window.md)
- **Relates to:** [62a](serve-status-and-runtime-record.md), [62b](serve-reload-and-served-vs-disk.md), [62f](audit-log.md), [62g](administering-local-and-marked-devices.md), [62h](local-admin-reads.md), [59a](../v0.2.3/paired-devices.md), [59c](../v0.2.3/many-sessions-at-once.md), [59d](../v0.2.3/reads-and-commands-over-the-network.md), GUI [7a](../../darwin/v0.2.4/app-connection-local-or-server.md), darwin [10a](../../darwin/macos/v0.2.4/design-the-server-window.md); [CLI SPEC](../../../assistant/cli/SPEC.md) → Core surfaces 4; [GUI SPEC](../../../assistant/gui/SPEC.md) → Core surfaces 3; [ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0006](../../../adrs/cli/backwards-compatibility.md), [ADR 0008](../../../adrs/cli/module-map.md)
- **Revises:** 59d's recorded default that the admin plane keeps its one token for scripts and the operator, a token any network caller may present under `--allow-remote` (the switch is stored here; what it means is [62g](administering-local-and-marked-devices.md)'s)
