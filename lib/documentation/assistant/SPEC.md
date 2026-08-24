# Apogee — Project Spec

The product spec: what Apogee is, what it is for, and the principles that shape it. The high-level plan and progress board are in [ROADMAP.md](ROADMAP.md); the work queue is [`backlog/`](../backlog/README.md) (one document per pending item); shipped work is recorded in [MILESTONES.md](MILESTONES.md); how to work in the repo is in [CLAUDE.md](CLAUDE.md).

---

## One-liner

**Apogee** is an AI harness that runs LLM workloads across the Anthropic, OpenAI, and Google cloud APIs and local models served through llama.cpp.

## Background

Every LLM provider ships its own API, SDK, authentication, and quirks — and local inference through llama.cpp is different again. A project that wants to mix cloud models with local ones, or simply stay free to switch providers, ends up rewriting the same plumbing for each backend. Apogee is the harness that absorbs that difference: the four backends (Anthropic, OpenAI, Google, llama.cpp) sit behind one system, so which model runs becomes configuration rather than architecture.

_TODO:_ the specific motivating use case, and how Apogee differs from existing multi-provider layers, haven't been discussed yet — record them here when they are.

## Core surfaces

_TODO:_ not yet decided — is Apogee consumed as a library API, a CLI, a server/HTTP API, or a combination? This is the first product-shape call to make; record each surface here (one line: the surface + what it's for) when decided.

## Scope (in)

- **Anthropic backend:** running workloads against Anthropic's cloud LLM API.
- **OpenAI backend:** running workloads against OpenAI's cloud LLM API.
- **Google backend:** running workloads against Google's cloud LLM API.
- **Local inference:** running local models through llama.cpp.

_TODO:_ the harness capabilities themselves (completions? streaming? tool use? conversation management?) are not yet specced — add each capability area here as it's decided.

## Non-goals

_TODO:_ none recorded yet — deliberate exclusions land here with their rationale as they come up in discussion.

## Principles

- **Backend-agnostic core.** The same workload runs against any supported backend — cloud or local — with the backend a matter of configuration, not code shape.

_TODO:_ further principles as design discussions settle them.

## Success criteria

- The same request can be executed against Anthropic, OpenAI, Google, or a local llama.cpp model by changing only the backend selection.

_TODO:_ 2–4 more concrete, checkable criteria once the surfaces and capability set are specced.
