# Apogee Mod — Developer Reference

The architecture reference for the Claude plugin. The contributor entry point is [CLAUDE.md](CLAUDE.md); the contracts the mod consumes are the CLI's ([machine-mode.md](../../reference/cli/machine-mode.md), the 28h JSON documents, and `apogee mcp serve`, public since the CLI's 54a, 2026-11-01 -- its tools in [tools.md](../../reference/cli/tools.md#apogee-as-an-mcp-server-apogee-mcp-serve)).

---

## Layout

As built by the scaffold (1a, 2026-11-21; [MILESTONES](MILESTONES.md#milestone-a--the-mod-is-born)), per [ADR 0001](../../adrs/mod/layer-enforcement.md) (four layers, dependencies down; a package is a folder `src/<layer>/<package>/`, its tests at `tests/<layer>/<package>/`):

```
lib/src/mod/
  .claude-plugin/plugin.json          the manifest: name apogee, version, engines (claude-code >=2.1.326),
                                      types, and the options binary and channel
  hooks/hooks.json                    names the hooks module: ./register.tsx
  hooks/register.tsx                  the composition root: every on(...), the options read once, $ handed down
  types/index.d.ts                    the contract: the $.state values (apogee.binary) and their types
  src/infrastructure/discovery/       finding the binary: candidates, locate, the install hints
  src/data/state/                     the apogee.binary atom (shape binary-1)
  src/business/binary/                findBinary: the answer kept, the absence said once a session
  src/presentation/notices/           the not-installed toast
  tests/support/                      machine.ts (the scripted machine), state.ts (the state, watched)
  tests/<layer>/<package>/*.test.ts   claude plugin test's hermetic tests
  tests/architecture/layering/        the layering check (check.mjs) and its planted violations, run by node
  tests/pipeline/changed.test.mjs     changed.sh mod's proof, run by node
  vendor/claude-code/index.d.ts       the 2.1.326 API declarations, vendored unmodified for tsc
  package.json, package-lock.json     the checks' toolchain: typescript 6.0.3 and the npm scripts
  tsconfig.json                       the API header's options; includes vendor/claude-code, hooks, src, types, tests
```

Not in git: `node_modules/`, and `.claude-plugin/types/`, the declarations the engine lays beside the mod each time it loads it from a folder. The mod's tsconfig never includes that folder, so a loaded mod's two copies do not collide.

**The layers' nouns.** The layering check holds each `$` noun to one layer: `process`, `fs`, `env` to Infrastructure; `state`, `store` and the atom library (`atom`, `derive`, `memberOf`, `read`, `update` from `'claude-code'`) to Data; `ui`, `command`, `tool` to Presentation. `http` is refused everywhere (the client invariant), and the composition root and the contract reach none. The atom library counts however it is taken outside Data: imported or re-exported by name, or `'claude-code'` whole (`import * as`, `export *`). Its other rules: an upward or undeclared edge fails by name, nothing comes from outside the mod but `'claude-code'`, no `import()`, every file at `src/<layer>/<package>/`, every layer with a package and every package with its tests, and an empty graph refused. Outside `src/`, `hooks/` holds `register.tsx` alone and `types/` holds `.d.ts` files alone, importing nothing of the mod but each other; a JavaScript file in a layer is refused, since the engine loads it and the check cannot read it (ADR 0001's Landed note has the full list).

**Discovery** (`src/infrastructure/discovery/`), the macOS app's ladder mirrored (`lib/src/darwin/source/data/discovery/BinaryDiscovery.swift`):

| `channel` | Looked at, in order |
|---|---|
| `release` (the default) | the `binary` override, `~/.local/bin/apogee`, `~/.local/bin/apogee-dev`, `~/.local/bin/apogee-test` |
| `dev` | the `binary` override, `~/.local/bin/apogee-dev` — never the release binary |

A leading `~/` in the override is the home folder (`$.env.get('HOME')`). A place that does not exist is `not installed` and is not run; one that cannot be looked at (`$.fs.exists` rejects, as it does for a network location) is passed over with why, and the ladder goes on. Each one that exists is run as `<candidate> version` (10 s) and must print `apogee <version> …` first; a `channel:` line is kept as the binary's reported channel. Every place passed over keeps its reason. None answering is `{ kind: 'not-found', skipped }`, said once a session as a 10-second toast naming every place and why, then the install line: the README's `curl … install.sh | bash`, or `make -C lib/src/cli install MODE=dev` on the dev channel. Discovery runs awaited in `session.start`, after the engine's own start.

## The contracts the mod builds against

- The `--output-format json` documents (models, train status/versions, check, …) — measured live by the mod spike (2026-10-21).
- Machine mode, for anything session-grade later.
- `apogee mcp serve` (the hidden `__mcp-tools` its alias) — the MCP server: 22 read-only tools since the CLI's 54a, the spike's 17 plus the five status reads (`models_list`, `models_status`, `train_status`, `train_versions`, `check`), each answering with its command's JSON document byte for byte; since the CLI's 54b also `complete`, the one tool not read-only, listed only where the person's config lists it under a local backend's `mcp_serve:` (absent otherwise -- the mod's fallback is the CLI path); the registration JSON printed by `apogee mcp presets`.

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
