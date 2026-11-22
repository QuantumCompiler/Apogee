# Apogee Mod — Developer Reference

The architecture reference for the Claude plugin. The contributor entry point is [CLAUDE.md](CLAUDE.md); the contracts the mod consumes are the CLI's ([machine-mode.md](../../reference/cli/machine-mode.md), the 28h JSON documents, and `apogee mcp serve`, public since the CLI's 54a, 2026-11-01 -- its tools in [tools.md](../../reference/cli/tools.md#apogee-as-an-mcp-server-apogee-mcp-serve)).

---

## Layout

As built by the scaffold (1a, 2026-11-21) and grown by the tools, slash commands and status line (1b, 2026-11-21) and the pane (1c, 2026-11-21; [MILESTONES](MILESTONES.md#milestone-a--the-mod-is-born)), per [ADR 0001](../../adrs/mod/layer-enforcement.md) (four layers, dependencies down; a package is a folder `src/<layer>/<package>/`, its tests at `tests/<layer>/<package>/`):

```
lib/src/mod/
  .claude-plugin/plugin.json          the manifest: name apogee, version, engines (claude-code >=2.1.326),
                                      types, and the options binary and channel
  hooks/hooks.json                    names the hooks module: ./register.tsx
  hooks/register.tsx                  the composition root: every on(...) (session.start, command.run for
                                      /apogee, ui.render for the pane, tool.run for mcp__apogee__*), the options
                                      read once, $ handed down
  types/index.d.ts                    the contract: the $.state values (apogee.binary, .tools, .jobs, .status,
                                      .pane) and the document shapes the pane keeps
  src/infrastructure/discovery/       finding the binary: candidates, locate, the install hints
  src/infrastructure/runner/          the process runner: runBinary (process.run), superviseBinary (process.spawn)
  src/infrastructure/timer/           $.clock wrapped: now, after, every
  src/data/documents/                 the documents, tolerant: check, models list|status, train status, train
                                      versions, the MCP server's frames, config get's one value
  src/data/state/                     the atoms: binary (binary-1), tools (tools-2), jobs (jobs-1), status (status-1),
                                      pane (pane-1)
  src/business/binary/                findBinary: the answer kept, the absence said once a session
  src/business/client/                the session's binary; readDocument, a command's document through the runner
  src/business/commands/              the /apogee set: plan (argv per word), reads, the confirmed actions
  src/business/served/                apogee mcp serve, one exchange a run: listServed, callServed
  src/business/surfaces/              what the session start offers: the served tools, apogee_complete's offer;
                                      keepRegistered, what the engine took
  src/business/complete/              apogee_complete: the offer, each backend's bound, the spend checks, the
                                      one-shot's argv, the served path
  src/business/jobs/                  the async law: launchJob (a 30 s window, then a job), detachJob (a job
                                      from its start, begun from a timer), the lost jobs
  src/business/status/                the status line's reads and its poll
  src/business/pane/                  the pane's reads (serialized, kept), its visibility-scoped poll, and what it
                                      offers: Promote's runs and backends, Roll back's ledgers
  src/presentation/notices/           the not-installed toast
  src/presentation/session/           the session start, in order
  src/presentation/commands/          /apogee: registered, its documents formatted; confirm.ts, the one shown-command
                                      confirmation, shared with the pane
  src/presentation/tools/             the served tools, apogee_complete and apogee_job: registered, answered
  src/presentation/status/            the status line's words
  src/presentation/pane/              the pane: opened and looked at (open.ts), its words (words.ts), its drawing
                                      and its presses (pane.tsx)
  tests/support/                      machine.ts (the scripted machine: commands, answers, the panes, the mocked
                                      clock), state.ts (the state, watched), apogee.ts (/apogee and the tools
                                      driven), pane.ts (the pane mounted on each surface, its words and buttons),
                                      recorded.ts (recorded documents), handmade.ts (hand-made ones)
  tests/<layer>/<package>/*.test.ts   claude plugin test's hermetic tests
  tests/architecture/layering/        the layering check (check.mjs) and its planted violations, run by node
  tests/pipeline/changed.test.mjs     changed.sh mod's proof, run by node
  vendor/claude-code/index.d.ts       the 2.1.326 API declarations, vendored unmodified for tsc
  package.json, package-lock.json     the checks' toolchain: typescript 6.0.3 and the npm scripts
  tsconfig.json                       the API header's options; includes vendor/claude-code, hooks, src, types, tests
```

Not in git: `node_modules/`, and `.claude-plugin/types/`, the declarations the engine lays beside the mod each time it loads it from a folder. The mod's tsconfig never includes that folder, so a loaded mod's two copies do not collide.

**The layers' nouns.** The layering check holds each `$` noun to one layer: `process`, `fs`, `env` and `clock` (since 1b) to Infrastructure; `state`, `store` and the atom library (`atom`, `derive`, `memberOf`, `read`, `update` from `'claude-code'`) to Data; `ui`, `command`, `tool` to Presentation. `http` is refused everywhere (the client invariant), and the composition root and the contract reach none. The atom library counts however it is taken outside Data: imported or re-exported by name, or `'claude-code'` whole (`import * as`, `export *`). Its other rules: an upward or undeclared edge fails by name, nothing comes from outside the mod but `'claude-code'`, no `import()`, every file at `src/<layer>/<package>/`, every layer with a package and every package with its tests, and an empty graph refused. Outside `src/`, `hooks/` holds `register.tsx` alone and `types/` holds `.d.ts` files alone, importing nothing of the mod but each other; a JavaScript file in a layer is refused, since the engine loads it and the check cannot read it (ADR 0001's Landed note has the full list).

**Discovery** (`src/infrastructure/discovery/`), the macOS app's ladder mirrored (`lib/src/darwin/source/data/discovery/BinaryDiscovery.swift`):

| `channel` | Looked at, in order |
|---|---|
| `release` (the default) | the `binary` override, `~/.local/bin/apogee`, `~/.local/bin/apogee-dev`, `~/.local/bin/apogee-test` |
| `dev` | the `binary` override, `~/.local/bin/apogee-dev` — never the release binary |

A leading `~/` in the override is the home folder (`$.env.get('HOME')`). A place that does not exist is `not installed` and is not run; one that cannot be looked at (`$.fs.exists` rejects, as it does for a network location) is passed over with why, and the ladder goes on. Each one that exists is run as `<candidate> version` (10 s) and must print `apogee <version> …` first; a `channel:` line is kept as the binary's reported channel. Every place passed over keeps its reason. None answering is `{ kind: 'not-found', skipped }`, said once a session as a 10-second toast naming every place and why, then the install line: the README's `curl … install.sh | bash`, or `make -C lib/src/cli install MODE=dev` on the dev channel. Discovery runs awaited in `session.start`, after the engine's own start.

## The surfaces (1b)

Set up at `session.start`, awaited, after discovery: `/apogee` is registered whatever discovery found; with a binary, the server is listed, `apogee_complete` offered or not, the tools registered, and the status line's poll started. Each tool is registered on its own: one the engine refuses is toasted once and said by `/apogee`, the rest are registered all the same, and the session starts regardless. Every run is the binary discovery found, so a development load on the `dev` channel drives `apogee-dev` alone.

**The tools** (`mcp__apogee__<name>`, behind ToolSearch as an MCP server's are):

| Tool | What answers it |
|---|---|
| Every tool `apogee mcp serve` lists but `complete` (22 on 0.2.0: the native reads and the five status reads), by its own name, description and schema | One run of `<binary> mcp serve` a call: the handshake (`initialize`, `notifications/initialized`) and one `tools/call` on its stdin, the reply read back from its stdout (the CLI's own client's way, `lib/src/cli/tests/scripts/py/mcp_serve_client.py`), within 120 s. The server's error result is the call's error, in its words. |
| `apogee_complete` (`prompt`, `backend`, `max_tokens`), offered only for a local, unmetered backend and saying so | Where the listing names the served `complete` (the person's config lists it under a backend's `mcp_serve:`): that tool, its backends its schema's `enum`, each at the bound the server's description says (`<name> (at most N tokens)`; the schema's `maximum` is only the widest), a bound left out never sent so the server holds each backend to its own. Otherwise the configured `llamacpp` and `mlx` backends of `models list` that chat (no `kind`, or `chat` or `vision`), only where `models status` names no active suite and a utility model that is unset or local and unmetered (the one-shot calls those, the served `complete` never does). Each is held to 1024 tokens a call, or its entry's own `max_tokens` when lower (`config get backends.<name>.max_tokens`): `<binary> complete -m <backend> -n <bound> --rag "" -- <prompt>` (retrieval off as the served complete has it; the prompt after `--`). A call runs within ten minutes on either path. With neither, it is not registered and `/apogee` says why. |
| `apogee_job` (`id`) | The job kept in `apogee.jobs`: still running and for how long, its answer or what it printed, why it failed, or lost (a copy of the mod that is gone left it running). Runs nothing. |

The listing is read once a session start (10 s); the names the mod registered are kept in `apogee.tools`, and the root's `tool.run` hook answers those alone, passing on any other `mcp__apogee__` name.

**The `/apogee` set** (`lib/src/cli/source/presentation/cli/` for each argv):

| Typed | Runs | Shows |
|---|---|---|
| `/apogee` (or `help`) | nothing | the binary driven (path, version, channel), the places passed over, the served tools and `apogee_complete` as offered, the set |
| `/apogee check` | `check --output-format json` | every row by section with its status and remedy, and the counts (the report is read whatever the exit status) |
| `/apogee models` | `models list --output-format json` | each backend and stored model by section, with its type, model, state and note |
| `/apogee train status` | `train status --output-format json` | what runs, the newest pipeline, the cycle, each active version, the recent runs |
| `/apogee promote <run> --as <backend>` | `train promote <run> --as <backend>`, after the yes | what it printed, or its status and last words |
| `/apogee rollback <backend>` | `train rollback <backend>`, after the yes | the same |
| `/apogee pane` | nothing itself: opens the pane (below), which reads | `Apogee's pane is open.`, or why it is not drawn yet or could not be opened |

A field a document lacked is said under what is shown. Promote and rollback ask first (`$.ui.ask`: the exact command, `Run it` or `Cancel`); anything but `Run it`, a dismissed dialog included, runs nothing and says so. Neither CLI command has a `--yes`, so the confirmation is the one gate, and no `--yes` is ever passed. They run supervised (`$.process.spawn`), with no time of their own, each a job from its start: the command answers the job's id at once, and the run is begun from a `$.clock` timer, outside the command's dispatch, since the API ends a spawn when the dispatch that started it is abandoned. A toast says its end, and the status line is read back then. With no binary every word says where the mod looked and the install line.

**The async law** (`src/business/jobs/`): a completion gets a 30-second window on `$.clock`. Ended inside it, it answers there. Otherwise it goes on as a job (`complete-1`): the call answers the id at once, a 10-second toast says when it ends, and its outcome is kept for `apogee_job`. A confirmed promote or rollback is a job from its start (`promote-2`, `detachJob`, above). The newest 20 jobs are kept, the running ones always. A session start in a fresh copy of the mod marks the jobs an earlier copy left running lost, since their ends can no longer be heard.

**The status line** (`$.ui.status`): `Apogee: <backend> v<version> · <run state>`, `Apogee (dev)` on the dev channel. The backend is `models status`'s `chat` role (`no chat backend` when it reads `(unset …)`), the version that backend's `active_version` in `train status`'s `versions` (`(no promoted version)` without one), the run state `idle` or `training: <n> runs, pipeline <spec>, cycle pass` from `running`, `active_pipeline` and `cycle_active`. Read at the start, then once a minute (two read-only runs), and after a confirmed command ends; never two at once, each read waiting for the one before it, so a read-back lands after any poll read begun before the command wrote. When the latest read failed, or the reading is two minutes old, the line keeps what it last read and adds `· stale: read <age> ago; <why>`; before any reading it says `status unread: <why>`. Fields the documents lacked are said (`· lacked: …`). With no binary there is no line.

## The pane (1c)

`/apogee pane` opens it: `$.ui.open({ id: 'apogee', title: 'Apogee' })`, the person's command, so it is placed at any width (docked beside the transcript in the desktop app and the fullscreen terminal, inline otherwise); nothing opens it unasked. The root's `ui.render` hook on `{ component: 'Pane', requestId: 'apogee' }` draws it (`drawPane`), its `.catch` drawing the fault in its place. It opens whatever discovery found: with no binary it shows the absence toast's words and reads nothing.

**What it shows**, every value from a document, a line of text a row with its buttons after it (`words.ts`):

| Section | From | Lines |
|---|---|---|
| The heading | the binary found, `apogee.jobs` | `Apogee <version> · <channel> channel` and **Read again**; each confirmed command still running |
| Backends and models | `models status`, `models list` | the roles, `field: value`; the rows by section (`backend · type · model · state`, an MLX row's kind beside its type), each note under its row |
| Training | `train status` | what runs, the newest pipeline, the cycle (the lines `/apogee train status` says: a history it could not read as `Cycle: history unreadable -- <path>: <reason>`, a dead pass's lock as `stopped -- cycle.lock is left; remove <path> to run again`, said once as the cycle's state or on a `Cycle lock:` line of its own); the recent runs, each with its gate where gated, **Promote** on a `complete` one |
| Version ledger | `train versions` | the document as-is: each ledger and its active version, **Roll back** where a version below it is listed; each version with every field it carries |

Each document not read yet is `Reading <document>…`; one that failed is the command and why; one that failed after a good read keeps it, `Shown as read <age> ago; the latest read failed: …`; a field it lacked is `<document> lacked: <fields>`. Empty is said: `No backends configured and no models stored.`, `No backend is configured.`, `Nothing trained yet: no runs, no pipeline, no cycle.` (only when the document names no cycle history, readable or not, and no lock), `No version promoted yet.` As a widget (the desktop app's minimized form) it is one line, `Apogee: <what runs> · <each active version>`, with no buttons: a document not read yet is `training unread` or `ledger unread`, one that failed with no good read before it is `training read failed` or `ledger read failed`, and a good read kept past a later failure carries its age, `(read 10 min ago; the latest read failed)`.

**Its buttons.** **Promote** opens a pick of the configured `llamacpp` and `mlx` backends `models list` names, each once, and **Cancel**; a backend not listed is `/apogee promote <run> --as <backend>`, said under the pick. With no `models list` to pick from, the pick says which: `models list is still being read, so no backend can be picked yet` or `models list gave no document, so no backend can be picked`, never that it names none; a good read kept past a later failure still offers its backends. A backend picked, or **Roll back**, asks as `/apogee` does (`src/presentation/commands/confirm.ts`, shared): `$.ui.ask` with the exact command, `Run it` or `Cancel`, the argv `plan.ts`'s `promotePlan` and `rollbackPlan` make. Only `Run it` runs it, supervised, as a job from its start; its answer is toasted, and its end too.

**Its reads** (`src/business/pane/`): all four documents when it opens (begun from a `$.clock` timer, outside the command's dispatch), on Read again, and after every confirmed command ends while it is open, the pane's and `/apogee`'s alike (read back, never mirrored). Every read of the pane waits for the one before it. **The poll**: `train status` every 15 s, one read-only run, only while the pane is shown: before each tick's read `$.ui.panes()` must list it open, placed and the tab in front. A hidden tick reads nothing; a closed pane ends the poll; a read still running is not started twice; one poll a copy. A session start resumes a pane the engine kept open across a reload. The state is `apogee.pane` (shape `pane-1`): each document as last read, and the run whose backend is being picked.

**Its tests** loop over `terminal` and `desktop` (`tests/support/pane.ts`'s `SURFACES`, as reference.md advises), the machine answering `$.ui.open` and `$.ui.panes()`; a test hides the pane by its row or closes it by emptying the list.

## The contracts the mod builds against

- The `--output-format json` documents (models list|status, train status, train versions, check, …) — measured live by the mod spike (2026-10-21); `train versions` is the ledger shape http-api.md's `GET /v1/admin/training/versions` documents, read by the pane since 1c; the tests read the recordings (`lib/src/darwin/tests/business/reads/Fixtures/`, copied into `tests/support/recorded.ts`).
- `train promote <run> --as <backend>` and `train rollback <backend>` — text out, no `--yes` and no question of their own (`train.cpp`).
- Machine mode, for anything session-grade later.
- `apogee mcp serve` (the hidden `__mcp-tools` its alias) — the MCP server, reached one exchange a run (above): 22 read-only tools since the CLI's 54a, the spike's 17 plus the five status reads (`models_list`, `models_status`, `train_status`, `train_versions`, `check`), each answering with its command's JSON document byte for byte; since the CLI's 54b also `complete`, the one tool not read-only, listed only where the person's config lists it under a local backend's `mcp_serve:` (absent otherwise -- the mod's fallback is the CLI path); the registration JSON printed by `apogee mcp presets`.

## Build & test

From `lib/src/mod/`, with node 26 and npm (Homebrew's, at `/opt/homebrew/bin`):

```sh
npm ci                                   # TypeScript 6.0.3 into node_modules/, from the lock file
npm run check                            # the five below, in order; exit 0 when the mod is sound
npm run typecheck                        # tsc -p . (against vendor/claude-code, no engine needed)
npm run layering                         # the layering check over the tree: "layering: N files and M imports hold ADR 0001"
npm run test:node                        # node --test "tests/**/*.test.mjs": the check's planted violations, changed.sh mod's proof
npm run validate                         # claude plugin validate --strict .
npm run test:plugin                      # claude plugin test .: every tests/**/*.test.ts(x), hermetic
```

`validate` and `test:plugin` run `$CLAUDE`, or `claude` on PATH when it is unset, and need **Claude Code 2.1.326 or newer**: an older CLI refuses the layered module (2.1.289: `$ is followed only into a function declared in this same file, never across an import`). Point `CLAUDE` at a newer binary when the one on PATH is older, for example the desktop app's engine: `CLAUDE="$HOME/Library/Application Support/Claude/claude-code/<version>/<build>/claude.app/Contents/MacOS/claude" npm run check`. `test:node` builds throwaway commits in a `git clone --shared` under `$TMPDIR`, never in this checkout, and removes it after.

Adding a package to the toolchain: `npm install --save-exact <name>` (only what the mod's checks need; the plugin itself imports nothing from npm). Moving the engine floor: replace `vendor/claude-code/index.d.ts` whole with the new build's `claude-code.d.ts` (its first line names the build), raise `engines` in plugin.json, and run `npm run check`.

**The pipeline** is 1d's: `changed.sh mod` (`lib/src/mod/`, `lib/scripts/changed.sh`, `.github/workflows/mod.yml`) answers whether a change needs it, and its workflow will call these same scripts (`npm ci && npm run typecheck` on runners, which need no engine; validate where the `claude` CLI installs headlessly; `claude plugin test` a dev-host gate).

### A development load drives apogee-dev

`claude --plugin-dir lib/src/mod` (Claude Code 2.1.326 or newer) loads the working copy for that session and watches it, so a save reloads the hooks module. In the desktop app, whose engine is the 2.1.326 build, the same folder loads through `CLAUDE_CODE_PLUGIN_DIRS` (an absolute path) in the process environment or in the `env` block of `~/.claude/settings.json`.

A development load must drive `apogee-dev` over `~/.apogee-dev`, never the production `apogee` (the dev channel law, [CLAUDE.md](CLAUDE.md#working-process)). Discovery's `release` default would find `apogee` first, so before a live pass set the plugin's `channel` option to `dev`, either from `/config` in the development session (each option is a row there, and a change reloads the module) or in `~/.claude/settings.json`:

```json
{ "pluginConfigs": { "apogee@inline": { "options": { "channel": "dev" } } } }
```

A `--plugin-dir` load is keyed `apogee@inline` (or `apogee`), apart from an installed `apogee@<marketplace>`, so this setting never reaches a production install of the mod. On `dev`, discovery tries the `binary` override and `~/.local/bin/apogee-dev` (`make -C lib/src/cli install MODE=dev`) alone. With neither there, the toast names the dev install, and the release binary is never run. Set `binary` too only to drive a particular dev-channel build: a binary of another channel would run over that channel's home.
