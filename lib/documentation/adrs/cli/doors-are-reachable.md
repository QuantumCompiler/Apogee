# ADR 0012 — A classified door is reachable

**Status:** Accepted · **Date:** 2026-10-14

## Context

[ADR 0010](the-shell-is-a-mode.md) classifies every root subcommand's place in the shell, in one table (`cli/tui_parity.cpp`). `tui_parity_test` holds the table to the registered commands: an unclassified subcommand, a stale row or a refusal for nothing fails it.

The CLI↔TUI disparity audit (2026-10-14) found the gap that leaves. `execute` was classified as a view, `View("Session (the picker's execute door)")` (`tui_parity.cpp:28`). The exec line's refusal of `execute` pointed there too: "open an execute session from its picker" (`tui_parity.cpp:61–63`). Yet the door was often absent:
- **On a fresh install there is no picker at all**, so no execute row. `TuiSessionDriver::start()` opens straight into a chat until a saved chat exists.
- **With a picker but no suite and no default backend, there is no execute row either.**

The law was green over a missing door, because it tests classification, not reachability. A classification is a promise to a user that the shell has a way in. Nothing checked the promise on the install a new user has.

## Decision

**A `View` classification asserts a door reachable on a fresh install.**

- **A fresh install** is a data directory as `apogee check --fix` seeds it, with nothing else: no suite, no saved chat, nothing configured beyond the starter.
- **A door is reached** when its view is shown by the shell's own keys (its number, or its Go-to entry) and what the row names is visible there: a picker's row, a view's title, the exec line opening.
- **A genuinely conditional door either becomes unconditional, or its row records the condition.** Where the door is absent, the shell says the condition in the words the row records. A door absent in silence is the defect.
- **The table stays the one source** (ADR 0010). The reachability check reads the classification rows, so a row added to the table is covered with nothing to remember.

## Consequences

- A `View` row is a promise about what a new user can reach, not a pointer into code.
- A condition is a visible, recorded debt rather than a silent gap.
- Each `View` row costs a step in a real-terminal walk on every POSIX build.

## Enforcement

- **Since 2026-10-14 (44d)** each `View` row names its door: a `ShellDoor` with the view's title, what that view shows for this door and, for a deliberately conditional door, the condition, `seen` then being the words that say it. The law (`shell_law_violations`) names a view with no door, a condition with nothing said where the door is absent, and a door on a row that is no view. `tui_parity_test` plants each and requires it named.
- **`cli.tui_doors`** (`tests/scripts/py/pty_door_check.py`) walks the doors on a real terminal:
  - it reads the view rows from the source the binary is built from, so a row added is walked with nothing to remember;
  - it seeds a data directory with `apogee check --fix` and nothing else;
  - in one boot, it reaches every door's view through the Go-to menu and requires what the row says it shows to be there.

  It fails on a view whose door it cannot read, on a table with no views, and on a table without the execute door. A planted conditional execute door failed it naming the row: "the Session view never showed 'Execute' on a fresh install".
