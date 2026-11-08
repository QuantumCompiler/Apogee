# Reads and commands over the network

**What / why.** The user's vision, 2026-11-07: Apogee as a private, self-hosted harness that feels like Claude on every device the person owns, with a strong machine serving the person's own desktops and phones. The SPEC now says `serve` carries "the reads' documents" to those devices ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4, revised 2026-11-07). On the Mac, the app gets every place's facts, and makes every change, by running the CLI's own commands as children over pipes. Machine mode says so: "There is no second protocol for mutations. A driving GUI shells out to the same commands a person uses" ([machine-mode.md](../../../reference/cli/machine-mode.md):986-998). An iPhone cannot start a child. So this item gives a paired device the same commands, run on the server.

**W4 — the GUI's places read the CLI's documents through child processes; nothing serves them to a remote device.** Measured 2026-11-07 at `4f92316d` (the branch binary, a scratch `APOGEE_HOME`, the mock backend):

1. **Every place goes through one child-process runner.** `CommandRunning` has five calls: a read (`apogee <args> --output-format json`), a command "exactly as a person would type it", the same command with stdin bytes (how a secret is passed), a report (a document whatever the exit, for `check`), and a read that keeps its stderr (`source/business/reads/CommandRunning.swift:7-22`). `ReadRunner` implements them over `ProcessSpawner` (`ReadRunner.swift:13-66`, the flag added at `:46`; its conformance `CommandRunning.swift:39`). A child's result is `CapturedRun`: exit, stdout, whether stdout was cut, a stderr tail, and whether it timed out (`infrastructure/process/CapturedRun.swift:6-15`). Long runs go through `SupervisedJob`, which reads the 52b stream from stdout and stops the run with the interrupt Ctrl-C sends (`business/jobs/SupervisedJob.swift:6-15`); every one starts through `JobStarting.startJob` (`:169-171`, `JobStarter` at `:197-230`). Counted in `source/presentation/` outside previews: 60 reads, 45 commands, 3 reports, 1 stderr-keeping read and 18 job starts, across 16 modules (the 11 sidebar places, `shell/ViewModels/Destination.swift:9-19`, plus `session`, `settings`, `compose`, `console` and `monitor`).
2. **Most of those documents have no route.** The CLI↔HTTP parity table classifies all 157 leaf commands (`tests/presentation/httpserver/parity_test.cpp:41`, table at `:75-334`): 46 twins, 10 served reads, 14 backfills not yet built, 39 carve-outs and 48 read-only. A read-only row has no network door at all. 36 of those 48 take `--output-format json` (each leaf's `--help`, re-read at HEAD); leaving out the session surfaces (`chat`, `execute`, `complete`, `tui`), that is 32 JSON reads the Mac app can run and a server cannot serve, `models list`, `chats list`, `chats show`, `embed list`, `train status` and `auth list` among them. In the probe (`config init` and one `mock` backend, `serve --port` on loopback), with the admin token, `GET /v1/admin/models`, `/v1/admin/chats` and `/v1/admin/check` each answered `404 no route for GET …`, while the same `models list --output-format json` ran locally and printed its document.
3. **A route written beside a command can answer differently.** Over the same home, with no knowledge captured, `GET /v1/admin/knowledge` answered `200 {"data":[],"object":"list"}`. `apogee knowledge list --output-format json` exited `1` with `no knowledge collection named 'knowledge' -- capture a record first with 'apogee knowledge capture'`. The table does not claim that route as the read's twin (`knowledge list` is read-only, `:316`). Even so, it shows what happens when a second implementation is written for each verb: one core, two answers.
4. **The long-work substrate covers three kinds.** Admin jobs are `graph-build`, `graph-communities` and `datasets-synth` (http-api.md:1506-1508). They live for the process and are not persisted (`httpserver/jobs.h:20-33`). `models pull`, `models convert`, `models quantize`, `embed ingest`, `datasets pull` and `datasets prepare` are backfills still waiting for routes (`parity_test.cpp:130-141`).
5. **The admin plane has one key per install.** It is a single bearer token (http-api.md:240-263). A device holding it would hold the operator's key (59a's wall; it ends there).

This item delivers **the command door**: a paired device sends the words a person types after `apogee`, and the server runs exactly that command as a child of its own binary, under its own root. The device gets back the child's stdout, its exit and its stderr tail, the same bytes `CapturedRun` holds on the Mac, so the document and the refusal are the CLI's own. A long command runs as a **job**: its stream-json events are relayed to the device, it can be cancelled with the interrupt Ctrl-C sends, it continues after the device leaves, and a device that reconnects resumes it from a cursor. A **network-door classification** gives every leaf command its door (a command, a job, 59b's session, 59e's task, or refused with its reason) and is the door's one source of refusals.

