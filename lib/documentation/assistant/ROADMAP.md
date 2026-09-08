# Apogee — Roadmap

How we work: features get discussed in chat, written up here (and in [SPEC.md](SPEC.md)), given their own document in [`backlog/`](../backlog/README.md) when they're specced, then an agent takes the backlog item and builds it straight from that document (there is no separate TODO file — the backlog is the queue). When work ships it's recorded in detail in [MILESTONES.md](MILESTONES.md); this file stays high-level — the running board of themes per release, plus what's next.

**Version scheme:** feature releases are `v0.x.0`, patch releases are `v0.x.y`; development happens on a branch named for the upcoming release (currently `v0.1.0`) and merges into `stable`. Checked boxes are shipped; the detailed write-up of each is in MILESTONES.md.

---

## Shipped releases

*(Nothing shipped yet — the first tagged release starts this board.)*

---

## In progress

### v0.1.0 — The C++ harness (walking skeleton → four backends → install contract)

The first release, planned 2026-08-24 from Ommi's documentation (see [SPEC.md](SPEC.md) → Background for the lineage and divergences): a single self-contained C++ binary shipping `apogee complete` and `apogee chat` over all four backends — Anthropic, OpenAI, and Google called directly with streaming, native tool use, and typed thinking display, plus in-process llama.cpp — on top of the config engine, harness core, and shared agent loop, with persistent resumable sessions, a single-status-line terminal UX, and a clean install contract (`apogee check` passes on a fresh keyless, modelless install). Twelve backlog items, in build order:

- [x] C++ project skeleton (CMake, tests, CI, CLI scaffold) — *shipped 2026-08-25, [Milestone A](MILESTONES.md#milestone-a--project-foundation)*
- [x] Config engine (typed loader + comment-preserving mutation)
- [x] Harness core (LLMProvider, message IR, router)
- [x] Anthropic backend (direct Messages API + SSE)
- [x] `apogee complete` — the walking-skeleton closer
- [x] Shared agent loop (Reporter seam, tools, ask_user)
- [x] Terminal UX layer (status line, thinking view, cliReporter)
- [x] `apogee chat` (REPL, sessions, context monitoring, logging)
- [x] Chat line editing (replxx: arrow keys, history, completion)
- [x] OpenAI + Google backends — *[Milestone I](MILESTONES.md#milestone-i--the-full-cloud-set)*
- [x] llama.cpp in-process backend — *[Milestone J](MILESTONES.md#milestone-j--local-inference)*
- [x] Install contract + `apogee check` + completions — *[Milestone K](MILESTONES.md#milestone-k--the-install-contract)*

See the [`backlog/`](../backlog/README.md) index for the full queue (priority-ordered; topmost claimable item = next to build).

**Every v0.1.0 item has shipped** (Milestones A–K). What remains before the release is cutting it: the tag → build → publish pipeline exists and is syntactically valid, but **no tag has been pushed**, so the first release is also the first run of that pipeline.

*Decisions taken along the way: config format, data-dir name, web-search strategy, in-process llama.cpp, local vision's home, and — 2026-09-01 — GitHub Releases as the distribution host, self-update deferred, macOS unsigned, and all six targets shipping from the first tag. See the milestones that consumed each.*

*Answered so far — 2026-08-24: the platform matrix (Linux/macOS/Windows on both ARM and x86, six targets) and the repo host (GitHub, making GitHub Releases/Actions the default). 2026-08-25, with the skeleton: **C++20** as the language baseline, **CMake + FetchContent** for build and dependencies, **Catch2 v3** for tests, **nlohmann/json + CLI11 + libcurl** as the standardized library picks, and **macos-arm64 as the only merge-blocking CI target** for v0.1.0.*

---

## Fast follow

The gated ring — specced with their own backlog documents, sequenced after v0.1.0 ships (gate convention: each also assumes the full v0.1.0 set):

- [x] **Claude-CLI backend** — persistent child, token-level streaming; the subscription-plan path *(shipped 2026-09-02, [Milestone L](MILESTONES.md#milestone-l--the-vendor-cli-family))*. It also builds the family's shared machinery: the child-process seam, the JSONL framer, and the typed event union that codex/gemini/ollama reuse.
- [x] **Stdio machine mode** — the CLI's JSONL event stream over stdin/stdout, so the GUI can power the executable directly, never over localhost *(shipped 2026-09-06, [Milestone M](MILESTONES.md#milestone-m--the-front-end-contract))*. The protocol is documented for front-end authors in [machine-mode.md](../reference/machine-mode.md).
- [x] **Vendor CLI backends — complete.** The subscription-plan path now exists for all four cloud vendors. **Split per CLI at grooming (2026-09-06)** into three items, all shipped 2026-09-06 *([Milestone L](MILESTONES.md#milestone-l--the-vendor-cli-family))*: **codex** (typed events, no deltas, a pinned read-only sandbox), **ollama** (the odd one out and the weakest backend in Apogee — no event stream, no persistent child, and a pre-flight that refuses rather than let the CLI start a server), and **gemini** (real token deltas, Apogee-chosen session ids, and a safety pin that needs two flags to hold). With this, SPEC's dual-path claim is true in full: every cloud vendor works under both a subscription plan and an API billing plan, chosen per backend entry.
- Local-model depth — **split into four at grooming (2026-09-06)**. **Model operations shipped 2026-09-07** *([Milestone N](MILESTONES.md#milestone-n--model-operations))*: one shared role resolver used by every surface, the `apogee models list/info/status` suite, and a real GGUF header reader that replaced a magic-bytes check which had been reporting "model loads" for files that cannot load. **Open acquisition shipped 2026-09-07** *(same milestone)*: `apogee models pull` fetches any user-named ref from Hugging Face or the user's Ollama store through a copy → verify → commit ladder that never leaves a half-downloaded model behind, and reports **which** checks ran rather than a bare "verified" — because with no allowlist most sources publish no digest at all. **Local multimodal shipped 2026-09-07** *([Milestone O](MILESTONES.md#milestone-o--local-multimodal))*: a local model with an `mmproj_path` reads images in-process through llama.cpp's mtmd — verified live — and the cross-surface `--image` guard gap is closed. **Acquisition completed the same day** *([Milestone N](MILESTONES.md#milestone-n--model-operations))*: in-process `quantize` (5 GiB F16 → 1 GiB Q4_K_M in 11 s), and Ollama's projector layer copied alongside the model so a vision model arrives ready to configure. The transform this item inherited from Ommi was **never built** — asking the registry showed Ollama already ships the projector as its own layer, so there was nothing combined to split. **Model profiles shipped 2026-09-07** *([Milestone P](MILESTONES.md#milestone-p--model-profiles))*: a per-family registry with an explicit resolution ladder, and a streaming reasoning filter that fixed an observed leak — Qwen was printing `<think></think>` straight into its answers. The characterization **contradicted the plan**: Gemma 3 ships a working chat template where Ommi's Gemma 4 shipped none, so porting rather than characterizing would have overridden something that worked. **Model profiles completed the same day** *([Milestone P](MILESTONES.md#milestone-p--model-profiles))*: the gate — a model that actually emits control tokens or channel headers — was opened by **gpt-oss-20b**, which emits both. Its framing was printing as the answer and its tool calls were printing instead of dispatching; a header filter, a control-token parser, and a suppress-then-parse gate fixed both, verified live end to end. Here the reference implementation's transcription turned out to be **right** — it had marked the same framing unverified because the GGUF it had would not load — and the run still refuted the obvious design: the analysis channel is a reasoning block, not framing to delete, so stripping headers alone would have moved the model's working into its answer. Still pending in this track: SafeTensors and dataset downloads.
- RAG — **the lexical floor shipped 2026-09-07** *([Milestone Q](MILESTONES.md#milestone-q--the-retrieval-floor))*: a SQLite/FTS5 chunk store, `apogee embed ingest|query|list|info|delete`, and `--rag` on `complete` and `chat` splicing retrieved excerpts into the outgoing request without ever writing them to persisted history. It needs **no model and no network** — verified live against Apogee's own docs with zero backends configured. Two findings came from running it: FTS5's implicit AND returns nothing for any real question (a question carries words its answer does not), and every hit reports which retriever scored it, because BM25 and cosine scales are not comparable. Still pending in this track: the `embeddings:` config section with auto-registration and the `auto_rag` key ([embedstore-lexical-rag.md](../backlog/embedstore-lexical-rag.md)), then embedding clients → vector/hybrid/rerank.
- `apogee serve` (OpenAI-compatible) → `/v1/admin` foundation + provider credential store (split before build) — **server deployments only**: remote clients (mobile/desktop) making REST calls to the executable on a server; never a localhost backend for a local front-end (decided 2026-08-24)
- MCP client + native toolsets + analyze/agents (split into three before build)
- Knowledge layer + knowledge graphs (placeholder — split into four before build)

## Unspecced ideas

- **GUI sibling applications** *(committed direction 2026-08-24; planned home `lib/src/darwin|linux|windows`, one app per platform)* — graphical front-ends shipped alongside the harness, powering the CLI directly over stdin/stdout via the structured JSONL machine mode (never a localhost port; mutations shell out to the same CLI commands). Its contract now exists — see the [machine-mode protocol](../reference/machine-mode.md) (shipped 2026-09-06); the GUI itself still needs its own planning pass. No TUI, ever.

## Ideas / candidate features

- Plugin system (`plugin.yaml` overlay — deliberately left out of the initial plan; separable later on top of the config engine)
- **Self-update (`apogee update`)** — deliberately deferred out of v0.1.0 *(decided 2026-09-01)*, and now unblocked: the distribution host is settled and the release pipeline exists. It becomes the **third parity path** alongside the two installers, so it must go through `apogee check --fix` like they do rather than seeding anything itself. Needs its own backlog item; the background update notifier (TTY-only, 24h cache, subcommand skip-list) rides with it.
- Remote MCP transports + OAuth connectors (and the connector-driven review agents that ride them)

## Explicit non-goals

Ruled out on purpose (see [SPEC.md](SPEC.md) → Non-goals for the rationale):

- **Mandatory cloud dependency**
- **Bundled models**
- **TUI front-end** (the GUI is a committed sibling project over the HTTP planes — never in-binary)
- **Silent install drift**
