# Apogee — Project Spec

The product spec: what Apogee is, what it is for, and the principles that shape it. The high-level plan and progress board are in [ROADMAP.md](ROADMAP.md); the work queue is [`backlog/`](../backlog/README.md) (one document per pending item); shipped work is recorded in [MILESTONES.md](MILESTONES.md); how to work in the repo is in [CLAUDE.md](CLAUDE.md).

---

## One-liner

**Apogee** is a self-contained, primarily-C++ AI harness that runs local and cloud LLMs from one system — a one-shot completion, an interactive chat, or an OpenAI-compatible HTTP server — with cloud access to Anthropic, OpenAI, Google, and Ollama under either a subscription plan (their official CLIs) or an API billing plan, and local inference via llama.cpp.

## Background

Every LLM provider ships its own API, SDK, authentication, and quirks — and local inference through llama.cpp is different again. A project that wants to mix cloud models with local ones, or simply stay free to switch providers, ends up rewriting the same plumbing for each backend. Apogee absorbs that difference: the four backends sit behind one harness, so which model runs becomes configuration rather than architecture.

Apogee is a from-scratch re-implementation of **Ommi** (the sibling project at `~/Data/Development/Projects/Ommi` — a mature Go harness with the same docs-first process), carrying over its product shape and battle-tested design rules while diverging deliberately in three ways *(decided 2026-08-24)*:

1. **Primarily C++** instead of Go — the harness core, backends, and surfaces are C++.
2. **Dual-path cloud access, four vendors** *(revised 2026-08-24)* — Anthropic, OpenAI, Google, and Ollama cloud models, each reachable through its official CLI (**subscription plans**: claude / codex / gemini / ollama, driven as persistent child processes with token-level streaming per [claude-cli-streaming-backend.md](claude-cli-streaming-backend.md)) and, where the vendor offers one, its direct HTTPS API (**API billing plans**). Both plans must work; the choice is per backend entry. Ommi had only the Anthropic API and a per-request claude shell-out.
3. **In-process llama.cpp** — C++ can link llama.cpp directly instead of Ommi's per-turn subprocess spawns, keeping the same zero-listening-socket guarantee without process churn. (Final call is an open call on the llama.cpp backend's backlog item.)
4. **Open local models** *(decided 2026-08-24)* — no forbidden or curated local models: any model the user supplies runs. Ommi's sha256-pinned approved-model allowlist and its refusal semantics are deliberately not ported. Models, datasets, and tensors download directly from Hugging Face or pull through Ollama.

Parts that are Python in Ommi (training drivers, MCP servers) may stay Python in Apogee — "primarily C++" governs the harness, not every satellite script.

## Core surfaces