**Core constraint(s).**
- **Remote mode is an add-on (the anti-goal).** Nothing here may make standalone use need a server, a network or an account. The door is a set of routes on `serve` and nothing else. No command behaves differently because a door exists, and the classification is read only by `serve`. The Mac app's local runner and every local command are unchanged: the existing suite, run with no server, is the proof.
- **Serving = server** (CLI CLAUDE.md → Interactive turns never open a listening socket). The routes are on `serve`'s port, and `httpserver/serve.cpp` stays the only object that may reference `listen`/`accept`. A door child gets its three pipes and nothing else (`platform/child_process`, as `cli/tui_child.h:13-26` hands the shell's children), so `cli.complete_opens_no_listening_socket` gains a phase over a door child's whole tree.
- **Admitted devices only** ([59a](paired-devices.md)). Every route sits under 59a's `/v1/harness/` prefix, behind its gate, which runs before routing. An unpaired probe gets `401` on every path and learns nothing about which commands exist.
- **Same document, same refusals, by construction.** The door runs the command, never a second implementation of it (ADR 0002: "one core, many renderers"). It adds nothing to stdout. It adds its own refusals only where the classification records them, each in the classification's words.
- **Secrets are unleakable** (CLI CLAUDE.md). A device's token never reaches a child, on argv or in its environment. A literal secret is still accepted from a loopback peer only: `auth add` from a device is refused, as its twin `PUT /v1/admin/auth/{id}` is (http-api.md:1444-1452). Documents already carry views (`auth list` is metadata), and `leak_test` searches every door response for a planted key.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). The network door is a fifth door. Every leaf is classified for it, a stale row fails, and a door a command deliberately does not reach is named with its reason, never left out in silence.
- **Additive** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). New routes and new relay frames only. A job's events are the verb's own stream-json lines; the network adds `seq` and its own `net_`-named frames, 59b's convention, so stdio bytes are unchanged.
- **The four layers** ([ADR 0008](../../../adrs/cli/module-map.md)). `httpserver` includes nothing of `cli` today (`serve_cmd.cpp:27-32` includes `httpserver`; no file under `httpserver/` includes `cli/`). The door keeps that direction: it takes its runner and its classifier as values, wired by `serve_cmd`.
- **The dev channel** (CLI CLAUDE.md → Development runs on the dev channel). Probes and tests use a temporary `APOGEE_HOME` and the mock backend. A live pass names `apogee-dev serve`, never `apogee` or `~/.apogee`.

**Seam + files.**
- `source/presentation/cli/remote_parity.h/.cpp`: the network-door classification. Each leaf is `Command`, `Job`, `Session` (59b's door, named), `Task` (59e's served task) or `Refused` (its reason). It also holds `remote_refusal(words)`, the door's one source of refusals, matched by word prefix as the exec line matches (`exec_refusal`, `cli/tui_runner.cpp:54-71`, over `shell_refusals`, `tui_parity.h:68-79`), and `remote_law_violations(root)`, the law over the live registry, shaped like `shell_law_violations`.
- `source/presentation/httpserver/command_door.h/.cpp`: the routes behind 59a's gate.
  - `POST /v1/harness/run`: `{argv, stdin?, timeout_s?, cwd?}` in. `{exit, signal?, stdout, stdout_truncated, stderr_tail, timed_out, restart_required}` out, which is `CapturedRun`'s fields plus the admin plane's `restart_required`.
  - `POST /v1/harness/jobs`: `202 {job_id}`.
  - `GET /v1/harness/jobs` and `GET /v1/harness/jobs/{id}`: the records.
  - `GET /v1/harness/jobs/{id}/events?after=N`: SSE, each `data:` one stream-json line with `seq` added, `id:` the `seq`.
  - `DELETE /v1/harness/jobs/{id}`: the interrupt.
  - `GET /v1/harness/commands`: the classification itself, so a device can show a refusal before it asks.
  - The runner is a `std::function` handed in, so the handler tests need no child.
