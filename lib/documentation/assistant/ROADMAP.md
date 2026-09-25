# Apogee — Roadmap

How we work: features get discussed in chat, written up here (and in [SPEC.md](SPEC.md)), given their own document in [`backlog/`](../backlog/README.md) when they're specced, then an agent takes the backlog item and builds it straight from that document (there is no separate TODO file — the backlog is the queue). When work ships it's recorded in detail in [MILESTONES.md](MILESTONES.md); this file stays high-level — the running board of themes per release, plus what's next.

**Version scheme:** feature releases are `v0.x.0`, patch releases are `v0.x.y`; development happens on a branch named for the release being built (currently `v0.1.2`) and merges into `stable` — **and that merge is the release** (decided 2026-09-22): the pipeline publishes whatever version `CMakeLists.txt` names, if it hasn't shipped already. Checked boxes are shipped; the detailed write-up of each is in MILESTONES.md.

---

## Shipped releases

### v0.1.0 — The C++ harness *(tagged 2026-09-22 · [release notes](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.0))*

The first release, planned 2026-08-24 from Ommi's documentation (see [SPEC.md](SPEC.md) → Background for the lineage and divergences) and built in exactly one month: a single, self-contained C++ binary that runs local and cloud language models from one harness — a one-shot completion, an interactive chat, or an OpenAI-compatible HTTP server — with the model a matter of configuration rather than architecture. Five platforms, no bundled models, no cloud account required, and it passes its own doctor on a fresh, keyless, modelless machine. The themes, each recorded in detail in [MILESTONES.md](MILESTONES.md) → Milestones A–Z:

- [x] **The walking skeleton** — CMake/Catch2 project skeleton, the config engine (typed loads, comment-preserving mutation), the harness core (message IR, router, capability probes), the Anthropic backend, `apogee complete`, the shared agent loop behind its Reporter seam, the terminal UX layer, and `apogee chat` with line editing *(Milestones A–H)*
- [x] **Eight backends behind one interface** — Anthropic, OpenAI and Google over their direct APIs (streaming, native tool use, typed thinking) for API billing plans; the same three vendors plus Ollama through their official CLIs (`claude-cli`, `codex-cli`, `gemini-cli`, `ollama-cli`) for subscription plans, each a characterized child process whose credentials Apogee never reads *(Milestones D, I, L)*
- [x] **Local inference and model depth** — llama.cpp linked in-process (KV-cached sessions, vision through mtmd, in-process quantization), `apogee models list/info/status/pull` over Hugging Face and the user's Ollama store with a copy → verify → commit ladder, and per-family model profiles with the three streaming filters *(Milestones J, N–P)*
- [x] **Serving and the control plane** — `apogee serve`, OpenAI-compatible and for server deployments only, over the same loop as every other surface; `/v1/admin/*` behind a per-install `0600` bearer token, its config edits byte-identical to the CLI's; the provider credential store and the one key resolver *(Milestones T–U)*
- [x] **The front-end contract** — stdio machine mode: the CLI's events as JSONL over stdin/stdout so a GUI can power the executable directly, never over a port *(Milestone M, [machine-mode.md](../reference/machine-mode.md))*
- [x] **Tools, agents and MCP** — native toolsets (sandboxed fs, gated shell, git, notes, retrieval) under the `permissions:` schema, the MCP stdio client plus the in-binary `__mcp-tools` server, and agents as data with per-agent tool policy, validated structured output and the bundled reviewers behind `apogee analyze` *(Milestones V–X)*
- [x] **Retrieval and knowledge** — the SQLite/FTS5 chunk store, lexical → vector → hybrid retrieval with reranking through the one per-turn resolver, `--rag` and `auto_rag` splicing excerpts without touching history, knowledge records with capture (`apogee knowledge`, `/capture`), and knowledge graphs up to communities and named cross-collection graphs *(Milestones Q–S, Y)*
- [x] **Training and distillation** — a Python environment Apogee owns, compiled-in kits, dataset preparation and teacher distillation, the train → eval → promote → rollback lifecycle producing versioned GGUFs, and the orchestration layer: gated pipelines, regimes, and the scheduler-invoked cycle *(Milestone Z)*
- [x] **One install contract** — `apogee check` / `--fix`, every install path reading one layout declaration, secrets `0600` and never over HTTP, shell completions for four shells, and the installers + GitHub Releases pipeline *(Milestone K)*

