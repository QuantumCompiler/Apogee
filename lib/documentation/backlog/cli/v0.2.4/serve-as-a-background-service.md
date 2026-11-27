# CLI-62d · The server as a background service on the Mac

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.4 |
| **Component** | `serve service` (the server's lifecycle on macOS) |
| **Labels** | `serve` `launchd` `background-service` `macos` |
| **Epic** | Track 62 · the server console |
| **Blocked by** | [62a](serve-status-and-runtime-record.md), [62c](serve-section-in-config.md) |
| **Blocks** | — |
| **Reporter** | The admin-mode and people spike (2026-11-14), on the user's call |
| **Created** | 2026-11-14 |

---

## Summary

The Mac app's "Apogee Server" window must install the server as a background service and start, stop and restart it, but the window drives the local CLI over pipes and the CLI has no lifecycle verb. This item gives the CLI `apogee serve service install|start|stop|restart|status|uninstall`, managing a launchd user agent that runs a bare `apogee serve` with the settings kept in the config ([62c](serve-section-in-config.md)).

## Description

### User story

**As** the owner of the Mac that serves Apogee,
**I want** to install the server as a background service and start, stop, restart, inspect and remove it, from the "Apogee Server" window or from the CLI,
**so that** the server keeps running when the window quits and after a restart, logs to the data directory, and the dev channel never touches the release service.

### Background

The user's call, 2026-11-14, from the admin-mode and people spike: on the Mac that serves, the "Apogee Server" window installs the server as a **background service** and starts, stops and restarts it ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4, "The server can run as a background service on that Mac"; [GUI SPEC](../../../assistant/gui/SPEC.md) → Core surfaces 3). The window drives the local CLI over pipes, so the CLI has to own that lifecycle. This item gives it `apogee serve service install|start|stop|restart|status|uninstall`. These verbs manage a launchd user agent that runs `apogee serve` with the settings kept in the config ([62c](serve-section-in-config.md)). The agent keeps running when the window quits and after a restart, and logs to the data directory. Each install channel gets a label of its own, so `apogee-dev` never touches the release service.

This item delivers the six verbs:

- **install** writes `~/Library/LaunchAgents/<label>.plist` and hands it to launchd. Before that it checks that serve would start: the config loads, the served set builds, and the bind is allowed.
- **start / stop / restart** drive the agent. A stop lasts through the next login until a start.
- **status** reads the agent's state and 62a's runtime record: installed, enabled, running, pid, last exit, address and log path. It answers in text and as an `--output-format json` document.
- **uninstall** removes the agent.

The plist runs a bare `serve`, so how the server connects lives only in the config. Changing it is a config edit and then a restart (or [62b](serve-reload-and-served-vs-disk.md)'s reload), and the plist never needs rewriting. Off macOS the verbs exist and refuse, naming why.

### Evidence

**A7 — the app cannot own the server's lifecycle.** Re-measured 2026-11-14 at HEAD (`4acf4a2b`) against the branch binary (`apogee 0.2.0 (b37b0a9f)`), in a scratch `APOGEE_HOME` with the `mock` backend, serving on loopback with `--port 0` and killed afterwards. No probe ran `launchctl` or wrote under `~/Library`.

1. **There is no lifecycle verb.** `serve --help` names no `service`, `status`, `stop` or `install`. `apogee serve service status` and `serve service install` exit `109` ("The following arguments were not expected"). `check --output-format json` has no server or service row: the one row near it is `Admin token`. The only mentions of launchd in the CLI's source are about the person's own scheduler: the training cycle is "meant for launchd or cron; there is no daemon and no `--watch`, by rule" (`business/training/cycle.h:21-24`), and SIGTERM is handled as launchd's stop (`presentation/cli/interrupt.h:22-29`; `presentation/cli/helpers.h:61-64`).
2. **Every front end carves `serve` out because nothing would own it.** The app: "a daemon the app would orphan when it quits -- run it at a prompt, or under a service manager" (`lib/src/darwin/source/presentation/shell/ViewModels/Parity.swift:83`). The shell: the same words (`presentation/cli/tui_parity.cpp:95`, `:126-128`). The refusal matches by word prefix (`:129-130`; the matcher, `presentation/cli/tui_runner.cpp:54-70`), so any verb added under `serve` is refused with it. HTTP: `{"serve", carve_out("it is the server")}` (`tests/presentation/httpserver/parity_test.cpp:172`). 59d refuses `serve` over the network as "the ground under the server" ([`reads-and-commands-over-the-network.md`](../v0.2.3/reads-and-commands-over-the-network.md) (Open questions, the default on what the door refuses: `serve`, `reset`, `uninstall` and `config init` as the ground under the server)).
3. **What already works.** The probe ran serve the way launchd would: `PATH=/usr/bin:/bin:/usr/sbin:/sbin`, stdin `/dev/null`, `HOME` and `APOGEE_HOME` only. It served `/health` `200` and a chat `200`. SIGTERM ended it at once with exit `0`, and "stopped" appeared in its stderr and in the operational log (`presentation/httpserver/serve.cpp:110-129`, `stop_serving` and `StopOnSignals`; `presentation/cli/serve_cmd.cpp:484-485`). stdout stayed at 0 bytes, because everything serve says goes to stderr (`serve_cmd.cpp:448-450`, `:361-362` "A daemon's stderr is its log"). `logs/apogee-2026-11-14.log` recorded the start, the turn and the stop.
4. **Three hazards a service makes likely, all measured:**
   - **Two servers share one port silently.** Two `serve` processes on one root and one fixed port both printed `listening on http://127.0.0.1:54980`, and `lsof` showed 2 listeners. httplib's default socket option is `SO_REUSEPORT` (`_deps/httplib-src/httplib.h:10904-10911` in the fetched copy), and serve sets no option of its own (`serve.cpp:141`, `:189-193`). A serve started at a prompt beside the service would quietly split the requests with it.
   - **A server that fails at start would loop.** On a root with no config, serve exits `1` ("cannot open config file (run 'apogee config init' to create one)"). Under launchd's keep-alive, that is a relaunch roughly every 10 seconds, forever.
   - **Keys from the shell's environment don't reach a service.** A key reached through `${VAR}` in the entry, or through the conventional variable, is read from the process environment (`data/secrets/resolve.cpp:15`, `:98-106`, rungs at `:119` and `:138`). A launchd agent gets launchd's environment, not the shell's. 56d hit the same problem with `PATH` ("an app opened from the Dock gives its children launchd's `PATH`", `presentation/operations/acp_registration.h:24-31`).

## Acceptance criteria

- [ ] **Given** a sandbox (temp `HOME` and `APOGEE_HOME`, the fake `launchctl`, the mock backend, 62c's section serving loopback on port 0), **when** I run `apogee serve service install`, **then** it writes `<HOME>/Library/LaunchAgents/com.quantumcompiler.apogee.serve.plist`, whose arguments are the absolute binary, the root and `serve` and whose environment is exactly the fixed list; the server it starts answers `/health`, and `serve service status --output-format json` shows it installed, enabled and running, with its pid and the address from 62a's record.
- [ ] **Given** a dev-channel build, **when** it installs the service, **then** it writes `com.quantumcompiler.apogee-dev.serve.plist` and touches no release file; **and given** both are installed, **when** one is stopped or uninstalled, **then** the other keeps running.
- [ ] **Given** the service runs, **when** I run `stop`, **then** the server ends with exit 0 and "stopped" in `serve.log`, and `status` says stopped and disabled; **when** I run `start`, **then** it comes back; **when** I run `restart`, **then** it has a new pid; **when** I run `uninstall`, **then** the plist is removed and `status` says not installed; **when** I repeat any verb, **then** it exits `0` and says it was already done.
- [ ] **Given** a root with no config, or with 62c's settings asking for a refused bind, **when** I run `install`, **then** it exits `1` naming the cause, and the fake launchd records no call.
- [ ] **Given** a backend keyed only by `OPENAI_API_KEY` in the environment, **when** any of the verbs runs, **then** `install`, `start` and `restart` draw the `auth add` warning, and the key's bytes are in no plist, document or log.
- [ ] **Given** the service runs, **when** a `serve` is started at a prompt on the same root, **then** it is refused, naming the service's pid and `serve service stop`; **when** a second listener on its port starts from another root, **then** it fails "address already in use".
- [ ] **Given** the service is installed, **when** I run `apogee uninstall`, **then** its plan names the service and removes it first; **when** I run `reset`, **then** it leaves the service stopped and says how to start it.
- [ ] **Given** Linux or Windows, **when** I run any of the verbs, **then** it exits `1`, naming macOS and the person's own service manager. All six leaves are in help and completion and classified in both parity tables.
- [ ] `chat` and `complete` are unchanged, and the full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user runs `apogee-dev serve service install` on their Mac (the dev channel, `~/.apogee-dev`), logs out and back in, and sees `apogee-dev serve service status` report it serving. They note how Login Items shows the entry and whether macOS's firewall asks about incoming connections on a non-loopback bind, then stop and uninstall it. The release `apogee`, `~/.apogee` and `com.quantumcompiler.apogee.serve.plist` are untouched.

## Constraints

- **Remote use is an add-on (the anti-goal).** Nothing here may make standalone use need a server, a service or an account. Nothing installs the service unasked: not `install.sh`, `install.ps1`, `bootstrap` or `check --fix`. `chat`, `complete`, `execute`, `task`, `acp`, `mcp serve` and the Mac app over pipes never read the plist. A machine that never runs `serve service install` sees no change but six new leaves in help and completion.
- **Serving = server, never-listens** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket; SPEC → Principles; [GUI SPEC](../../../assistant/gui/SPEC.md) → No HTTP path for local work). The service runs `serve` and nothing else. `httpserver/serve.cpp` stays the only listener, and the service verbs open no socket: they write a file and run `launchctl`. The window never holds a `serve` child and never connects to the server on its own machine. It runs these verbs over pipes and reads `serve service status`.
- **Never a daemon, for everything else.** The service does not schedule tasks, training cycles or review sweeps. Those stay the person's own scheduler's (`cycle.h:21-24`; `business/tasks/runner.h:31`; `business/review/sweep.h:19`).
- **One config mutation path** (SPEC → Principles, "one config-mutation path"). The plist holds no settings. Bind, port, TLS and the rest are 62c's `serve` section, written only through `edit_config_file` under its lock (`data/contracts/config_edit.h:524`). So there is no second store to drift, and the window edits settings with `config set serve.*`.
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). A LaunchAgents plist is an ordinary `0644` file in the user's Library, so it must never hold a secret. Its environment is built from a fixed list: `PATH`, `APOGEE_HOME` when set, and the service marker. It is never copied from the process environment, so no API key or admin token can reach it. `serve.log` is created `0600` before launchd opens it, because serve's stderr carries MCP servers' bytes (`serve_cmd.cpp:361-368`).
- **Older versions keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The verbs, the plist and `serve.log` are additive. The one behavior change is that a second listener on a taken port now fails instead of sharing it (see Open questions, one listener per port). A serve at a prompt refused while the service runs is new only on a root that installed the service. It is dated in [http-api.md](../../../reference/cli/http-api.md) and the release notes.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). The CLI is the contract, and `--output-format json` gives the window its document. HTTP carves the six leaves out with their reason: host-local, the server's own lifecycle. When [62g](administering-local-and-marked-devices.md) builds, it moves `serve service restart` alone into its Administer class, for devices the owner marked; `install`, `uninstall`, `stop`, `start` and `status` stay refused over the network. The shell runs them as commands (see Open questions, the shell). Help and completion cover all six ([ADR 0007](../../../adrs/cli/tab-completion.md)).
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). `launchctl` and the plist format sit in Infrastructure (`platform`). The plan and the actions are a Presentation operation, as `acp registration`'s are. The verb sits in `cli`. No new module.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). The label comes from the channel baked into the binary (`data/contracts/paths.h:49-92`), so `apogee-dev` installs and manages only `com.quantumcompiler.apogee-dev.serve`. Tests and probes never run the real `launchctl` and never write under the real `~/Library/LaunchAgents`. A live pass uses `apogee-dev` over `~/.apogee-dev`, and the release `apogee`, `~/.apogee` and the release label stay untouched.

