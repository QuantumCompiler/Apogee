# ADR 0009 — Built-in model lists are kept current with their vendors

**Status:** Accepted · **Date:** 2026-10-10

## Context

Since M13 (2026-10-08), the models a backend can run come from the vendor, live: an API's models endpoint at registration and `providers scan --refresh`, cached with the date they were fetched. Nothing is hardwired, because a list carried in the binary goes stale the day a vendor ships a model. Item 34 extended this to the Codex CLI, whose `codex debug models` prints its catalog.

Two vendor CLIs print no list at all: Claude Code and the Gemini CLI. On 2026-10-10 the user chose to carry lists for them in the binary (item 35), relaxing M13's rule for these two:
- **Claude Code**: Anthropic's published model ids, current and still available, as Anthropic's models overview listed them that day.
- **Gemini CLI**: the CLI's own aliases (`pro`, `flash`, `flash-lite`).

They show as rows in `models list`, resolve with `/model` and `-m`, and complete at the keystroke. A carried list goes stale in two ways, both silent until someone notices:
- A vendor ships a model, and the list under-reports what the user can run.
- A vendor retires one, and Apogee offers a model the API refuses. That breaks [ADR 0007](tab-completion.md)'s contract, that what completion offers the command accepts, from the vendor's side.

The same day, the user set the rule this record holds: the lists are updated during development whenever relevant, not when someone happens to notice.

## Decision

**A model list Apogee carries is kept as current as the vendor's, and the change that finds it stale is the change that updates it.**

- **One place.** Every carried list lives in `lib/src/cli/source/data/backends/model_roster.cpp` (`built_in_rosters()`), one table per backend type. It is never copied: completion, resolution, `models list`/`info`, `/models` and the TUI all read it through `known_rosters()`.
- **Only what the vendor publishes.** Each table names its source beside it:
  - Claude Code's: Anthropic's models overview, the current and still-available models by Claude API id;
  - the Gemini CLI's: its `GEMINI_MODEL_ALIAS_*` constants.

  No id goes in that the source does not show.
- **Each table carries the day it was last checked against its source** (its `…Reviewed` constant). Users see that date, as `reviewed <date>` in `models list` and `models info`, the way a fetched roster shows `fetched <date>`.
- **When a list is updated** — during development, by whoever is working when it becomes relevant:
  1. **A vendor ships, renames or retires a model.** Anthropic's models overview or deprecations page changes, or a new Gemini CLI alias appears: the rows change.
  2. **A change touches these backends, the rosters or model selection.** The lists are re-checked as part of that change, and the reviewed date moves even when no row does.
  3. **A CLI gains a command that lists its models.** Implement `CatalogListing` for its backend, as item 34 did for Codex, and delete its carried list in the same change. A fetched roster already wins, so the carried one would be dead weight.
  4. **Every release roll.** Both lists are checked against their sources before the version is cut.
- **An update is small and whole.** In the same change:
  - the rows change and the reviewed date moves;
  - `model_roster_test` follows where it names a changed id;
  - MILESTONES records what changed and from which source.
- **A new carried list is this record's to govern.** Another vendor CLI that prints no list joins the same table, with its source and date. It needs the user's call per vendor: "fetched where possible, carried only where the vendor offers no listing."

## Consequences

- A user sees how old each list is, and a stale one is visible rather than trusted.
- A stale list costs a missing row or a refused model, never a wrong answer. The vendor validates every model at its first turn, and its refusal is said as any turn's error is.
- The maintenance is a few lines per vendor release, done by the change that runs into it. No separate task is needed to remember it.

## Enforcement

- **Where work is judged.** The per-change checklist in [CLAUDE.md](../../assistant/CLAUDE.md) (Implementing a Feature) names the rule. Two skills check it:
  - the pre-MR docs pass (`apogee-cli-maintenance-update-documents`) audits the lists whenever a branch touches backends, rosters, completion or model selection;
  - the release roll (`apogee-cli-maintenance-update-release`) checks both lists against their sources at its gate.

  [DEVELOPER.md](../../assistant/DEVELOPER.md) carries the recipe.
- **Where users look.** The reviewed date is in `models list` and `models info`.
- **In the suite.** `tests/data/backends/model_roster_test.cpp` holds the tables' shape: carried, never saved, a fetch winning, the reviewed date a real date. It also names Anthropic's current lineup, so dropping one of those ids is a deliberate test edit, never a slip.
