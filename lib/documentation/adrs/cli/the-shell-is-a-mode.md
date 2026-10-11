# ADR 0010 — The shell is a mode

**Status:** Accepted · **Date:** 2026-10-10

## Context

[ADR 0002](mode-parity.md) opens "The CLI is driven three ways": a terminal, the HTTP admin plane, machine mode. On 2026-10-10 a fourth door shipped: the full-screen shell (bare `apogee` at a terminal, `apogee tui`, [Milestone AH](../../assistant/MILESTONES.md#milestone-ah--the-full-screen-tui)), with seven views — Session, Models, Chats, Suites, Config, Home, Keys. The same day's parity spike counted, off the installed binary in a sandboxed home:
- **25 root subcommands, 7 with a shell surface and 18 with none;**
- **118 leaf verbs, 24 carrying a machine read.**

Nothing held that gap honest. No record named the shell a mode, the per-change checklist had no line for it, and which commands deliberately get *no* view lived in chat history rather than in a test.

A second spike the same day set the floor this record stands on. It counted **119 leaf surfaces carrying 434 flags**, which rules out a form per verb at that scale: the hand-built-views anti-pattern that took a sibling project's TUI down under its per-view parity burden. It also proved the structural answer live. One exec line, speaking the CLI's own grammar and running each typed command as a captured child of the shell's own binary, makes every verb and flag work in the shell by construction ([37h](../../backlog/README.md)).

## Decision

**The shell is a mode. Every root subcommand has its place in it classified, in one table, and a subcommand shipped without its place classified fails a test.**

- **The table** lives in `lib/src/cli/source/presentation/cli/tui_parity.cpp`, one row per root subcommand, the hidden plumbing included. Each row has one of four classes:
  - **view**: a view draws it, named;
  - **backfill**: a pending item will draw it, named;
  - **runner-covered**: no curated view, and the exec line is its surface;
  - **carved out**: no surface at all, with a recorded reason.
- **A view computes nothing.** Its read is the command's own rows, carved into a function the command and the view share. Its mutation is the command's own core, leaving byte-identical files: the 32d structure, held by each view's byte-parity tests.
- **The exec line is the parity floor.** Every verb and flag runs in the shell through it, byte-identical to a script's run. A curated view is additive polish over that floor, never the only way in.
- **A carve-out is a refusal with its reason.** The table carries the exec line's refusals as a second list, one row per refused word or verb, with the reason it says. The exec line reads them from there and from nowhere else:
  - the doors the shell already is (`chat`, `execute`, `tui`);
  - the `$EDITOR` verbs, because the editor needs the terminal the shell holds;
  - `reset`, `uninstall` and `serve`, the user's call of 2026-10-10: the first two delete the ground the shell stands on, and the third is a daemon it would orphan.
- **Classification is at the root subcommand.** The shell's unit is the view, so per-verb coverage lives in each view's own parity tests. The HTTP table's rows are verbs because HTTP twins are per verb.
- **The TUI stays strictly additive** (SPEC.md → Non-goals, the 2026-10-04 revision). The law classifies surfaces and never re-tiers a command, and every command off a terminal is byte-identical whether the shell exists or not.

## Consequences

- The cost of a user-facing feature includes its shell classification, the way [ADR 0002](mode-parity.md) prices in its machine event and its served read. A new subcommand is placed in the same change: drawn, named for a later item, or carved out with its reason.
- Backfills are honest debts. Each names the item that pays it, and that item flips its row to a view when it ships.
- "Few views, each earning its place" is a recorded judgment per command, not folklore. Revisiting a carve-out is a deliberate edit of one row.

## Enforcement

- `tests/presentation/cli/tui_parity_test.cpp` builds the real command registry in-process and holds it to the table through `shell_law_violations`. It fails naming each of these:
  - an unclassified subcommand;
  - a row for a subcommand that no longer exists;
  - a refusal naming words no command answers to;
  - a carve-out with no recorded refusal.

  It fails on an empty registry too, never passing vacuously, and a planted fake subcommand proves it names what it finds.
- The 32d byte-parity tests hold each view to its command, and the leak test searches each view's rows for a planted key.
- The per-change checklist in [CLAUDE.md](../../assistant/CLAUDE.md) names the rule. [DEVELOPER.md](../../assistant/DEVELOPER.md#adding-a-tui-view) carries the recipe for adding a view.
