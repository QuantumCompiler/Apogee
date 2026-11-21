---
name: apogee-backlog-execute-call-resolution
description: Resolve every open call in a backlog group's documents (each one's Open questions table), one by one — the user answers the user-owned calls, confirms or vetoes the recorded defaults, and each answer lands in its document as a dated Decision-log row with its consequences propagated, so the group builds later with nothing left to ask. Use when the user points at a group ("resolve the open calls in v0.2.0", "groom maintenance's calls", "groom the GUI backlog") — resolving which application's queue is meant, and asking when it cannot.
---

# Resolve a Group's Open Calls

A backlog group (one home's table: a version, `maintenance`, `architecture`, or the GUI index or a track within it) builds fastest when nothing in it is still a question. This skill is the standalone grooming pass: walk every document in the group, surface every row of its **Open questions** table, and resolve them **one by one** — each answer recorded where it belongs *before* the next question is asked. It changes documents, never code: `/apogee-backlog-execute-item` and `/apogee-backlog-execute-group` still do the building; after this pass, their ask-everything-first steps find nothing left to ask.

## 0. Which application?

Apogee's documentation is split by kind, then by platform ([the router](../../../lib/documentation/CLAUDE.md)): `lib/documentation/{assistant,backlog,adrs,reference}/<app>/`, where `<app>` is **`cli`** (the harness), **`darwin`** (the Apple apps, macOS, iPadOS and iOS, with an item for one OS under `darwin/macos/`, `darwin/ipados/` or `darwin/ios/`, and what binds every GUI app under `gui/`), **`mod`** (the Claude plugin), or **`design`** (the design prototype at `lib/design/`, the reference every GUI app is built from). `unix/` and `windows/` hold the apps not yet built. Resolve which application this invocation means before touching anything, in order:

1. **The arguments.** An id or table names its queue — the CLI's version tables (`v0.1.<x>`, `v0.2.0`) with their numeric tracks (`52a`) and the standing `M<n>`/`A<n>` queues; the GUI's and the mod's indexes with their lettered tracks. A named document resolves by which backlog holds it. A bare version can mean any queue — they all carry work per release.
2. **The conversation.** What the work touches: the harness's commands, backends and seams are the CLI's; screens and windows an app builds are the GUI's; the design prototype itself (`lib/design/`, drawing a screen before an app builds it) is the design's; the Claude plugin (panes, slash commands, `$`-API work, `lib/src/mod/`) is the mod's. Work that spans queues gets cross-queue siblings, gated both ways.
3. **Still ambiguous? Ask.** One AskUserQuestion — which application's queue (the CLI, the GUI, the mod, or the design) — before anything is treated as decided. Never guess between them.

Everything below reads from the chosen application's folders, written `<app>`: its backlog index (`lib/documentation/backlog/<app>/README.md`) is the authority on that queue's tables, numbering, document format and link depths, and its contributor docs (`lib/documentation/assistant/<app>/CLAUDE.md` first) carry the invariants. Where this skill says `<app>`, substitute the chosen application's folder name.

## 1. Ground and inventory

1. Re-read the live backlog README (`lib/documentation/backlog/<app>/README.md`) and `git status` — parallel sessions ship and take items; a 🚧 item's calls belong to its builder, so its document is **skipped and said**, and a placeholder with no document (a "still to be written" row) likewise.
2. Read **every document in the group's table**, in table order, and build the inventory: for each doc, its Open questions rows by their Status tag — `[user]` (blocks the build), `[default]` (agent-decidable, the default in its Answer, veto-able), `(consumed decision)` (owned elsewhere — verify the owner still exists; if it shipped, repoint the Answer to the MILESTONES record, that's the whole resolution).
3. Show the user the map before the first question: how many calls, which documents, which are user-owned — so they know the size of the sitting before it starts.

## 2. Resolve, one by one, in build order

Work through the documents in the table's build order, and within a document top to bottom. **An answer is recorded before the next question is asked** — the documents are never left holding a batch of unwritten decisions.

- **`[user]` calls get full attention, one at a time:** one AskUserQuestion each, phrased in plain architecture language — what the call decides, what each option costs, and the document's recommendation (when it has one) as the first, recommended option. No jargon, no stacking three decisions into one question.
- **`[default]` calls are confirm-or-veto, batched lightly:** up to four per AskUserQuestion round, each with "take the default (recommended)" first and the default's own wording (its Answer cell) shown. A veto becomes the user's call, recorded as such; a confirmation locks the default.
- **A call that evidence has mooted** (the feature shipped differently, the dependency vanished) is not asked — it is removed with a dated Decision-log row explaining why, and said in the report.
- **"Skip" is a real answer:** a call the user declines to decide stays open, stays tagged, and is listed in the report as still blocking — never silently dropped, never guessed instead.

## 3. Record each resolution completely

Per answer, in the document, immediately:

1. **Decision log** gains the dated row — the question and the choice in its Decision cell, whose call it was in Whose call (`the user`, or `default` for a default confirmed; the Date column carries the day).
2. **The row leaves the Open questions table** (a table left empty keeps one row, `None open`).
3. **Consequences propagate**: any constraint, Technical approach line, Testing line or acceptance criterion that branched on the call is rewritten to its decided form (the house precedent: when the config-format comments call landed on JSONC, the editor contract, migration golden, template line and acceptance criteria all flipped to the decided shape in the same pass — a decision recorded but not propagated is drift wearing a date).
4. **The flags come down elsewhere**: an index row, track preamble or ROADMAP line that advertised the call as blocking is updated to say it was answered, with the date (a changed row changes the document's header in the same pass) — the "**was answered the same day**" phrasing is the house style. The same goes for [the release overview](../../../lib/documentation/backlog/README.md#keeping-this-page-true): a row still waiting on the user's decision says so in its "Waits on" cell, an answered one no longer does, and its line on how many items wait on a decision stays true.

## 4. Validate and report

- The group's documents grep clean: no `[user]` Status cells remain except the deliberate skips; no `[default]` rows remain except those skipped or deliberately left for build time (say which and why).
- The standard index validation (rows ↔ docs, gates, links, and the release overview against the index) still passes — resolution edits documents, so the sweep proves nothing tore.
- Report: every call asked and its answer, one line each; the decisions recorded per document; the consequences that were propagated; the skips (🚧 docs, declined calls, missing documents) with what still blocks what; and the group's state now — ideally "nothing in this table waits on a question."