- `source/presentation/httpserver/jobs.h/.cpp`: a door job is a `JobRecord` of kind `command`, its words as its fields, so `GET /v1/admin/jobs` and `/v1/admin/events` see it. Each job gets a bounded line log beside its record, for replay.
- `source/presentation/httpserver/mux.cpp`: the route rows, so `cli.http_api_conformance` holds them to the reference.
- `source/presentation/cli/serve_cmd.cpp`: wires the door. The runner is `self_command` and `run_child_lines` (`cli/tui_child.h:30-55`) under serve's root, with serve's environment snapshot and its start folder. The classifier is `remote_parity`.
- Docs: [http-api.md](../../../reference/cli/http-api.md) gets a chapter, "The command door". [machine-mode.md](../../../reference/cli/machine-mode.md):986 gets a line: the same commands over the network. ADR 0002 gets a dated line for the fifth door. CLI CLAUDE.md's socket sub-rules get "a door child holds no socket". DEVELOPER.md gets the file rows.
- Tests (hermetic: a temporary `APOGEE_HOME`, the mock backend, no real model, no network):
  - `tests/presentation/cli/remote_parity_test.cpp`: every leaf classified; a stale row fails; each refusal has a reason; `serve`, `reset`, `uninstall`, `chats recall`, `mcp serve`, `__mcp-tools`, `__quantize` and `config init` refused; `chat`, `execute` and `complete` are `Session`; each `Job` either takes stream-json or is marked relayed; a planted unclassified verb is named.
  - `tests/presentation/httpserver/command_door_test.cpp`, over a scripted runner:
    - argv passed through verbatim;
    - root flags refused;
    - a refused word answered `403 refused_remotely` with the table's reason, the runner never called;
    - exit, signal, truncation and timeout mapped;
    - `401` from an unpaired request on every door path.
  - `tests/presentation/httpserver/command_jobs_test.cpp`, over a scripted child:
    - events in order, with `seq`;
    - replay after a cursor;
    - `net_replay_gap` past the bound;
    - cancel as an interrupt, then `130` and the stream's own `error`, then `net_exit`;
    - cancel wins over a later failure;
    - the concurrency caps.
  - `tests/presentation/cli/leak_test.cpp`: a planted key and a planted device token never appear in a door response, a job event or a log line.
  - `cli.command_door_lifecycle` (`tests/scripts/sh/command_door_e2e.sh`), the real binary under `serve --port 0` with 59a's test pairing:
    - `models list`, `chats list`, `task list` and `check` byte-identical to the local commands' stdout over the same home;
    - `knowledge list` with no collection answering exit `1` and the local stderr;
    - `config add-backend` leaving the config byte-identical to the local command's;
    - `reset` refused with the data directory untouched;
    - a `video generate` job over `video_e2e.sh`'s seeded driver, stub runtime and fake `ffmpeg`: its event types those of the local `--output-format stream-json` run, a cancel mid-run, a reconnect with `after`;
    - no socket in any door child's tree.

