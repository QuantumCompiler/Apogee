# CLI-60e · Maintaining the app integration: the ADR and the contributor rules

| Field | Value |
|---|---|
| **Type** | 🛠️ Task |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.4 |
| **Component** | ADRs and contributor docs (the app integration) |
| **Labels** | `adr` `contributor-docs` `app-integration` `census-test` |
| **Epic** | Track 60 · one tool definition and device tools |
| **Blocked by** | [60d](building-an-app-on-apogee.md) |
| **Blocks** | — |
| **Reporter** | The user (2026-11-14, while the apps-as-clients split was being written) |
| **Created** | 2026-11-14 |

---

## Summary

Tracks 59 and 60 make Apogee something other apps build on, but none of their items makes the integration a standing rule that every later change must keep. This item writes that law as ADR 0013 with its enforcement named, adds a census check that holds it mechanically, and updates the CLI contributor docs and the docs-pass skill so the next contributor (or agent) maintains it without being told.

## Description

### User story

**As** a contributor to Apogee (or an agent working on it),
**I want** the app integration written down as a law, with a check that fails when a new capability is not classified for apps,
**so that** a verb, read, tool or backend kind added after v0.2.4 reaches outside apps through both doors, inside its scope, without anyone having to remember it.

### Background

The user's call, 2026-11-14, said while the apps-as-clients split was being written: *"Make sure there is a task to update adrs / claude documentation about how to maintain this integration."*

Tracks 59 and 60 make Apogee something other apps build on:
- paired apps, each held to a scope ([59f](../v0.2.3/paired-apps-with-scopes.md));
- discovery of what an app may use ([59g](../v0.2.3/capability-discovery.md));
- structured output on every door ([59h](../v0.2.3/structured-output-on-every-door.md));
- an app's own tools on both doors ([60c](app-tools-on-every-door.md));
- the client core as a Swift package (darwin [7d](../../darwin/v0.2.4/apogeekit-swift-package.md));
- the integrator's guide and its compatibility promise ([60d](building-an-app-on-apogee.md)).

Each item builds its part. **None makes the integration a standing rule** that every later change must keep.

The repo's existing laws have exactly this shape, and each is held by an ADR plus a checklist line:
- parity across modes: [ADR 0002](../../../adrs/cli/mode-parity.md);
- the shell's classification of every subcommand: [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md);
- the GUI's parity table: [the GUI's ADR 0003](../../../adrs/gui/parity.md).

This item writes the integration's law the same way, with its enforcement named. It also updates the contributor docs and the docs-pass skill, so the next contributor (or agent) maintains it without being told.

### Evidence

1. **The gap.** A new verb, a new read, a new tool or a new backend kind added after v0.2.4 would compile, pass its own tests, and quietly never reach an outside app:
   - no scope class would name it;
   - discovery would not list it;
   - one door would carry it and the other not;
   - the package would not know it.
2. **The precedent.** `cli/tui_parity.cpp` and [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md) are a classification table with a test against the registered commands; `tui_parity_test` and the HTTP `parity_test` fail today when a command is added without a row.
3. **The HTTP parity table already lists every leaf command**: its 157-command census, `tests/presentation/httpserver/parity_test.cpp`.
4. **The ADR number.** **0013** is the next free number in the CLI's series.

## Acceptance criteria

- [ ] ADR 0013 exists, is in the index, and names its enforcement by file.
- [ ] **Given** a command added without its app classification, **when** the census test runs, **then** it fails, naming the command.
- [ ] The CLI contributor guide states the rule and the per-change duty, DEVELOPER.md has the recipe, and the docs-pass skill audits it.
- [ ] The full suite is green; the docs validate.

## Constraints

- **ADRs are append-only** ([the ADR index's format](../../../adrs/cli/README.md)): a new record, numbered **0013** (the next free number in the CLI's series), never an edit of 0002 or 0006. It cites them as the rules it extends.
- **A rule with no teeth is a suggestion.** The record's Enforcement names the checks 59f–60d built, and this item adds the census check that holds the law mechanically (Technical approach).
- **The anti-goal stands in the record.** The integration is an add-on, and nothing in it may make the CLI or the Mac app need a server, a network or an account (the user's call, 2026-11-07).
- **Additive** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)): the record restates, for outside apps, that the protocol, the discovery document and the served tools grow only by addition.
- **One home per fact.** The integrator-facing *how* stays in 60d's guide (`reference/cli/apps.md`). The ADR holds the *rule* and the contributor guide the *per-change duty*, each pointing at the other.

## Technical approach

