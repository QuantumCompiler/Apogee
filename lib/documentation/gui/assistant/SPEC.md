# Apogee GUI — Project Spec

The product spec: what the Apogee GUI is, what it is for, and the principles that shape it. The high-level plan and progress board are in [ROADMAP.md](ROADMAP.md); the work queue is [`backlog/`](../backlog/README.md) (one document per pending item); shipped work is recorded in [MILESTONES.md](MILESTONES.md); how to work in the repo is in [CLAUDE.md](CLAUDE.md). The harness itself — the product the GUI fronts — is specced in [`cli/assistant/SPEC.md`](../../cli/assistant/SPEC.md).

---

## One-liner

**The Apogee GUI** is the desktop face of the harness: one native application per platform (macOS, Linux, Windows) that drives the `apogee` CLI over its stdio machine mode — the harness's capabilities rendered graphically, owned by the CLI, reimplemented nowhere.

## Background

Apogee was built CLI-first and docs-first, with the GUI designed in from the start as a *client*: the machine mode exists precisely so a front end can drive the CLI over stdin/stdout — a JSONL protocol with a published schema (`machine-schema.json`, printed by the binary and shipped in every archive), additive growth as law, and the whole capability surface behind it. The GUI effort opened 2026-10-20 (the user's call), after v0.1.9 shipped the CLI's desktop-parity capabilities. The shape this enables: the GUI is thin and honest — every session, model operation, read and long-running job is the CLI's own, so the CLI's test suite vouches for what the GUI shows.

## Core surfaces

1. **The desktop application** — one per platform (`darwin`, `linux`, `windows`), each the same product: Apogee's sessions and operations in a graphical shell, over the machine-mode child.

## Scope (in)

- **Driving sessions:** chat and execute over the machine-mode protocol — turns, streaming, thinking, tool narration, asks answered from the UI, cancellation.
- **Rendering the reads:** the CLI's `--output-format json` documents (models, chats, suites, check, agents, MCP, tasks, graphs …) as native views.
- **Long-running work:** the CLI's jobs (pulls, conversions, training, video) narrated with progress and cancel, through the protocol's own events.

_TODO:_ the first release's concrete feature cut is the first spike's to propose and the user's to settle.

## Non-goals

- **No capability of its own.** The GUI never calls a provider, stores a model, edits config outside the CLI's one path, or grows a feature the CLI lacks — a wanted capability is built in the CLI first, then rendered. (The repo's parity law.)
- **No HTTP path for local work.** Local front-ends are pipes; `apogee serve` is for remote deployments. The GUI spawns the binary; it does not connect to a local server. (The never-listens law.)

## Principles

- **Thin client, honest renderer.** The GUI shows what the protocol says — states, errors and refusals in the CLI's own words — and invents nothing on top.
- **Additive protocol, tolerant client.** Unknown events and fields are tolerated, never fatal; a GUI version never requires a breaking protocol change (the CLI's ADR 0006, consumed).
- **Native per platform.** Each app feels at home on its OS — the macOS app is Swift and SwiftUI (the user's call, 2026-10-20). Whether linux/windows share anything beyond ADR 0001's shape is decided when their work opens.

## Success criteria

_TODO:_ written when the first release's feature cut is settled — phrased as observable outcomes (a user can open a chat, drive a turn, and cancel it, entirely through the GUI, with the CLI's own session files as the record).
