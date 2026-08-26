# Apogee — Roadmap

How we work: features get discussed in chat, written up here (and in [SPEC.md](SPEC.md)), given their own document in [`backlog/`](../backlog/README.md) when they're specced, then an agent takes the backlog item and builds it straight from that document (there is no separate TODO file — the backlog is the queue). When work ships it's recorded in detail in [MILESTONES.md](MILESTONES.md); this file stays high-level — the running board of themes per release, plus what's next.

**Version scheme:** feature releases are `v0.x.0`, patch releases are `v0.x.y`; development happens on a branch named for the upcoming release (currently `v0.1.0`) and merges into `stable`. Checked boxes are shipped; the detailed write-up of each is in MILESTONES.md.

---

## Shipped releases

*(Nothing shipped yet — the first tagged release starts this board.)*

---

## In progress

### v0.1.0 — The C++ harness (walking skeleton → four backends → install contract)

The first release, planned 2026-08-24 from Ommi's documentation (see [SPEC.md](SPEC.md) → Background for the lineage and divergences): a single self-contained C++ binary shipping `apogee complete` and `apogee chat` over all four backends — Anthropic, OpenAI, and Google called directly with streaming, native tool use, and typed thinking display, plus in-process llama.cpp — on top of the config engine, harness core, and shared agent loop, with persistent resumable sessions, a single-status-line terminal UX, and a clean install contract (`apogee check` passes on a fresh keyless, modelless install). Ten backlog items, in build order:

- [x] C++ project skeleton (CMake, tests, CI, CLI scaffold) — *shipped 2026-08-25, [Milestone A](MILESTONES.md#milestone-a--project-foundation)*
- [x] Config engine (typed loader + comment-preserving mutation)
- [ ] Harness core (LLMProvider, message IR, router)
- [ ] Anthropic backend (direct Messages API + SSE)
- [ ] `apogee complete` — the walking-skeleton closer
- [ ] Shared agent loop (Reporter seam, tools, ask_user)
- [ ] `apogee chat` + terminal UX layer (split at grooming: UX / chat)
- [ ] OpenAI + Google backends
- [ ] llama.cpp in-process backend
- [ ] Install contract + `apogee check` + completions (release closer)

See the [`backlog/`](../backlog/README.md) index for the full queue (priority-ordered; topmost claimable item = next to build).

**Decisions needed before building continues** (the `[user]` open calls on the remaining items): config format (YAML vs TOML), data-dir name (`~/.apogee/`), in-process llama.cpp confirmation, and the web-search strategy. The self-update timing question blocks only the release closer.

*Answered so far — 2026-08-24: the platform matrix (Linux/macOS/Windows on both ARM and x86, six targets) and the repo host (GitHub, making GitHub Releases/Actions the default). 2026-08-25, with the skeleton: **C++20** as the language baseline, **CMake + FetchContent** for build and dependencies, **Catch2 v3** for tests, **nlohmann/json + CLI11 + libcurl** as the standardized library picks, and **macos-arm64 as the only merge-blocking CI target** for v0.1.0.*

---

## Fast follow

The gated ring — specced with their own backlog documents, sequenced after v0.1.0 ships (gate convention: each also assumes the full v0.1.0 set):

- Claude-CLI backend: persistent child, token-level streaming — the subscription-plan path (design notes adopted 2026-08-24; earmarking into v0.1.0 is an open `[user]` call)
- Stdio machine mode: the CLI's JSONL event stream over stdin/stdout, so the GUI can power the executable directly — never over localhost (the GUI project gates on this)
- Vendor CLI backends: codex (OpenAI), gemini (Google), ollama (Ollama cloud) — completing the subscription-plan path for the four-vendor cloud set (split per CLI before build; each starts with an empirical characterization)
- Local-model depth: per-family profiles + open model management — no forbidden models, sources = Hugging Face direct + Ollama pulls (split before build)
- RAG: lexical floor → embedding clients → vector/hybrid/rerank
- `apogee serve` (OpenAI-compatible) → `/v1/admin` foundation + provider credential store (split before build) — **server deployments only**: remote clients (mobile/desktop) making REST calls to the executable on a server; never a localhost backend for a local front-end (decided 2026-08-24)
- MCP client + native toolsets + analyze/agents (split into three before build)
- Knowledge layer + knowledge graphs (placeholder — split into four before build)

## Unspecced ideas

- **GUI sibling applications** *(committed direction 2026-08-24; planned home `lib/src/darwin|linux|windows`, one app per platform)* — graphical front-ends shipped alongside the harness, powering the CLI directly over stdin/stdout via the structured JSONL machine mode (never a localhost port; mutations shell out to the same CLI commands). Needs its own planning pass once stdio-machine-mode exists. No TUI, ever.

## Ideas / candidate features

- Plugin system (`plugin.yaml` overlay — deliberately left out of the initial plan; separable later on top of the config engine)
- Self-update (`apogee update`) — rides the distribution-host decision on the install item
- Remote MCP transports + OAuth connectors (and the connector-driven review agents that ride them)

## Explicit non-goals

Ruled out on purpose (see [SPEC.md](SPEC.md) → Non-goals for the rationale):

- **Mandatory cloud dependency**
- **Bundled models**
- **TUI front-end** (the GUI is a committed sibling project over the HTTP planes — never in-binary)
- **Silent install drift**