## Technical approach

| File | Change |
|---|---|
| `source/infrastructure/platform/launchd.h/.cpp` (new) | macOS body; elsewhere it reports "not supported". `LaunchAgent`, `render_plist`, `LaunchControl`, the real `/bin/launchctl` runner and `parse_print` (below) |
| `source/presentation/operations/serve_service.h/.cpp` (new) | `ServiceInputs`, `plan_service`, the actions over an injected `LaunchControl`, `preflight` and `service_status` (below) |
| `source/presentation/cli/serve_cmd.cpp` | `serve`'s own callback must not start a server when a `service` verb is given. At start, serve writes `managed_by` from `APOGEE_SERVICE` into 62a's record, and a serve without it refuses while the service's record is live |
| `source/presentation/cli/serve_service_cmd.h/.cpp` (new) | the verb group: `serve service install [--replace]`, `start`, `stop`, `restart`, `status` and `uninstall`, each with `--output-format json` |
| `source/presentation/httpserver/serve.cpp` | `server.set_socket_options(...)` with `SO_REUSEADDR` only, before the bind (`:141`, `:189-193`) |
| `source/presentation/cli/uninstall.h/.cpp` | `UninstallPlan` (`:121-128`) gains the channel's service beside `completions`. It is booted out and its plist removed first, and both are named in the prompt |
| `source/presentation/cli/reset.cpp` | stops a running service before it touches the root |
| `source/presentation/cli/check.cpp` | a "Background service" row (not installed, running, stopped, or stale: the plist names a binary or root that is gone). `check` only reports, and `--fix` never runs `launchctl` |
| `source/presentation/cli/tui_parity.cpp` | the `serve` refusal (`:126-128`) becomes exact for the bare word, and `serve service` is classified as commands |
| `tests/presentation/httpserver/parity_test.cpp` | six carve-out rows. The leaf walk (`:338-347`, `:364-366`) fails until they exist |

