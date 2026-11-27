<p align="center"><img src="lib/design/assets/Logo-320.png" alt="The Apogee logo: a white rocket on a black field" width="160"></p>

# Apogee

**Local and cloud language models, one system.**

Apogee is an AI harness: it runs LLM workloads against Anthropic, OpenAI, Google and Ollama in the cloud — under either a subscription plan (their official CLIs) or an API billing plan (a direct key) — and against local models through llama.cpp linked in-process — or, opt-in on Apple silicon, through MLX. Which model runs is configuration, not architecture. It ships as a single, self-contained binary that bundles no models, requires no cloud account, and passes its own doctor on a fresh, keyless, modelless machine.

This repository is home to the whole Apogee project. Today that is the **CLI application** ([`lib/src/cli`](lib/src/cli)), which carries the entire harness and is the current focus; **GUI applications** for each desktop platform are a committed direction and live here as siblings (`lib/src/darwin`, `lib/src/linux`, `lib/src/windows`), powering the same binary directly over its structured stdio machine mode — never a localhost port. The macOS one has been built since v0.2.0 (`lib/src/darwin`): every command the CLI offers a person has its place in it. Its own pipeline builds, tests and packages it whenever it changes, and every release carries it as `apogee-gui-darwin.zip`. The **mod** ([`lib/src/mod`](lib/src/mod)) is the Claude plugin: Apogee's local models offered to Claude as tools, `/apogee` commands, a pane and a status line, driving the same binary. Its own pipeline checks it and zips it, and no release carries it yet. The **design** ([`lib/design`](lib/design)) is the prototype every GUI app is built from.

## Install

