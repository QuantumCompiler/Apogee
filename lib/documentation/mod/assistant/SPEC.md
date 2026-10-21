# Apogee Mod — Project Spec

The product spec for the Apogee mod. The board is [ROADMAP.md](ROADMAP.md); the queue is [`backlog/`](../backlog/README.md); shipped work lands in [MILESTONES.md](MILESTONES.md); how to work is [CLAUDE.md](CLAUDE.md). The harness is specced in [`cli/assistant/SPEC.md`](../../cli/assistant/SPEC.md).

---

## One-liner

**The Apogee mod** brings Apogee into Claude: a TypeScript plugin that offers the user's local models to Claude as tools, runs Apogee's commands as `/apogee` slash commands, and shows backends, models and training in a pane and status line — by driving the `apogee` binary on the user's own machine, never a port.

## Background

Apogee's capabilities reach people through its own front ends (CLI, TUI, the macOS app). The mod is the fourth surface, inside someone else's host: Claude sessions on the user's Mac, where Claude can hand work to local models (cheap summaries, a fine-tune's opinion, graph lookups) and the user can see and steer Apogee without leaving the conversation. The spike (2026-10-21) found the hard half already built: Apogee's MCP stdio server answers with 17 read-only tools today, every pane datum is a JSON document, and `complete -m` runs one-shot — the mod is mostly presentation over contracts that exist, plus the CLI's track 54 making the server public and (opt-in) model-calling.

## Core surfaces

1. **Model-callable tools** — Apogee's MCP-served tools (reads today; `complete` per the opt-in) registered into the session.
2. **`/apogee` slash commands** — check, models, train status, promote, rollback: each the real command, its result shown.
3. **The pane** — backends, loaded models, training progress, the version ledger; promote/rollback with confirmation.
4. **The status line** — the active backend/version/state at a glance.

## Non-goals

- **Not Claude's chat model.** A plugin cannot replace the model Claude runs on; chatting *with* a local model stays in Apogee's own front ends. The mod offers local models as tools.
- **No ports, no cloud sessions.** The mod drives the binary over pipes on the machine it runs on; it is not installed into cloud sessions, where the binary and models do not exist.
- **No capability of its own.** The repo's parity law: a wanted capability is built in the CLI first, then surfaced.

## Principles

- **A client of the binary** (the ⚠ invariant): documents and machine mode in, commands out, tolerant reads, no secrets.
- **Turns are never blocked**: long runs launch, toast and poll — a training run or a big completion must not stall the conversation.
- **Honest about absence**: no binary, no models, a cloud session — each a stated condition with its hint, never a crash.

## Success criteria

- _TODO:_ written when the first items ship — shaped as: Claude completes a prompt through a local backend mid-conversation; `/apogee check` shows the doctor's rows; the pane survives a training run without blocking a turn.