**`launchd.h/.cpp`, in Infrastructure (`platform`):**

- `LaunchAgent`: label, program arguments, environment, working directory, log path. `render_plist` turns it into XML with every string escaped.
- `LaunchControl`: an interface over `bootstrap`, `bootout`, `enable`, `disable`, `kickstart -k` and `print`, in the `gui/<uid>` domain.
- The real implementation runs `/bin/launchctl` as a captured child (`platform::run_captured`, `infrastructure/platform/child_process.h:181`). `APOGEE_LAUNCHCTL` names a substitute program, and it exists for tests only.
- `parse_print` reads only `state`, `pid` and `last exit code`, leniently: Apple documents `launchctl print`'s output as not an API, and 62a's record is the truth for "serving".

**`serve_service.h/.cpp`, a Presentation operation:**

- `ServiceInputs`: the channel, the root and how it was chosen, `platform::executable_path()` (`platform.h:99`), `PATH`, and the agents folder (`platform::home_directory()` + `Library/LaunchAgents`, `platform.h:67`).
- `plan_service` is a pure function giving the label, the plist and the paths.
- The actions take an injected `LaunchControl`.
- `preflight` runs serve's start checks: the config load `serve_cmd.cpp:248` does, the served set, and `httpserver::bind_refusal` (`serve.h:38`) over 62c's effective settings. It also lists backends whose key would come only from the environment.
- `service_status` builds the `serve.service` document.

