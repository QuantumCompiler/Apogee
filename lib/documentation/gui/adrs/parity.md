# ADR 0003 — Every CLI capability has a classified place in the GUI

**Status:** Accepted · **Date:** 2026-10-20

## Context

The user's first hands-on run of the built app, 2026-10-20, found the structural hole. The GUI covered the design's 29 screens, but nothing held it to the CLI's whole surface. An audit against the TUI's own classification (`lib/src/cli/source/presentation/cli/tui_parity.cpp`, the CLI's ADR 0010) measured **8 whole command families with no GUI place**: `knowledge`, `embed`, `symphonies`, `train`, `datasets`, `agents`, `mcp` and `review`. It also found partials (execute sessions, prose graphs, task recipes, `analyze`) and no recorded carve-outs.

The TUI never drifted this way because ADR 0010 made its coverage mechanical: one table, a test against the binary's own commands, a refusal for every carve-out. The user's mandate the same day, that "the GUI is meant to be able to do everything and anything that the CLI and TUI does", became the GUI SPEC's **Full parity** principle. This record is its teeth.

## Decision

**Every top-level command the CLI offers a person has a classified place in the GUI, in one table:** `ParityTable` (`lib/src/darwin/source/presentation/shell/ViewModels/Parity.swift`), the GUI's `tui_parity`. Each command is exactly one of:

- **a screen**, named by the module that draws it;
- **the console** (2b), where the command runs as typed;
- **a gap**, named by its queued backlog item, never "later";
- **a carve-out**, with the reason the console refuses it by;
- **plumbing**, a protocol other programs speak and never a person's page.

**The carve-outs** are the TUI's three, each with the GUI's own reason:
- `serve`: a daemon the app would orphan;
- `reset`: it deletes the data directory the running app stands on;
- `uninstall`: it removes Apogee and its data directory, and the app never uninstalls the binary it drives.

**The table is held to the binary's own word list**: the top-level words `apogee __complete ""` answers, recorded as a fixture by `scripts/refresh-parity-fixture.sh` and refreshed deliberately, never fetched live by a test (hermetic, like the protocol fixture).

## Consequences

- A command a new CLI adds fails the app's suite the day the fixture is refreshed against that CLI, until it is classified. One the CLI drops fails it too.
- A gap row names its item, and the test holds that item to the backlog index. When the item ships and its row leaves the index, the test fails until the table names the screen that now draws it. The table cannot claim a gap is queued once it isn't.
- A screen row names a real module, so the table cannot point at a screen that was never built (the CLI's ADR 0012, doors reachable, adopted).
- The classification is top-level. A command's verbs inside a covered family, such as `models convert` under Models, are each screen item's own scope (2h and 2i found that gap). The console, which runs any verb as typed, is what guarantees reach in the meantime.

## Enforcement

`lib/src/darwin/tests/presentation/shell/ParityTests.swift`, in the shared scheme:
- every fixture word is placed;
- an unclassified word fails by name, as does a classified one the binary no longer has;
- an empty word list never passes;
- every screen's module has its folder;
- every queued item is a row of the GUI backlog's index;
- the census is pinned.

The GUI's CLAUDE.md checklist carries the rule: a change that adds or removes a CLI command classifies it here in the same change.
