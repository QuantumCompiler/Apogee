# Apogee GUI — Backlog

**The work queue: one document per work item.** This directory holds every GUI feature and fix that has been discussed and specced but **not yet built** — the layer between the loose ideas in [ROADMAP.md](../assistant/ROADMAP.md) and the shipped record in [MILESTONES.md](../assistant/MILESTONES.md). There is no separate TODO file: an agent takes an item from the index below and **implements it straight from its document**. (The CLI's queue is its own, at [`cli/backlog/`](../../cli/backlog/README.md); an item that needs a CLI change gets a sibling document there, gated both ways.)

## Lifecycle

1. A feature is **discussed in chat** and gets a line on [ROADMAP.md](../assistant/ROADMAP.md) (and a [SPEC.md](../assistant/SPEC.md) update if it changes product shape).
2. Once specced — seam, files, decisions, open calls — it gets **its own document here** (kebab-case filename, listed in the index below in priority order).
3. When an agent **takes** it (told explicitly, or via the bare-"Continue" convention: the topmost item whose gate is satisfied), the document **is** the working spec — confirm scope with the user first (the document's **Open calls** are the natural questions), mark its index row **in progress** (🚧), and build. One item at a time.
4. When it **ships**, the work is recorded in [MILESTONES.md](../assistant/MILESTONES.md) per the docs flow in [CLAUDE.md](../assistant/CLAUDE.md), and the document is **deleted, along with its index row** — a completed item must never remain in the backlog. On the same pass, update any gate the shipped work just satisfied.

**The standing invariant:** every document in this directory is pending work. If it's shipped (in MILESTONES.md), it's not here.

**Tags and gates** follow the CLI backlog's conventions: an open call is `[user]` (genuinely user-owned; blocks the build) or `[default: …]` (agent-decidable, with the recommendation recorded); a decision owned by another item is `(consumed decision)`; a row's Status is 🟢 workable, 🔒 gated on a named real item (no cycles), or 🚧 in progress.

## Document format

Every item document follows this shape (sections in order; omit one only when it's genuinely empty):

```markdown
# <Item title>

**What / why.** <The feature or fix in a paragraph: what it does for the user, and why it's worth building — evidence baked in.>

**Core constraint(s).** <The rules the design must not violate — invariants it touches, compatibility it must keep.>

**Seam + files.** <Where the change plugs into the codebase: the interfaces it extends and the files it touches.>

**Decisions made** (dated):
- <YYYY-MM-DD> — <a design decision and its rationale>

**Open calls:**
- <tagged questions, per the conventions above>

**Guardrail(s).** <What must be tested or checked so the change can't regress silently.>

**Acceptance criteria:**
- [ ] <observable outcome that means "done">

**Scope note.** <Status: unscheduled / earmarked for <version> / gated on <item> — and anything explicitly out of scope.>
```

## Index

Priority-ordered: the topmost item whose gate is satisfied is the next one to build.

| # | Item | File | Status |
|---|---|---|---|

*(The queue is empty — new items enter via ROADMAP.md → a document here. The first spike's split seeds it.)*
