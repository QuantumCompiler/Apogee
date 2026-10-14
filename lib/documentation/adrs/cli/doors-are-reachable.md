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

- **Pending, 2026-10-14.** A real-terminal case walks every `View` row's door on a fresh install and asserts each recorded condition's refusal against its wording. It refuses to pass on an empty row list, and must see the execute door specifically. It lands with track 44's **44d**, the reachability law, in the [backlog](../../backlog/README.md). Until then the classification test is this record's only check, and it cannot see an absent door.
