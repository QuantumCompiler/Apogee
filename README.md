# Apogee

**Local and cloud language models, one system.**

Apogee is an AI harness: it runs LLM workloads against Anthropic, OpenAI, Google and Ollama in the cloud — under either a subscription plan (their official CLIs) or an API billing plan (a direct key) — and against local models through llama.cpp linked in-process. Which model runs is configuration, not architecture. It ships as a single, self-contained binary that bundles no models, requires no cloud account, and passes its own doctor on a fresh, keyless, modelless machine.

This repository is home to the whole Apogee project. Today that is the **CLI application** ([`lib/src/cli`](lib/src/cli)), which carries the entire harness and is the current focus; **GUI applications** for each desktop platform are a committed direction and will live here as siblings (`lib/src/darwin`, `lib/src/linux`, `lib/src/windows`), powering the same binary directly over its structured stdio machine mode — never a localhost port.

## Install

One command fetches the [latest release](https://github.com/QuantumCompiler/Apogee/releases/latest) for your platform, installs the binary and shell completions, and verifies the install.

macOS / Linux:

```sh
curl -fsSL https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.sh | bash
```

Windows (PowerShell):

```powershell
irm https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.ps1 | iex
```

Prefer to do it by hand? Take your platform's archive from the [releases page](https://github.com/QuantumCompiler/Apogee/releases) — `apogee-<target>.tar.gz` for macOS and Linux, `apogee-<target>.zip` for Windows, each holding the binary and the completion stubs — then run `apogee check --fix` once.

**Platforms:** `macos-arm64`, `linux-x64`, `linux-arm64`, `windows-x64`, `windows-arm64` — every release binary is built natively on its own runner, with llama.cpp in it. Two notes: macOS binaries are unsigned (a `curl` install is clean; for a browser download, `apogee check` detects the quarantine attribute and prints the exact fix), and on Windows the vendor-CLI backends are not available yet — use a direct-API backend there.

## First run

```sh
apogee check
```

The doctor tells you what is installed, configured, and missing — and `apogee check --fix` repairs the install itself.

**Cloud:** uncomment a backend in `~/.apogee/config.yaml` (the starter config documents every entry, and Apogee's own edits preserve your comments), then store the provider's key — keys never go on the command line:

```sh
apogee auth add anthropic   # hidden prompt; or --stdin / --from-env
apogee chat
```

**Local:** pull a model from Hugging Face or your Ollama store, point a `llamacpp` backend at it, and nothing needs the network again:

```sh
apogee models pull <ref>
apogee chat -m <backend>
```

The three surfaces: `apogee complete "…"` is the scriptable one-shot (it reads stdin, too), `apogee chat` is the persistent, resumable conversation — line editing, streamed Markdown rendering, mid-session `/model` switching, context monitoring — and `apogee serve` is the OpenAI-compatible HTTP server for **server deployments**, where a remote client makes REST calls to a machine you run it on.

## What's in the harness

- **Eight backends behind one interface** — Anthropic, OpenAI and Google over their direct APIs with streaming, native tool use and typed thinking display; the same vendors plus Ollama through their official CLIs (`claude-cli`, `codex-cli`, `gemini-cli`, `ollama-cli`), each driven as a child process whose credentials Apogee never reads; and llama.cpp in-process, with vision, KV-cached sessions and quantization.
- **One agent loop behind every surface** — native toolsets (a sandboxed filesystem, a gated shell, git, notes, retrieval) under an explicit permissions gate, an MCP client for stdio servers, and agents as data with per-agent tool policy and validated structured output.
- **Retrieval and knowledge** — a local chunk store with lexical, vector and hybrid search plus reranking; `--rag` on any turn without touching saved history; knowledge records that capture the *why* behind decisions; knowledge graphs over any collection.
- **Model operations** — `apogee models pull|convert|quantize|list|info|status`: one directory per model, per format, per set of weights named by the weights' own hash, so nothing ever overwrites an earlier download or fine-tune.
- **Training and distillation** — dataset preparation, teacher distillation, LoRA/QLoRA runs with an eval-gated train → promote → rollback lifecycle producing versioned GGUFs, in a Python environment Apogee owns.
- **One install contract** — every install path produces the identical on-disk layout, `apogee check` validates it, and secrets live `0600` beside the config: never logged, never returned over HTTP.

## This repository

| Path | What it is |
|---|---|
| [`lib/src/cli/`](lib/src/cli) | The CLI application — the whole harness, one self-contained CMake project. |
| `lib/src/darwin` · `linux` · `windows` | The GUI applications, one per platform (planned; they drive the CLI over its [machine mode](lib/documentation/reference/machine-mode.md)). |
| [`lib/documentation/reference/`](lib/documentation/reference) | Integrator-facing reference: the [stdio machine mode](lib/documentation/reference/machine-mode.md) for front-ends, the [HTTP API](lib/documentation/reference/http-api.md) for `serve` clients, and [training](lib/documentation/reference/training.md). |
| [`lib/documentation/assistant/`](lib/documentation/assistant) | Contributor docs — [CLAUDE.md](lib/documentation/assistant/CLAUDE.md) is the entry point; [ROADMAP.md](lib/documentation/assistant/ROADMAP.md) is the release board. |
| [`lib/documentation/backlog/`](lib/documentation/backlog) | The work queue: one document per pending item, priority-ordered per release. |
| [`lib/scripts/`](lib/scripts) | The installers and `cicd.sh`, the one build entry point local runs and CI share. |

## Building from source

```sh
lib/scripts/cicd.sh --test
```

That builds every application for the host's native target and runs its suite — the same call CI makes. Requirements: CMake ≥ 3.25, a C++20 compiler, and git (libcurl development headers on Linux). llama.cpp is off by default in a source build — configure with `-DAPOGEE_ENABLE_LLAMA=ON` to link it; release binaries always ship with it. `make -C lib/src/cli help` lists the CLI project's own targets.

## Status

Young and moving fast: [v0.1.0](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.0) (the harness) and [v0.1.1](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.1) (the release pipeline) are out, and `v0.1.2` is in development on its branch. The running board of what's shipped, in flight, and next is [ROADMAP.md](lib/documentation/assistant/ROADMAP.md).
