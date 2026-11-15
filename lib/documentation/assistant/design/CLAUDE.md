# CLAUDE.md — the design

Context for agents working on **the design prototype**: the reference every Apogee GUI app is built from. It lives at `lib/design/`, and its queue is [`backlog/design/`](../../backlog/design/README.md). The apps themselves have their own homes ([the router](../../CLAUDE.md)).

## Where the design stands (2026-11-14)

The design is moving to a new form (the user's call, 2026-11-14, track 1 of [the queue](../../backlog/design/README.md#v021)):
- **Today:** one Design Component file, `lib/design/spec/html/main.html`, plus its `.dc.html` boards and the claude.ai design canvas (7,604 lines and 73 boards when measured the morning of 2026-11-14; both grew that day, and stop growing once 1c freezes them). How to work on it is [`lib/design/spec/CLAUDE.md`](../../../design/spec/CLAUDE.md), which stays the working guide until item 1t replaces it.
- **Where it is going:**
  - a React and TypeScript app at `lib/design/prototype/`, built with the same four layers and screen modules as the Mac app;
  - its view-models plain TypeScript that never imports React;
  - its data the CLI's own recorded documents;
  - built into one self-contained `lib/design/main.html` that opens with a double-click.
  - The canvas is retired once the new app covers every board.

The law of the new form is the design's ADR 0001, written by [1a](../../backlog/design/v0.2.1/design-app-architecture-and-law.md).

## The interim rule, while the port runs

[default, 2026-11-14, recorded in 1a]: the old prototype is **frozen** as the port's reference from item 1c's baseline on. It takes fixes only, never a new screen or state. New design waits for the React app's first screen (1e) and is drawn there.

An item already queued elsewhere that touches the design lands wherever the prototype is when it builds, and says so in its record. Those items are the Apple apps' 10a, 10c and 11a.

## The rules that don't change

The design's rules carry over unchanged into the new form ([`lib/design/spec/CLAUDE.md` → The design's rules](../../../design/spec/CLAUDE.md#the-designs-rules)):
- every part maps to the CLI;
- honest by default;
- every color is a token;
- keyboard first and accessible as drawn;
- copy in plain words.

## Working process

The docs-first lifecycle and its conventions are the CLI's ([`assistant/cli/CLAUDE.md`](../cli/CLAUDE.md)); this home states only its deltas. A change is specced in [the backlog](../../backlog/design/README.md), built from its document, and recorded in [MILESTONES.md](MILESTONES.md). Each step that changes the queue's index changes the [backlog overview](../../backlog/README.md#keeping-this-page-true) in the same pass. The plan is [ROADMAP.md](ROADMAP.md).

The design is not an input to any app's build, and CI never reads it. Its checks run locally before a commit (1b).