**Reference.**

- In-house precedents:
  - `acp registration` (56d, `acp_registration.h:20-31`): the running binary's absolute path, never `argv[0]`; the root carried with it; `PATH` captured because launchd's is bare. This item writes its fields into a plist where 56d prints them.
  - 57g's SIGTERM: launchd's stop is already a clean stop (`interrupt.h:22-29`), and serve keeps its own handler (`serve.cpp:119-129`).
  - `uninstall`'s completions (57k, `uninstall.h:92-128`): a file Apogee writes outside its root, found and removed by the product rather than by two installer scripts.
  - The install channels (M10, [DEVELOPER.md → Install channels](../../../assistant/cli/DEVELOPER.md#install-channels)): one baked channel, one root, one name per channel.
  - 62a's runtime records, one per running server, named by its pid and shaped after `PidLock` (`infrastructure/platform/pid_lock.h`): how the service finds a server it did not start, and how a hand-started serve finds the service's.
- Prior art:
  - Homebrew's `brew services`: an agent plist per formula in `~/Library/LaunchAgents`, `launchctl bootstrap|bootout gui/<uid>`, and start, stop, restart and info verbs. Divergence: `brew services stop` unregisters the service unless given `--keep`, while Apogee's `stop` keeps it installed and disabled, so `status` can still say "installed, stopped".
  - `launchd.plist(5)`: `RunAtLoad`, `KeepAlive` with `SuccessfulExit`, `ThrottleInterval`, `StandardErrorPath`, `ProcessType`, and `AssociatedBundleIdentifiers` (macOS 13 and later).
  - Apple's `SMAppService` (macOS 13 and later) registers an agent that ships inside an app bundle. It is not used here. The CLI is the contract, it lives outside any bundle (`~/.local/bin`), and it must be able to install the service on its own. The window runs the CLI verb.

**Docs when it ships:**

- [http-api.md](../../../reference/cli/http-api.md): a dated revision about the port that is no longer shared.
- [machine-mode.md](../../../reference/cli/machine-mode.md): the `serve.service` document, in the reads' section and the machine schema.
- The commands reference and [DEVELOPER.md](../../../assistant/cli/DEVELOPER.md): the verbs, the labels per channel, and the test seam.
- CLI CLAUDE.md's secrets sub-rules: "a plist never holds a secret; its environment is built from a fixed list".

## Testing

All hermetic: a temp `APOGEE_HOME` and a temp `HOME`, `APOGEE_LAUNCHCTL` pointing at a fake, the mock backend.

- **No test or probe touches the real launchd.** Every test sets `HOME` and `APOGEE_LAUNCHCTL` to scratch fakes. A guard case fails the suite if either resolves to the real home or `/bin/launchctl`. A brief for this item says the shell case must never be copied or edited to point at the real ones: on 2026-11-07 a hand-edited copy of the install test ran the real `install.sh` against `~/`.
- **`tests/infrastructure/platform/launchd_test.cpp`** — the plist golden, paths with spaces, `&` and non-ASCII escaped, and the environment exactly the fixed list (the golden pins it). `parse_print` over recorded outputs: running, exited with code 1, not loaded.
- **`tests/presentation/operations/serve_service_test.cpp`** — the plan per channel, with three distinct labels; the root carried, absolute; a key exported in the test's environment absent from every byte of the plist and the document; each preflight refusal: no config, bind refused, no usable backend; the call sequence each verb makes on a recording `LaunchControl`; idempotence; `--replace` across roots.
- **`tests/presentation/cli/serve_service_cmd_test.cpp`** — each verb's text and JSON, and the refusal off macOS (run by the Linux and Windows rows of CI).
- **`lifecycle_test.cpp`** (where `UninstallPlan` is tested), **`reset_test.cpp`**, **`check_test.cpp`** and **`tui_parity`** — extended.
- **`leak_test.cpp`** — extended: gains a distinctive environment key and checks that it appears in no plist, `status` document, `check` row or log line.
- **`cli.serve_service`** (`tests/scripts/sh/serve_service.sh`) — a shell case, the spike's probe as the regression fixture, replayed through the fake launchd. Its fake `launchctl` starts the plist's `ProgramArguments` with the plist's environment and log path, and `bootout` sends SIGTERM: `install` then `status` reaches `/health` on the address 62a records; `stop` ends it with exit 0 and "stopped" in `serve.log` and in the operational log; a second `serve` on the service's port fails "address already in use".
- **The HTTP parity walk and the shell's census** — fail until the six leaves are classified.
- **`cli.install_parity`** — stays green: no file is seeded, and nothing outside the root is written by an install.
- **The anti-goal** — `chat` and `complete` run unchanged with no service, with one installed and stopped, and with an unreadable plist.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What kind of launchd job is the service, and when does it start? | `[default]` | **A launchd user agent** in the `gui/<uid>` domain, with its plist in `~/Library/LaunchAgents`. It starts when the owner logs in, not at boot. A Mac that must serve after a power cut needs automatic login, and `status` and help say so. A LaunchDaemon (at boot, installed as root) is out: it needs an administrator's password and would run outside the owner's account. |
| What is the service's label, and how many services can there be? | `[default]` | **The label is per channel**: `com.quantumcompiler.apogee.serve`, `com.quantumcompiler.apogee-dev.serve` and `com.quantumcompiler.apogee-test.serve`, after the app's bundle identifier `com.quantumcompiler.apogee` ([darwin CLAUDE.md](../../../assistant/darwin/CLAUDE.md):90). It comes from the baked channel, never from `argv[0]`, the binary's file name or `APOGEE_HOME`. The plist's file is `<label>.plist`. There is one service per channel. `install` from a root other than the one an installed service names refuses, naming both roots and `--replace`. |
| What does the plist say? | `[default]` | `ProgramArguments`: the resolved `executable_path()`, the root flag or `--custom` in force made absolute (as 56d carries it), and `serve`; nothing else, so settings stay in the config. `EnvironmentVariables`: `PATH` as the installer had it (so subscription CLIs resolve), `APOGEE_HOME` when set, and `APOGEE_SERVICE=<label>`, from which serve fills the `managed_by` field this item adds to 62a's record (under its `schema: 1`, as 62a provides), so `serve status` and `serve service status` say "started by the service". `WorkingDirectory`: the root. `StandardOutPath` and `StandardErrorPath`: `<root>/logs/serve.log`. `RunAtLoad` true. `KeepAlive` `{SuccessfulExit: false}`: a stop (SIGTERM, exit 0) stays down, and a crash or a failed start comes back. `ThrottleInterval` 10, launchd's default made explicit. `ProcessType` `Interactive`, so inference is not throttled as a background job's would be; the live pass compares tokens per second against a serve run at a prompt, and it reverts to launchd's default if there is no difference. `AssociatedBundleIdentifiers` `com.quantumcompiler.apogee`, so System Settings' Login Items can show the entry under Apogee (observed on the live pass, not assumed). The plist is written atomically, `0644`. |
| How do the verbs map onto launchd? | `[default]` | Each is idempotent: an act already done says so and exits `0`. `install`: preflight, write the plist, `bootstrap`; it runs at once. `start`: `enable`, then `bootstrap`, or `kickstart` when already loaded. `stop`: `bootout`, then `disable`, so it stays stopped through the next login until `start`. `restart`: `kickstart -k`. `uninstall`: `bootout`, remove the plist, `enable` to clear the override; it keeps `serve.log`, which the data directory's own removal handles. |
| What is checked before launchd is called? | `[default]` | **Preflight before launchd.** `install`, `start` and `restart` run serve's own start checks in-process: the config loads, the served set builds, and `bind_refusal` passes on 62c's effective settings. A failure is refused with exit `1`, naming the cause, and launchd is never called, so a server that would exit `1` at once is never handed to keep-alive. A backend whose key would come only from the environment (`resolve.cpp` rungs 1 and 3) is a warning, not a refusal: "`openai`'s key comes from `OPENAI_API_KEY` in this shell; a background service does not see it -- `apogee auth add openai` stores it." A key is never copied into the plist. |
| How does the service share a root and a port with other servers? | `[default]` | **The service is the root's only server while it runs, and one listener per port.** [62a](serve-status-and-runtime-record.md) keeps two hand-started servers on one install legal, as today, and lists each (its default). This item narrows that for the service alone, through 62a's records rather than a lock of its own: while a record names a live serve the service did not start, `start` and `install` refuse, naming its pid, its address and how to stop it; and a serve run at a prompt while the service's record (`managed_by` this label) is live is refused the same way, naming the service's pid and `serve service stop`. A root with no service installed behaves exactly as today. Separately, serve sets `SO_REUSEADDR` in place of httplib's `SO_REUSEPORT`, so a second listener on a taken port, from any root, fails "address already in use" instead of sharing it. That revision is dated in http-api.md under ADR 0006. |
| Where does the service log, and how is the log kept? | `[default]` | **`serve.log`** is one file, pre-created `0600`. `install`, `start` and `restart` roll it to `serve.log.1` once it passes 10 MiB. The operational log is unchanged. `status` gives its path, and when the last run failed, its last 5 lines. |
| What does `status` report, and what do the other verbs print? | `[default]` | **`status`** answers in text and as the `serve.service` document: the label, channel, root, binary and plist path; whether it is installed, enabled, loaded and running, with its pid and last exit; from 62a: the address, the start time, and the version running against the version installed; `problems[]`: stale plist, binary gone, restarting after a failure, the version differing ("running 0.2.4, installed 0.2.5 -- restart to apply"). The other verbs print one line, and with `--output-format json` they print the same document after the act. An update replaces the binary in place, and nothing restarts the service on its own: the window offers Restart. |
| What do `uninstall` and `reset` do with the service? | `[default]` | `apogee uninstall` (and `make uninstall MODE=dev`, which runs that binary's own uninstall) removes that channel's service first, named in its plan. `reset` stops a running service before it rebuilds the root, leaves it installed and stopped, and says `apogee serve service start` brings it back once a backend is configured. |
| How does the shell treat `serve service …`? | `[default]` | The exact-word refusal of bare `serve` lets `serve service …` (and 62a's `serve status`) run as commands. Whichever of 62a and this item builds first narrows it, and the other consumes it. |
| Lifting or narrowing the app's carve-out (`Parity.swift:83`) so the server window runs `serve service`, and how its buttons read | `(consumed decision)` | Darwin [10b](../../darwin/macos/v0.2.4/server-window.md)'s, drawn first by [10a](../../darwin/macos/v0.2.4/design-the-server-window.md). |
| A development build of the app drives `apogee-dev`, not the release binary it found first before M18 ([darwin CLAUDE.md](../../../assistant/darwin/CLAUDE.md#development-runs-on-the-dev-channel)) | `(consumed decision)` | M18's, shipped 2026-11-21 ([Milestone A](../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel)). This item's half is the label coming from the binary's own channel. |
| Recording `install`, `start`, `stop`, `restart` and `uninstall` as acts with their actor; which lifecycle verbs a network caller may run | `(consumed decision)` | Recording the acts is [62f](audit-log.md)'s. Which verbs a network caller may run is [62g](administering-local-and-marked-devices.md)'s and 59d's ([`reads-and-commands-over-the-network.md`](../v0.2.3/reads-and-commands-over-the-network.md) (Open questions, the default on what the door refuses: `serve` refused as the ground under the server)): 62g's default admits `serve service restart` for marked devices and keeps `install`, `uninstall` and `stop` refused; this item carves all six out until 62g builds, and adds `start` and `status` to the refused (host-local: they act on or read this Mac's launchd). |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Specced from the admin-mode and people spike. The user's calls: (1) An admin mode: a separate "Apogee Server" window in the macOS app, offered only on the Mac that serves, driving the local CLI over pipes. (2) The window installs the server as a background service (launchd), and starts, stops and restarts it. (3) Remote administering is local by default, and the owner may mark chosen paired devices as allowed to administer remotely. This revises 59d's default that the admin plane keeps one token for any network caller; otherwise the admin plane stays local-only ([62g](administering-local-and-marked-devices.md)). The service changes nothing here: it runs the same `serve`, behind the same doors. (4) Placement, both before v0.2.5 and both in v0.2.4: the server console (CLI track 62, darwin/macos track 10's 10a and 10b), people and access (CLI track 63, darwin/macos 10c), nothing already queued renumbered. (5) People: a household or a small team, designed for both. The words are "people" and "access" (Owner, Member), never "roles", which in Apogee means model roles. The owner is implicit: the OS account that owns `APOGEE_HOME` on the server Mac. Each member has an allowance in 59f's scope shape, from presets (Full, Family, Limited) rather than groups, and a guest is a member on Limited with nothing kept. The owner sees members' metadata (devices, apps, activity, which model, when), never chat contents; whoever controls the server Mac can still read its disk, and the docs say so. Members use the server's local models only, unless the owner grants a household cloud key with a limit. All of it is track 63's ([63a](people-on-one-server.md)); this item adds nothing per person. (6) The defaults the user took: live changes applied on the spot (reload), with what still needs a restart said ([62b](serve-reload-and-served-vs-disk.md), whose "needs a restart" leads to this item's `restart`); a 90-day audit of who, what and when without request contents ([62f](audit-log.md)); collections as shared libraries handed out per person plus a member's own private ones; notes and saved decisions personal by default; members' apps paired by the owner at the server Mac, capped at that member's allowance; a per-person task queue rather than a refusal. (7) The standing law: remote use is an add-on, and the CLI alone and the Mac app over pipes stay complete and need no server. (8) The SPEC amendments are already written: CLI SPEC → Core surfaces 4 ("Revised again 2026-11-14"), GUI SPEC → Core surfaces 3 (the server window). | the user |
| 2026-11-14 | CLI 62d in v0.2.4, gated on [62a](serve-status-and-runtime-record.md) and [62c](serve-section-in-config.md): `status` reads 62a's record for the address, the start time and the build running. The plist runs a bare `serve`, whose settings are 62c's section, so the service has no settings of its own. Darwin [10b](../../darwin/macos/v0.2.4/server-window.md) is gated on 10a, 62a, 62c and 62h, not on this item. Its Install, Start, Stop, Restart and Remove run these verbs once they exist; until then the window says to run `apogee serve` at a prompt, as [10a](../../darwin/macos/v0.2.4/design-the-server-window.md) draws it. | the spec |
| 2026-11-14 | The service runs as the OS account that owns the root, in that account's session. That is the people plan's implicit owner ([63a](people-on-one-server.md)). Apogee never asks for an administrator's password. | the spec |
| 2026-11-14 | Linux and Windows equivalents (a `systemd --user` unit, a Windows service) are out of scope. The verbs are declared on every platform, so help, completion and the parity tables match, and off macOS they refuse. | the spec |
| 2026-11-18 | Moved here from v0.2.6 when the releases were merged; the id is unchanged. Earmarked for v0.2.4 (track 62), gated on 62a and 62c. | the user |

## Out of scope

- Linux (`systemd --user`) and Windows service equivalents. The verbs refuse there.
- A boot-time LaunchDaemon, or a service run as another account.
- Keeping the Mac awake: its energy settings are the owner's.
- Restarting the service automatically after an update.
- Reload ([62b](serve-reload-and-served-vs-disk.md)).
- The window's screens and buttons (darwin [10b](../../darwin/macos/v0.2.4/server-window.md)).
- Scheduling tasks, training or review sweeps, which stay the person's own scheduler's.

## Links

- **Blocked by:** [62a](serve-status-and-runtime-record.md), the runtime record and `serve status`; [62c](serve-section-in-config.md), the settings the service runs with · **Blocks:** —
- **Relates to:**
  - [62b](serve-reload-and-served-vs-disk.md), reload.
  - [62f](audit-log.md), the audit log.
  - [62g](administering-local-and-marked-devices.md), administering locally.
  - [63a](people-on-one-server.md), people on one server (the implicit owner).
  - [59a](../v0.2.3/paired-devices.md): its TLS identity and registry sit under the config and are read by the service's serve unchanged.
  - [59d](../v0.2.3/reads-and-commands-over-the-network.md): `serve` is refused over the network.
  - darwin [10a](../../darwin/macos/v0.2.4/design-the-server-window.md), drawing the window.
  - darwin [10b](../../darwin/macos/v0.2.4/server-window.md), which drives these verbs.
  - darwin [7a](../../darwin/v0.2.4/app-connection-local-or-server.md), the client side.
  - darwin M18 [Milestone A](../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel), so a development build of the app drives `apogee-dev`.
  - [SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4; [GUI SPEC](../../../assistant/gui/SPEC.md) → Core surfaces 3.
