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
- Stdio machine mode: the CLI's JSONL event stream over stdin/stdout, so the GUI can power the executable directly — never over localhost (the GUI project gates on this)
- Vendor CLI backends — completing the subscription-plan path for the four-vendor cloud set. **Split per CLI at grooming (2026-09-06)** into three items. **ollama shipped 2026-09-06** *([Milestone L](MILESTONES.md#milestone-l--the-vendor-cli-family) → the `ollama-cli` entry)* — the odd one out, and the weakest backend in Apogee: no event stream, no persistent child, and a pre-flight that refuses rather than let the CLI start a server. **codex** (OpenAI) and **gemini** (Google) remain, each gated in practice on having that CLI installed, since each must open with an empirical characterization before any adapter code.
- Local-model depth: per-family profiles + open model management — no forbidden models, sources = Hugging Face direct + Ollama pulls (split before build)
- RAG: lexical floor → embedding clients → vector/hybrid/rerank
- `apogee serve` (OpenAI-compatible) → `/v1/admin` foundation + provider credential store (split before build) — **server deployments only**: remote clients (mobile/desktop) making REST calls to the executable on a server; never a localhost backend for a local front-end (decided 2026-08-24)
- MCP client + native toolsets + analyze/agents (split into three before build)
- Knowledge layer + knowledge graphs (placeholder — split into four before build)

## Unspecced ideas

- **GUI sibling applications** *(committed direction 2026-08-24; planned home `lib/src/darwin|linux|windows`, one app per platform)* — graphical front-ends shipped alongside the harness, powering the CLI directly over stdin/stdout via the structured JSONL machine mode (never a localhost port; mutations shell out to the same CLI commands). Needs its own planning pass once stdio-machine-mode exists. No TUI, ever.

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