*Every decision the release consumed — the toolchain (C++20, CMake + FetchContent, Catch2 v3, nlohmann/json + CLI11 + libcurl + replxx + SQLite), the five-target matrix (`macos-arm64`, `linux-x64/arm64`, `windows-x64/arm64`; the Intel Mac dropped 2026-09-19), GitHub Releases as the distribution host, macOS unsigned, self-update deferred — is recorded in the milestone that consumed it; the living summary is [CLAUDE.md](CLAUDE.md) → Stack & environment. Known limitations are on the release page: unsigned macOS binaries, Windows the least-verified platform (the vendor-CLI backends refuse there), no self-update, and `serve` is plain HTTP behind your own TLS.*

### v0.1.1 — The release pipeline *(released hours after v0.1.0 · [release notes](https://github.com/QuantumCompiler/Apogee/releases/tag/v0.1.1))*

A maintenance release — nothing under `source/` changed; the binary is functionally identical to v0.1.0, rebuilt by a pipeline worth trusting. A merge into `stable` **is** now the release, with the version in `CMakeLists.txt` deciding which (an already-shipped version publishes nothing, so docs merges stay quiet); the version can no longer disagree with the tag (a local preflight, the workflow's gate job, and a PR-time `version bump` check); the tag and the release are created together after every blocking build passes, so a failed run strands nothing; the source-level suite — 1,411 cases, selected by construction rather than an exclusion list — gates all five platforms before anything is built for release; and the pipeline survives being re-run. The recorded cost: the 18 executable-spawning ctest entries (`cli.install_parity` among them) gate only `make test` now. The procedure lives in [DEVELOPER.md](DEVELOPER.md#cutting-a-release) → Cutting a release.

---

## In progress

### v0.1.2 — The terminal and the model directory

Quality-of-life for the two places a local-model user actually lives: the chat terminal and the model files. Landed on the branch so far:

- [x] **Completion at every depth** *(2026-09-23/24)* — shell completion now reads the whole live parser (every verb, alias and flag, at every depth), argument kinds are declared where each argument is added, and the 95 arguments that name something real complete to it; the stubs are exercised in the real shells.
- [x] **The model store** *(2026-09-23)* — one directory per model, per format (`gguf/`, `safetensors/`), per set of weights named by the weights' own hash, declared once in `models/store.h`; `apogee models convert` (SafeTensors → GGUF through llama.cpp's vendored converter, image projector included); `models migrate` for existing installs; a pull that can no longer overwrite an earlier snapshot; training outputs land in the same structure.
- [x] **Conversion hardening** *(2026-09-23/24)* — llama.cpp repinned to `b11151` (Gemma 4's unified checkpoints), `quantize` carries the projector (claimed on day one, found unwired, fixed with a test), and `convert` shows hashing progress and cleans its staging on Ctrl-C.
- [x] **Chat and terminal UX** *(2026-09-25)* — the wait after every chat answer removed, a hybrid model's failing second turn fixed, `models list` coloured by what a row is, and room around the banner and each question.
- [x] **Terminal Markdown rendering** — backlog item 23, shipped 2026-09-25 *([Milestone G](MILESTONES.md#milestone-g--the-terminal-ux-layer))*: an answer's Markdown rendered as it streams — emphasis, headings, lists, quotes, fenced code, rules, links, tables — one open line redrawn in place and never the last column, while pipes, machine mode and the saved transcript keep the model's text byte for byte. Hand-written over md4c and no code highlighting yet (both the user's calls); `--raw` and `ui.markdown: false` switch it off.
- [x] **Chat input completion** — backlog item 24, shipped 2026-09-25 *([Milestone H](MILESTONES.md#milestone-h--apogee-chat))*: Claude Code's input affordances in `apogee chat`. Typing `/` lists the commands as you type, each with a one-line description; a command's values follow it (backends after `/model`, retrievers after `/retriever`); `@` completes files and folders from the working directory; Tab takes the top row. The rows are the line editor's own (not a TUI), never reach the last column, and never outlive the line they belong to. One table now feeds `/help`, completion and dispatch, and a missing handler fails the build; `/retriever` and `/rerank` are listed for the first time. A piped chat is unchanged. A sent `@file` stays text until attachments (26d) land (the user's call).
- [x] **Releases from the pull request's own build** *(2026-09-25, [Milestone K](MILESTONES.md#milestone-k--the-install-contract))*: a merge into `stable` no longer rebuilds anything, and no longer runs `release.yml`. CI packages every build as it runs. Once the pull request merges, `tag and release` checks those archives against the merged source, tags the version the executable reports, and publishes them. A dispatched rehearsal proves it before the merge, and a pushed tag remains the manual path. It is first exercised by this release's own merge.

---

## Up next

### v0.1.3 — Local agent tools + small-model depth

The two tracks specced 2026-09-25, eighteen backlog items in all — see the [`backlog/`](../backlog/README.md) index's v0.1.3 table for the queue and gates.

- **Local agent tools** *(a spike split into six items, [25a–25f](../backlog/README.md#index))*: local models get files, the shell, git, notes, document search, page reading and web search — the tools every other backend already has. The spike found the gap (the llama.cpp backend never put tools into the prompt, so no local model was ever shown one) and measured the fix, llama.cpp's own chat layer linked in-process: 6 of 6 tasks on Qwen3.8-27B and Qwen3-VL-8B. In build order: **tool safety defaults** (fetch asks per new website, file tools start in the launch folder — closes an outbound-data exposure that exists today), **local tool calling** (the unlock), **hybrid prompt checkpoints** (Qwen3.5/3.8 stop re-reading the conversation every step), **tool ergonomics**, **web search through the user's own SearXNG** (revising the 2026-08-26 provider-search-only decision), and **`fetch_url` as a reader**. The user's calls: SearXNG, ask per website, the launch folder, and 8B-class models and up.
- **Small-model depth** *(a review split into twelve items, [26a–26l](../backlog/README.md#index))*: getting the most out of small local models. **Automatic attachments** — files, folders, PDFs, images, audio and video attached to a chat, indexed with the embedding model, handed to the model each turn with citations; media read natively when the model can and through a helper model when not. **Helper-model roles** (vision, transcription, a utility model for chores). **Reliability** — grammar-constrained JSON, tool selection by relevance, sampling that actually reaches local models, thinking on/off/auto with a budget. **Speed and memory** — a default window sized to the machine, a persistent prompt cache, speculative decoding only if a clean measurement justifies it. **Memory across chats** — a per-turn context budget and automatic recall, never on `serve`. The user's calls: attachments kept with their chat and cached by hash, helper models used automatically, and external converters.

## Unspecced ideas

- **GUI sibling applications** *(committed direction 2026-08-24; planned home `lib/src/darwin|linux|windows`, one app per platform)* — graphical front-ends shipped alongside the harness, powering the CLI directly over stdin/stdout via the structured JSONL machine mode (never a localhost port; mutations shell out to the same CLI commands). Its contract now exists — see the [machine-mode protocol](../reference/machine-mode.md) (shipped 2026-09-06); the GUI itself still needs its own planning pass. No TUI, ever.

## Ideas / candidate features

- Plugin system (`plugin.yaml` overlay — deliberately left out of the initial plan; separable later on top of the config engine)
- **Self-update (`apogee update`)** — deliberately deferred out of v0.1.0 *(decided 2026-09-01)*, and now unblocked: the distribution host is settled and the release pipeline exists. It becomes the **third parity path** alongside the two installers, so it must go through `apogee check --fix` like they do rather than seeding anything itself. Needs its own backlog item; the background update notifier (TTY-only, 24h cache, subcommand skip-list) rides with it.
- Remote MCP transports + OAuth connectors (and the connector-driven review agents that ride them)
- **Forge integration for the review agents** — a GitHub and/or GitLab tool server for the merge-request agent's MR mode (read the MR, its discussions and diff; look up a ticket) and the forge credential slots deferred from the credential store (2026-09-13). **The forge target is the user's call** (GitHub, GitLab, or both) and gates this item; it would be the first user of the in-binary MCP server pattern beyond Apogee's own tools.

## Explicit non-goals

Ruled out on purpose (see [SPEC.md](SPEC.md) → Non-goals for the rationale):

- **Mandatory cloud dependency**
- **Bundled models**
- **TUI front-end** (the GUI is a committed sibling project over the HTTP planes — never in-binary)
- **Silent install drift**