**Reference.**
- Siblings: [59a](paired-devices.md) (the gate, the `/v1/harness/` prefix, TLS, the device credential); [59b](machine-mode-over-the-network.md) (sessions over `/v1/machine`, `seq`, `net_replay_gap`, `--workspace`); [59c](many-sessions-at-once.md) (concurrency, a shared resident model); [59e](tasks-outlive-the-connection.md) (the served task, which takes this door's `task` words).
- Later items: [60b](../v0.2.4/device-tools.md) (a desktop's own files and shell). GUI [7a](../../darwin/v0.2.4/app-connection-local-or-server.md) gates on this item and 59b: its remote runner speaks this door, a `ChildSpawner` behind `CommandRunning`. GUI [7c](../../darwin/v0.2.4/every-place-against-a-server.md) (every place against a server).
- Precedents:
  - The shell's exec line (37f/37h): every command run as a captured child of its own binary, "the command itself, never a second implementation" (`cli/tui_child.h:13-16`), refused from one recorded list.
  - The 52b relay: long verbs' events, `document` last, `error` on failure, Ctrl-C ending in `130` (machine-mode.md:375-471).
  - The admin job registry and its "cancel wins" (`jobs.h:28-33`).
- The road not taken: `mcp serve`'s in-process reads, one function per document (`status_reads`, `cli/mcp_serve.cpp:192-289`, five `ServedRead`s). That shape works for five reads but cannot cover 157 leaf commands without rewriting them.

**Decisions made** (dated):
- 2026-11-07: Specced from the Apple-platforms spike and brainstorm. The user's calls that shape this item:
  - The vision: a private, self-hosted harness serving the person's own devices, admitting only the person and their devices. No customer accounts, billing or metering.
  - Work happens in server tools by default (call C), so a command run through this door names the server's files. A connected desktop offers its own files and shell, and when the Mac points at a server and is asked to change a project on the Mac, the Mac's files are the ones touched: that is 60b's and GUI 7b's, never this door's.
  - Remote mode is an add-on, never the only way the CLI or the Mac app runs. The CLI alone, and the Mac app over pipes to a local CLI, stay complete and the default; anything that makes them need a server, a network or an account is a failure.
  - iPhone and iPad are clients of the person's server only (no on-device core), which is why every read and command they need must cross the network. The Mac and the CLI stay standalone.
  - Successive versions, with v0.3.0 the target where macOS, iPadOS and iOS (the iPhone and the foldable iPhone) fully work.
  Track 59, v0.2.3.
- 2026-11-07: The SPEC amendment is approved and already written. The CLI's [SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4 names "the reads' documents" among what `serve` carries. GUI [CLAUDE.md](../../../assistant/darwin/CLAUDE.md) → The GUI is a machine-mode client was amended: "It is the same protocol". This item is the commands' half of that sentence.
- 2026-11-07: The door runs the command itself, as a captured child of `serve`'s own binary. It does not add a REST twin per verb. That gives the same document and the same refusals by construction, and it scales to all 157 leaves where twins reached 46. The admin plane is unchanged.
- 2026-11-07: Gated on 59a alone. Nothing runs before a device is admitted, and the door does not need 59b's session transport. It sits in v0.2.3 because GUI 7a (v0.2.4) gates on it and 59b: every place on a server reads and commands through this door.
- 2026-11-07: 59a's open question, whether a paired device may reach admin routes, is decided here: no. Every admin twin's command runs through this door as the command itself, so a device needs none. The admin plane keeps its one token for scripts and the operator.

**Open calls:**
- [user] **Training control and the other "training precedent" verbs from a device.** The HTTP table carves out training control as "CLI-only over HTTP, forever" (`parity_test.cpp:179-181`), and the CLI SPEC's scope line says "Training control is CLI-only over HTTP". The same precedent carves out:
  - the rest of `train` (`eval`, `promote`, `rollback`, `pipeline run|resume`, `regime run`, `cycle run|halt|resume`, `setup`);
  - `bootstrap`;
  - `video generate`, `video compose` and `voice say`;
  - `symphonies play`;
  - `review mr`, `review sweep` and `review targets add|delete`;
  - `config set-default-video`, carved out because only the CLI-only `video generate` reads it.

  These run the server's GPU or post through its `gh` login, on a person's act. Should a paired device run them as jobs through this door? Recommended: yes, through this door only. The verb is still the CLI's own, run by the person from their own device, and the admin plane's REST carve-outs stay as they are. The SPEC line would get a dated revision. This call blocks only these rows. Until it is answered they are `Refused` with today's reasons, and the rest of the item builds.
- [default: the routes are the ones listed under Seam. `run` takes argv exactly as a person types it after `apogee`, with `--output-format` included when the device wants a document (as `ReadRunner` adds it itself). There is no separate read route: a read is a command with the flag.]
- [default: a short command's limits:
  - 120 s by default, which a request may lower (the Mac's reads use 60 s, `ReadRunner.swift:19`);
  - stdout up to 16 MiB, flagged `stdout_truncated` past that;
  - a 64 KiB stderr tail;
  - stdin up to 1 MiB, or closed, so a command that would ask a question declines, as it does on the Mac's pipes;
  - 4 commands at once; a fifth waits within its own time limit.
  A command past its time is interrupted as Ctrl-C would and answered `timed_out: true` with what it wrote.]
- [default: a `Job` is a verb that runs for minutes:
  - pulls: `models pull`, `datasets pull`;
  - conversions: `models convert`, `models quantize`, `datasets prepare`;
  - `embed ingest`, `graph build|update|communities`, `datasets synth`;
  - the training-precedent verbs once the [user] call allows them.
  A verb that takes `--output-format stream-json` runs with it, and its lines are relayed verbatim. A long verb without it runs in text, and each line it prints on stdout is relayed as a `notice` after a `job` first line marked `relay: true`. That is the 52b relay's own rule ("every line a verb prints for people on stdout reaches the stream as a `notice`"), so the device decodes one stream. Giving those verbs their own stream-json stays their own work. Every job's stream ends with the door's `net_exit` (`code`, `signal`, `stderr_tail`).]
- [default: a job keeps its last 4,096 lines and its final `document` for its process's lifetime, as admin jobs live. A cursor older than that gets 59b's `net_replay_gap` (`from`, `to`) and then the kept lines. 2 jobs run at once, and a third is refused `busy`, naming the running ones. Cancel is the interrupt; SIGTERM follows after 10 s, as on the Mac. A stopping `serve` interrupts its jobs, and they are recorded as cancelled. A job that survives a server restart is out of scope.]
- [default: what is refused, each with its reason in the table:
  - the ground under the server: `serve`, `reset`, `uninstall`, `config init` (the shell's refusals, the user's call 2026-10-10, and the HTTP table's);
  - another program's stdio protocol: `mcp serve`, `__mcp-tools`, `acp` and its `registration`, `__quantize`;
  - a terminal: `tui`, `agents edit`, `symphonies edit` (they open `$EDITOR`; editing a server file's text from a device is GUI 7c's call);
  - never on `serve` by construction: `chats recall` (26l);
  - a literal secret from a non-loopback peer: `auth add`;
  - a pairing code: `devices pair`, never minted over the network (59a).
  `chat`, `execute` and `complete` are refused here naming `/v1/machine` (59b). `task run|resume` arrive through this door and are classified `Task`: 59e's served task runs them, and until it ships they are refused naming it. `task halt|cancel|status|list` run as commands (the request file works across processes, as 59e measured).
  This is how the door reads "anything the parity tables refuse": every exec-line refusal (`tui_parity.cpp:105-137`) is refused or routed to its own door, and every HTTP carve-out is classified with its reason, refused where the reason still holds over the network. A carve-out whose reason is only that no REST twin should exist (host-local, or task control CLI-only) runs, since the door runs the CLI itself on the person's own host; the training-precedent carve-outs wait on the [user] call above.]
- [default: the host's own facts run as commands, because the device's person owns the host: `check`, `check --fix`, `providers scan|register`, `config scan`, `config migrate|upgrade`. The HTTP table's "host-local by nature" reason means no REST twin. Here the host is the person's server, and the person asked.]
- [default: a request cannot pass root flags (`--config`, `--custom`) or environment. A child runs under serve's root, made absolute as `self_command` does, and serve's environment snapshot. It runs in the folder `serve` started in. `cwd` may name a folder under a `--workspace` folder and nothing else (59b's rule, consumed).]
- [default: `__complete` runs as a command, so a device's command line can complete from the server's live state (ADR 0007). `__machine-schema` runs too, so a device reads the schema of the build it talks to.]
- [default: `restart_required` is computed as the admin plane computes it, the config read fresh against what `serve` started with. The door does not reload anything.]
- (consumed decision: 59a) The prefix, the gate, TLS and the device credential.
- (consumed decision: 59b) Sessions over the network, `seq`, the gap frame's name and `--workspace`.
- (consumed decision: 59c) Whether a child that calls a local model shares `serve`'s resident model. Until then, a door child loads its own, as the Mac's children do today, within the caps above.
- (consumed decision: 59e) The served task behind `task run|resume`.

**Guardrail(s).** The classification law runs over the live registry, so a new command cannot ship unclassified, and a refusal cannot be written in two places. Byte-identity cases compare door output to local output over one home, so a door that reshapes a document fails. The refused-word case checks the runner is never called. Planted secrets cover the leak checks. A socket sample covers the door children's trees. The full local suite, with no server running, holds the anti-goal.

**Acceptance criteria:**
- [ ] Every leaf command has a network-door classification. A planted unclassified verb and a planted stale row each fail the law by name.
- [ ] Through the door, `models list`, `chats list`, `task list` and `check --output-format json` print byte for byte what the local command prints over the same home. `knowledge list` with no collection answers exit `1` and the local command's stderr.
- [ ] `config add-backend` through the door leaves `config.json` byte-identical to the local command's.
- [ ] `reset`, `uninstall`, `serve`, `mcp serve`, `chats recall` and `auth add` are refused `403 refused_remotely` with their reasons, and the data directory is untouched. `chat` is refused naming `/v1/machine`.
- [ ] A `video generate` job relays the same event types as the local stream-json run and ends with `net_exit`. A cancel mid-run ends it with `130` and its `error`. A reconnect with `after=N` receives exactly the lines after N.
- [ ] No door child holds a socket. No key or device token appears in a door response, an event or a log line. An unpaired request gets `401` on every door path.
- [ ] With no server running, every local command and the Mac app's local runner behave exactly as before. The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user runs `apogee-dev serve --bind 0.0.0.0 --allow-remote`, pairs a second machine (59a), and runs `models list` and a `models pull` job through the door with curl.

**Scope note.** Earmarked for v0.2.3 (track 59); gated on 59a. GUI 7a gates on it with 59b. Out of scope:
- sessions (59b);
- many sessions at once and a shared resident model (59c);
- the served task's own run and its attach (59e);
- a desktop's own files and shell (60a, 60b);
- moving a file's bytes between a device and the server: uploads, downloads and edits in place (GUI 7c, 60b);
- a job surviving a server restart;
- new stream-json on verbs that print text;
- the device side (GUI 7a).