Apogee presents the same harness through three front-ends, in rough order of interactivity *(shape adopted from Ommi 2026-08-24; Ommi's removed TUI is deliberately not cloned)*:

1. **`apogee complete`** — one-shot prompt → answer; scriptable and pipeable.
2. **`apogee chat`** — interactive multi-turn session: persistent, resumable, mid-session model switching, context-window monitoring.
3. **`apogee serve`** — OpenAI-compatible HTTP server (`/v1/chat/completions`, `/v1/completions`, …), later joined by a bearer-gated `/v1/admin` control plane. **Server deployments only** *(decided 2026-08-24)*: serve applies when the executable runs on a server and a client — a mobile or desktop app — makes REST calls (POST/GET) to it over the network. A local front-end never talks to a localhost port.

Around those sit capability and lifecycle commands (models, config, check, …) as the capability areas land — the full map is the [`backlog/`](../backlog/README.md).

A **GUI ships as a sibling application** *(committed direction 2026-08-24; planned home `lib/src/darwin|linux|windows`, one app per platform — down the road)*, and it powers the CLI directly: the GUI runs the executable as a child process and speaks to it over **stdin/stdout** — a structured JSONL event mode (the backlog's stdio-machine-mode item), never a localhost port. Mutations go through the same CLI commands, which is why the CLI-is-the-contract principle carries the GUI for free. There is **no TUI**, and there never will be one (Ommi's build-then-delete lesson, adopted as policy).

## Scope (in)

- **Backends:** cloud — Anthropic, OpenAI, Google, and Ollama, each via its official CLI (subscription plan; persistent child, token-level streaming; the harness never reads a CLI's credentials or session files) and, where offered, its direct HTTPS API (API billing plan), selected per backend entry; local — llama.cpp linked in-process.
- **Open local models:** no forbidden or curated models — any user-supplied model runs; verification protects integrity (digests, loadability), never gates choice; unknown models are handled permissively through behavior profiles.
- **Model & data sources:** direct Hugging Face downloads (models, GGUFs, SafeTensors, datasets) and pulls through the user's Ollama store.
- **Agentic tool loop:** one shared model→tool→model loop behind all surfaces; MCP client and agent management in a later ring.
- **Sessions & context:** persistent resumable chats; Apogee-owned context monitoring and compaction on every backend.
- **RAG & knowledge** *(gated ring)*: lexical-floor-first retrieval (BM25, then vector, then hybrid), and the knowledge/graph layers after it.
- **Local-model depth** *(gated ring)*: model management and per-family behavior profiles.
- **One install contract:** identical on-disk layout from every install path, validated by `apogee check`.
- **Training/distillation** *(unscheduled outer ring; confirmed direction 2026-08-24)*: fine-tuning local models on their full-weight SafeTensors files, cloned from Ommi's orchestration shape when its turn comes — timing, not existence, is the open question.

## Non-goals

*(Adopted from Ommi 2026-08-24 — inherited defaults, each revisable as Apogee finds its own shape.)*

- **No mandatory cloud dependency.** The harness runs fully local; cloud backends are opt-in per configured API key.
- **No bundled models.** Models are user-supplied; installers download nothing unasked.
- **No TUI, and no second in-binary front-end.** Ommi built a full TUI and deleted it — the recorded lesson: the CLI is the contract, GUIs consume the HTTP planes. Apogee starts from that lesson. *(Clarified 2026-08-24: a GUI is a committed direction — as a sibling project over `serve` + `/v1/admin`, never inside the harness binary.)*
- **No silent install drift.** Any asset landing in the data directory ships identically across all install paths in the same change.

## Principles

*(The first is Apogee's own; the rest are adopted from Ommi 2026-08-24, where each was earned by a real bug or reversal.)*

- **Backend-agnostic core.** The same workload runs against any supported backend — cloud or local — with the backend a matter of configuration, not code shape.
- **Parity is the product.** A capability that exists on only one surface is a bug; the CLI is the contract and HTTP inherits it.
- **Interactive = pipes, serving = server.** No interactive turn on any backend opens a listening socket; only `serve` owns a port. In-process llama.cpp satisfies this by construction. *Strengthened 2026-08-24: local front-ends are pipes too* — the GUI powers the CLI over stdin/stdout, and `serve` exists solely for server deployments where a remote client makes REST calls; it is never a localhost backend for a local front-end.
- **Local by default, cloud by choice.** Model, data, and secrets stay on the machine; secrets live `0600` next to the config and are never returned over HTTP or written to logs.
- **One source of truth per concern.** One agent loop, one config-mutation path, one resource-creation core — shared by every surface so parity is structural, not audited.
- **Fail loud on install/parity, degrade gracefully at runtime.** A broken layout stops a release; a missing key or model produces a clear message, never a crash.

## Success criteria

- The same request runs against Anthropic, OpenAI, Google, or a local llama.cpp model by changing only the backend selection.
- A fresh install with **no API keys and no models** passes `apogee check`; adding one API key makes `chat`/`complete` work against that provider; pointing config at a GGUF makes local inference work.
- Each cloud vendor works under **both** a subscription plan (its official CLI) and an API billing plan (a direct key), chosen per backend entry.
- A streamed, tool-using chat behaves the same from the CLI on all four backends, with thinking displayed live and never persisted into history.
- A stock OpenAI client library works unmodified against `apogee serve` *(post-v0.1.0 ring)*.