| File | Change |
|---|---|
| `lib/documentation/adrs/cli/apps-as-clients.md` (new) | **ADR 0013, "Every capability reaches an outside app through both doors, inside its scope."** Context, Decision, Consequences and Enforcement, as below |
| `adrs/cli/README.md` | gains ADR 0013's row |
| the census table (`presentation/httpserver/parity_test.cpp`'s, extended) | classifies every registered command and served read for apps: its scope class and its doors, or the reason it is withheld |
| `lib/documentation/assistant/cli/CLAUDE.md` | a new section, "Other apps are clients too"; checklist lines in **Implementing a Feature**; **Documentation and Status** names the guide |
| `lib/documentation/assistant/cli/DEVELOPER.md` | a recipe, "Adding a capability an app can use" |
| `.claude/skills/apogee-maintenance-update-documents/SKILL.md` | the pre-merge audit gains a step for the integration |

**ADR 0013's sections:**
- **Context**: the 2026-11-14 spike's walls W1–W7 and the user's calls.
- **Decision**:
  - a capability a person can use is classified for apps: its scope class (59f), its discovery entry (59g), its door on the OpenAI plane and on Apogee's protocol (or a recorded reason one door lacks it), and its support in the Swift package (7d);
  - every request is checked against the app's scope at every door;
  - discovery, structured output and the package grow only by addition.
- **Consequences.**
- **Enforcement**: 59f's scope checks, 59g's discovery tests, 60d's guide conformance, and the census below.

**The census check.** One table, beside the existing parity tables, that classifies every registered command and served read for apps (its scope class and its doors, or the reason it is withheld). A test fails when a command or read is added without a row, as `tui_parity_test` and the HTTP `parity_test` do today. The table extends an existing one where that fits (`presentation/httpserver/parity_test.cpp`'s), rather than a fourth table, if the build finds that cleaner.

**The CLI contributor guide (`lib/documentation/assistant/cli/CLAUDE.md`):**
- a short section after "Development runs on the dev channel", "Other apps are clients too", stating the rule and linking ADR 0013 and 60d's guide;
- checklist lines in **Implementing a Feature**: a new or changed capability takes its scope class, its discovery entry, both doors (or a recorded reason) and the package's support, in the same change; a change to the protocol, discovery or a served tool's shape is additive and updates `reference/cli/apps.md`;
- **Documentation and Status** names the guide among what a ship updates.

**The DEVELOPER recipe**, "Adding a capability an app can use": the census row, the scope class, the discovery entry, the two doors, the guide and the package.

**The docs-pass skill's new step.** When the diff touches a command, a read, a tool or the protocol, check the census row, scope class, discovery entry, both doors, the guide and the package, as it checks the completion surface (ADR 0007) and the shell's classification (ADR 0010) today.

**Reference.**
- In-house: [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md) and `cli/tui_parity.cpp` (a classification table with a test against the registered commands, the precedent this census follows); [ADR 0002](../../../adrs/cli/mode-parity.md) (one implementation, every surface); [ADR 0006](../../../adrs/cli/backwards-compatibility.md) (additive growth).
- The siblings this item maintains: 59f, 59g, 59h, 60c, 60d and darwin 7d. Its darwin sibling is [7e](../../darwin/v0.2.4/maintaining-apogeekit.md), the same duty for the package's side.

## Testing

- **The census test.** A planted command with no row fails, naming it; the census test is pinned on a planted unclassified command.
- **The docs validator.** Holds ADR 0013's index row: the ADR is held to the index.
- **The docs-pass skill's audit.** Holds the checklist lines, on the first branch after this ships.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Which number and file does the record take? | `[default]` | The record is ADR 0013, `apps-as-clients.md`; if another record has taken 0013 by the time this builds, the next free number. |
| Where does the census live: a new table, or an existing one? | `[default]` | The census extends the HTTP parity table with an "apps" classification where that table already lists every leaf command (its 157-command census, `tests/presentation/httpserver/parity_test.cpp`), rather than a new table. |
| When does the docs-pass skill's new step run? | `[default]` | Only when the diff touches a command, a read, a tool or the protocol, as its completion-surface step does. |
| The scope classes themselves | `(consumed decision)` | 59f |
| The discovery document's shape | `(consumed decision)` | 59g |
| The guide's location and its compatibility promise | `(consumed decision)` | 60d |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | A task to update the ADRs and the contributor docs on how to maintain the integration ("Make sure there is a task to update adrs / claude documentation about how to maintain this integration."). It is the closing item of v0.2.4's app work, after 60d, because the law names checks that 59f–60d build. | the user |
| 2026-11-14 | A cross-queue pair: this item holds the CLI's half (its ADR, its contributor docs, the census and the docs-pass skill), and darwin 7e holds the package's half. Each queue owns its own docs. | the spec |
| 2026-11-14 | Taylor'd Portfolio's own side is not specced now. | the user |

## Out of scope

- Taylor'd Portfolio's own side (not specced now, the user's call, 2026-11-14).
- Any change to the integration's behavior. This item writes the rule; it does not move it.

## Links

- **Blocked by:** [60d](building-an-app-on-apogee.md) (and through it on [59g](../v0.2.3/capability-discovery.md), [60c](app-tools-on-every-door.md) and darwin [7d](../../darwin/v0.2.4/apogeekit-swift-package.md)) · **Blocks:** —
- **Sibling:** darwin [7e](../../darwin/v0.2.4/maintaining-apogeekit.md), the same duty for the package's side.
- **Relates to:** [59f](../v0.2.3/paired-apps-with-scopes.md), [59g](../v0.2.3/capability-discovery.md), [59h](../v0.2.3/structured-output-on-every-door.md), [60c](app-tools-on-every-door.md), darwin [7d](../../darwin/v0.2.4/apogeekit-swift-package.md), [60d](building-an-app-on-apogee.md); [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0006](../../../adrs/cli/backwards-compatibility.md), ADR 0007, [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md), [the GUI's ADR 0003](../../../adrs/gui/parity.md), [the ADR index](../../../adrs/cli/README.md).
- **Scheduling:** earmarked for v0.2.4 (track 60).