One command fetches the [latest release](https://github.com/QuantumCompiler/Apogee/releases/latest) for your platform, installs the binary and shell completions, verifies the install, and bootstraps it: the Python runtimes this machine can run are installed into Apogee's own environment, and the providers and stored models it finds are registered — each step said, none fatal. Models are never downloaded; you pull those. `APOGEE_MINIMAL_INSTALL=1` skips the bootstrap, and `apogee bootstrap` runs it any time.

macOS / Linux:

```sh
curl -fsSL https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.sh | bash
```

Windows (PowerShell):

```powershell
irm https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.ps1 | iex
```

For a release other than the latest, name it, as `0.2.0` or `v0.2.0`: `curl … | APOGEE_VERSION=0.2.0 bash` on macOS and Linux, and `& ([scriptblock]::Create((irm <the same URL>))) -Version 0.2.0` in PowerShell.

Prefer to do it by hand? Take your platform's archive from the [releases page](https://github.com/QuantumCompiler/Apogee/releases) — `apogee-<target>.tar.gz` for macOS and Linux, `apogee-<target>.zip` for Windows, each holding the binary, the completion stubs and `machine-schema.json` (the machine-mode protocol as a JSON Schema, for front-end authors) — then run `apogee check --fix` once, and `apogee bootstrap`.

**Tab completion in macOS's own `/bin/bash`:** the installers put bash's stub where bash-completion v2 looks, and bash-completion v2 does not load on bash 3.2, so add `source ~/.local/share/bash-completion/completions/apogee` to your `~/.bash_profile`.

**Platforms:** `macos-arm64`, `linux-x64`, `linux-arm64`, `windows-x64`, `windows-arm64` — every release binary is built natively on its own runner, with llama.cpp in it. Two notes: macOS binaries are unsigned (a `curl` install is clean; for a browser download, `apogee check` detects the quarantine attribute and prints the exact fix), and on Windows the vendor-CLI backends are not available yet — use a direct-API backend there.

**The macOS app:** each release also carries `apogee-gui-darwin.zip`, holding `Apogee.app` for Apple silicon on macOS 27 or later, and a README. It drives the `apogee` the one-liner above installs. It is signed ad hoc and not notarized, so the first time, right-click `Apogee.app`, choose Open, then Open again (or System Settings → Privacy & Security → Open Anyway).

## First run

```sh
apogee check
```

The doctor tells you what is installed, configured, and missing — and `apogee check --fix` repairs the install itself.

**Cloud:** uncomment a backend in `~/.apogee/config/config.json` (the starter config documents every entry, and Apogee's own edits preserve your comments), then store the provider's key — keys never go on the command line:

```sh
apogee auth add anthropic   # hidden prompt; or --stdin / --from-env
apogee chat
```

The config file is JSON with `//` comments (JSONC) — a strict JSON parser reads it once its comments are stripped. An install from before v0.1.5 keeps its `config.yaml` working, with a one-line notice, until `apogee config migrate` converts it (every comment carried over, the original kept as `config.yaml.bak` -- or, touching nothing, the line it cannot carry named, which `apogee check` says beforehand); `apogee config upgrade` adds the options a newer release documents, with their comments, and `apogee check` says when there are any.

**Subscription CLIs:** if you already use `claude`, `codex` or `gemini`, Apogee can find them — `apogee providers scan` shows what this machine has and the evidence for it, and `--register` adds a backend for each one found (nothing is written without it):

```sh
apogee providers scan --register
```

**Local:** pull a model from Hugging Face or your Ollama store, point a `llamacpp` backend at it, and nothing needs the network again:

```sh
apogee models pull <ref>
apogee chat -m <backend>
```

The five surfaces: `apogee complete "…"` is the scriptable one-shot (it reads stdin, too), `apogee chat` is the persistent, resumable conversation — line editing, streamed Markdown rendering, mid-session `/model` switching, context monitoring — `apogee execute` is that conversation opened with a model suite, its symphonies playable with `/play`, `apogee serve` is the OpenAI-compatible HTTP server for **server deployments**, where a remote client makes REST calls to a machine you run it on, and `apogee acp` is Apogee as the agent of an Agent Client Protocol host such as Xcode (Settings › Intelligence › Agents › Add an Agent), over its own stdin and stdout. Beside them, `apogee task run "<goal>"` works a goal unattended: it plans, drives rounds of the same agent loop, and checks the acceptance you state after each, every step in a ledger you can watch, halt and resume.

Run `apogee` with nothing after it at a terminal and it opens a full-screen shell (`apogee tui` spells it out): a new chat, your saved chats and execute's doors, then a conversation — chat's or execute's own session, tools on, permissions asked on screen or settled by a permission mode (Shift+Tab, as in Claude Code), a model's reasoning shown as one line of what it is doing, Ctrl-C stopping a turn — that `apogee chat --resume` picks up and back. Beside it are some twenty views in five categories (Ctrl-G moves between them) — models, chats, suites, symphonies, knowledge, training, the doctor, config and more — each key there the very command it stands for, and an exec line (`:` or `!`) runs any command in place. `apogee tui` also takes the flags `chat` and `execute` take, and opens the shell inside that session: `apogee tui --suite dev --attach . --effort high`. Every command still runs exactly as before, and off a terminal a bare `apogee` prints the help as it always has.

## What's in the harness

- **Nine backends behind one interface** — Anthropic, OpenAI and Google over their direct APIs with streaming, native tool use and typed thinking display; the same vendors plus Ollama through their official CLIs (`claude-cli`, `codex-cli`, `gemini-cli`, `ollama-cli`), each driven as a child process whose credentials Apogee never reads; llama.cpp in-process, with vision, KV-cached sessions and quantization; and, opt-in on Apple silicon, Apple's MLX (`mlx`), a model directory run by `mlx-lm` in a Python child over pipes.
- **One agent loop behind every surface** — native toolsets (a sandboxed filesystem, a gated shell, git, notes, retrieval, knowledge-graph navigation) under an explicit permissions gate, an MCP client for stdio servers, and agents as data with per-agent tool policy and validated structured output.
- **Retrieval and knowledge** — a local chunk store with lexical, vector and hybrid search plus reranking; `--rag` on any turn without touching saved history; knowledge records that capture the *why* behind decisions; knowledge graphs over any collection, or over a source tree parsed with tree-sitter — no model involved — that you, or the model through its graph tools, can walk, and export as a report, a page, GraphML or Mermaid.
- **Model suites and symphonies** — a named set of models, one per role, admitted and loaded as a set; the chat model consulting a smaller member and members checking each other's work, on local models only; and symphonies — named, staged multi-model prompt processes, chainable — played from `apogee execute` or, opt-in, offered to its model as tools.
- **Model operations** — `apogee models pull|convert|quantize|list|info|status`: one directory per model, per format, per set of weights named by the weights' own hash, so nothing ever overwrites an earlier download or fine-tune.
- **Training and distillation** — dataset preparation, teacher distillation, LoRA/QLoRA runs with an eval-gated train → promote → rollback lifecycle producing versioned GGUFs (or, with `--target mlx`, MLX model directories), in a Python environment Apogee owns.
- **One install contract** — every install path produces the identical on-disk layout, `apogee check` validates it, and secrets live `0600` beside the config: never logged, never returned over HTTP.

## This repository

| Path | What it is |
|---|---|
| [`lib/src/cli/`](lib/src/cli) | The CLI application — the whole harness, one self-contained CMake project. |
| [`lib/src/darwin/`](lib/src/darwin) | The macOS application — an Xcode project; it drives the CLI over its [machine mode](lib/documentation/reference/cli/machine-mode.md). Its own pipeline, `darwin.yml`, builds, tests and packages it when it changes, and every release carries it as `apogee-gui-darwin.zip`. |
| [`lib/src/mod/`](lib/src/mod) | The mod — the Claude plugin, in TypeScript (Claude Code 2.1.326 or newer); it drives the same binary through its JSON documents, machine mode and `apogee mcp serve`. Its pipeline, `mod.yml`, checks it and keeps it zipped as the run artifact `apogee-mod`; no release carries it yet. |
| `lib/src/linux` · `windows` | The Linux and Windows applications (planned). |
| [`lib/design/`](lib/design) | The design — the React prototype every GUI app is built from, built into one `main.html` that opens with a double-click ([its README](lib/design/README.md)); `assets/` holds the logo. |
| [`lib/documentation/`](lib/documentation) | The documentation, split by kind and then by application: [`assistant/`](lib/documentation/assistant) (contributor docs), [`backlog/`](lib/documentation/backlog) (the work queues), [`adrs/`](lib/documentation/adrs) and [`reference/`](lib/documentation/reference) (integrator-facing contracts), each holding one folder per application. [CLAUDE.md](lib/documentation/CLAUDE.md), the router, says which application a change belongs to and where its docs are. |
| [`lib/documentation/reference/cli/`](lib/documentation/reference/cli) | Integrator-facing reference: the [stdio machine mode](lib/documentation/reference/cli/machine-mode.md) for front-ends, the [HTTP API](lib/documentation/reference/cli/http-api.md) for `serve` clients, [ACP](lib/documentation/reference/cli/acp.md) for agent hosts such as Xcode, and [training](lib/documentation/reference/cli/training.md). |
| [`lib/documentation/assistant/cli/`](lib/documentation/assistant/cli) | Contributor docs — [CLAUDE.md](lib/documentation/assistant/cli/CLAUDE.md) is the entry point; [ROADMAP.md](lib/documentation/assistant/cli/ROADMAP.md) is the release board. |
| [`lib/documentation/backlog/cli/`](lib/documentation/backlog/cli) | The work queue: one document per pending item, written as a ticket, priority-ordered per release. |
| [`lib/scripts/`](lib/scripts) | The installers and `cicd.sh`, the one build entry point local runs and CI share. |

## Building from source

```sh
lib/scripts/cicd.sh --test
```

That builds the CLI and, on a Mac, the macOS application for the host's native target and runs their suites; the CLI's and the app's pipelines make the same call, each naming its own with `--app`. The mod and the design build outside it, through their own npm scripts (`npm ci`, then `npm run check`, under node 26) in `lib/src/mod` and `lib/design/prototype`. Requirements: CMake ≥ 3.25, a C++20 compiler, and git (libcurl development headers on Linux); on a Mac the macOS application builds too, which needs Xcode 27 (`--app cli` leaves it out). llama.cpp is off by default in a source build — configure with `-DAPOGEE_ENABLE_LLAMA=ON` to link it; release binaries always ship with it. `make -C lib/src/cli help` lists the CLI project's own targets.

`make -C lib/src/cli install` puts what you built in `~/.local/bin` with its shell completions, the same install the release scripts make. `MODE=dev` or `MODE=test` installs `apogee-dev` or `apogee-test` beside it instead, each keeping its models, chats and keys in a directory of its own (`~/.apogee-dev`, `~/.apogee-test`), so a development build never touches the install you rely on; `make -C lib/src/cli uninstall MODE=dev` removes it again. Any binary can be pointed at another one for a single run with `--dev`, `--test`, `--release` or `--custom <dir>/config/config.json`.

## Status

Young and moving fast: [v0.1.0](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.0) (the harness), [v0.1.1](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.1) (the release pipeline), [v0.1.2](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.2) (the terminal and the model directory), [v0.1.3](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.3) (local agent tools and small-model depth), [v0.1.4](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.4) (MLX inference, model suites, autonomous tasks and code knowledge graphs), [v0.1.5](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.5) (provider detection and machine-mode integrations), [v0.1.6](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.6) (system insight and the full-screen TUI), [v0.1.7](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.7) (the pre-shipped library), [v0.1.8](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.8) (the doors made law), [v0.1.9](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.9) (desktop-parity capabilities), [v0.2.0](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.2.0) (the desktop client's CLI growths, and the first macOS app) and [v0.2.1](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.2.1) (Apogee in Xcode, the MCP server, the Claude plugin and the second bug sweep) are out, and `v0.2.2` is in development on its branch. The running board of what's shipped, in flight, and next is [ROADMAP.md](lib/documentation/assistant/cli/ROADMAP.md).

## License

[MIT](LICENSE).
