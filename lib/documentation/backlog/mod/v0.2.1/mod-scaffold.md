# MOD-1a · The mod scaffold: a validating plugin with the law at birth

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | Claude plugin (mod) |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.1 |
| **Component** | The plugin skeleton (`lib/src/mod/`) |
| **Labels** | `scaffold` `layering-check` `binary-discovery` |
| **Epic** | Track 1 · the mod is born |
| **Blocked by** | — |
| **Blocks** | [1b](mod-tools-and-commands.md), [1d](mod-pipeline.md) |
| **Reporter** | The mod spike (2026-10-21) |
| **Created** | 2026-10-21 |

---

## Summary

Nothing of the mod exists yet, while the toolchain and the contracts it needs are proven. This item makes the plugin real: the skeleton, the four layers with the layering check at birth, binary discovery with the not-installed guard, and the wiring (`tsc` and `claude plugin validate` as the local gates, `changed.sh` gaining the `mod` entry).

## Description

### User story

**As** a contributor to Apogee,
**I want** the mod to start as a validating plugin whose four layers are checked from the first commit and which finds the Apogee binary or says plainly that it is missing,
**so that** every later mod item builds on a skeleton that obeys the layering law and never crashes when Apogee is not installed.

### Background

The first item of track 1 and ungated. The plugin it creates is:

- **The skeleton.** `.claude-plugin/plugin.json` (plain `apogee` naming, per the house rule), `hooks/hooks.json` → `register.tsx` as the composition root, and `types/` as needed.
- **The four layers with the layering check at birth.** [ADR 0001](../../../adrs/mod/layer-enforcement.md)'s enforcement clause: a TypeScript import-graph check holding each file to its layer's row, `$`-API confinement included (`$.process` is Infrastructure's, `$.ui` is Presentation's), failing upward edges by name, and refusing an empty graph.
- **Binary discovery with the not-installed guard.** The GUI 1b ladder pattern: known channel paths plus an override atom. Absence is a typed, toasted condition with the install hint — never a crash.
- **The wiring.** `tsc` and `claude plugin validate` as the local gates; `lib/scripts/changed.sh` gaining the `mod` entry (what the mod is built from, declared once).

### Evidence

1. **Nothing of the mod exists** (the mod spike, 2026-10-21): no `lib/src/mod/`, no `changed.sh` entry, no docs beyond this home.
2. **The toolchain is proven on the dev machine** (the mod spike, 2026-10-21): `claude plugin validate` answers.
3. **The contracts the mod needs are measured live** (the mod spike, 2026-10-21: the toolchain and contract measurements).

## Acceptance criteria

- [ ] **Given** the plugin folder, **when** `claude --plugin-dir lib/src/mod` runs, **then** the plugin loads; with no binary, it states the condition once, with the hint.
- [ ] **Given** planted upward edges and misplaced `$`-API use, **when** the layering check runs, **then** it fails each one by name.
- [ ] The four layers exist; `changed.sh mod` exists; `tsc` and validate are clean.

## Constraints

- **The ⚠ client invariant from birth.** Nothing in the skeleton opens a port or parses prose where a document exists.
- **One honest visible behavior.** Loaded with no binary, the scaffold says so once and stays quiet.
- **The four-layer law at birth.** The layering check ships with the layers, per [ADR 0001](../../../adrs/mod/layer-enforcement.md)'s enforcement clause.
- **Never a crash.** A missing binary is a typed, toasted condition with the install hint.
- **Plain `apogee` naming.** The product name is `apogee`, never a suffixed variant (`plain-apogee-naming`).
- **Clean gates.** `plugin validate` and `tsc` clean.

## Technical approach

| File | Change |
|---|---|
| `lib/src/mod/` | the whole plugin, in the layout DEVELOPER.md plans (new) |
| `lib/src/mod/.claude-plugin/plugin.json` | the plugin manifest; `name` is `apogee` |
| `lib/src/mod/hooks/hooks.json` | points at `register.tsx` |
| `lib/src/mod/register.tsx` | the composition root |
| `lib/src/mod/types/` | as needed |
| `lib/scripts/changed.sh` | the `mod` entry: what the mod is built from, declared once |
| `.gitignore` (repo) | the plugin's build outputs |

The layering check is a TypeScript import-graph check: each file is held to its layer's row, `$.process` is confined to Infrastructure and `$.ui` to Presentation, upward edges fail by name, and an empty graph is refused. Binary discovery mirrors the GUI's ladder: known channel paths plus an override atom; when nothing is found, the condition is typed and toasted once with the install hint.

**Reference.** The mod spike (2026-10-21) — the toolchain and contract measurements; the GUI's 1a — teeth-at-birth and the discovery ladder, mirrored; `plain-apogee-naming` — the product name is `apogee`, never a suffixed variant.

## Testing

Hermetic tests, under `lib/src/mod/tests/` mirroring the layers.

- **The layering check.** Verified against planted violations (the house bar): planted upward edges and misplaced `$`-API use fail by name.
- **The discovery/guard matrix.** Over scripted `$.process` results.
- **`changed.sh mod`.** Answers correctly for mod-only and docs-only diffs.
- **Gate:** `tsc` + `claude plugin validate` clean (the local gates).

## Open questions

| Question | Status | Answer |
|---|---|---|
| How is the layering check implemented? | `[default]` | an import-graph script in the test suite, written at build, with the CLI's and GUI's checks as the reference |
| What is the plugin's `name` field? | `[default]` | `apogee` |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-10-21 | With the spike: `lib/src/mod/`, this docs home, the four-layer law regardless of language; the skills grown to three homes the same day | the user |

## Out of scope

- Tools and commands ([`mod-tools-and-commands.md`](mod-tools-and-commands.md))
- The pane (1c)
- CI (1d)
- Any CLI change (the CLI's track 54 is the sibling)

## Links

- **Blocked by:** — · **Blocks:** [1b](mod-tools-and-commands.md), [1d](mod-pipeline.md)
- **Sibling:** the CLI's track 54
- **Relates to:** [ADR 0001](../../../adrs/mod/layer-enforcement.md) (the enforcement clause); the mod spike (2026-10-21); the GUI's 1a (teeth-at-birth, the discovery ladder) and 1b (the ladder pattern); `plain-apogee-naming`
