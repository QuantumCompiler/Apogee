# Apogee design — Backlog

**The work queue for the design prototype: one document per work item.** The design (`lib/design/`) is the reference every Apogee GUI app is built from. This queue holds its work that has been specced but **not yet built**. It is the layer between [ROADMAP.md](../../assistant/design/ROADMAP.md) and the shipped record in [MILESTONES.md](../../assistant/design/MILESTONES.md). An agent takes an item from the index below and **implements it straight from its document**. The release-by-release view across every product is the [backlog overview](../README.md); this index is the authority on its rows, and every change to it reaches the overview in the same pass ([Keeping this page true](../README.md#keeping-this-page-true)).

An item that needs an app or the CLI to change gets a sibling in that queue ([the router](../../CLAUDE.md)), gated both ways. A cross-queue gate is written with the queue's name: `darwin 10a` here, `design 1e` there. On the [backlog overview](../README.md#keeping-this-page-true), the waiting row's "Waits on" names it the same way, and a tie into a later release is listed under its "Ties to later releases".

## Lifecycle

1. A change is **discussed in chat** and gets a line on [ROADMAP.md](../../assistant/design/ROADMAP.md).
2. Once specced, it gets **its own document here**, written as a ticket ([Document format](#document-format)): a kebab-case filename in its table's folder, listed in the index in build order. The same pass adds it to the [backlog overview](../README.md#keeping-this-page-true).
3. When an agent **takes** it, the document **is** the working spec:
   - confirm scope with the user first (its **Open questions**);
   - mark its row 🚧 and its ticket's header Status (🚧 In progress);
   - build. One item at a time.
4. When it **ships**, it is recorded in [MILESTONES.md](../../assistant/design/MILESTONES.md). The document and its row are **deleted**, and every item it gated has its index row and its ticket's header updated in the same pass: **Blocked by** loses the shipped id, **Status** turns 🟢 Ready when that was its last gate, and the shipped id leaves any **Blocks**. Its row leaves the [backlog overview](../README.md#keeping-this-page-true) too, with the counts, "can start today" and any "Waits on" it satisfied updated.

**The standing invariant:** every document here is pending work.

**Tags, gates and numbering** follow the Apple apps' queue ([its README](../darwin/README.md)):
- An open question's Status cell is `[user]` (blocks the build), `[default]` (the Answer is the recorded default: the agent takes it, records it in the Decision log, the user may veto) or `(consumed decision)` (owned by another item).
- A row's Status is 🟢 workable, 🔒 gated on named real items (no cycles), or 🚧 in progress. Items marked **split first** are groomed into their listed sub-documents before anyone builds them. A changed gate, a split, or a row moved to another table changes the [backlog overview](../README.md#keeping-this-page-true) in the same pass.
- Ids are a track number and a letter (`1a`), ever-assigned. An item joining a track takes the track's next letter, wherever it sits in the build order, and a vacated id stays spent, as the CLI's M5 and M6 do (1f, split on 2026-11-19 into 1u and 1v).

## Document format

The ticket format ([the Apple apps' README → Document format](../darwin/README.md#document-format)), with the key `DSN` (`DSN-1e`) and the Project `Design`; precedents to cite go in the **Reference.** paragraph of its Technical approach. The header restates the index row, the index staying the authority.

## Index

Build order: the topmost item whose gate is satisfied is the next one to build.

**Track 1, the design prototype in React** (1a–1v) has shipped, complete in **v0.2.1** — in development on its branch, released when it merges into `stable`. Opened 2026-11-14 from that day's brainstorm (the user's idea and calls), it rebuilt the design in React and TypeScript with the production GUIs' layers, reading the CLI's own recorded output, built into one `lib/design/main.html` that opens with a double-click, and retired the old one-file prototype (frozen at `b375fac9`); its law is the design's [ADR 0001](../../adrs/design/react-design-prototype.md). Its story is [ROADMAP.md](../../assistant/design/ROADMAP.md), and each item's record is in [Milestone A](../../assistant/design/MILESTONES.md#milestone-a--the-design-prototype-in-react). No version table is open: nothing is planned after v0.2.1 yet.
