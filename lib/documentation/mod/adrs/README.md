# Architecture Decision Records — the mod

The standing rules of the mod's architecture, each written once, dated, and kept beside the reasons it exists. An ADR is **append-only history**: a changed decision is a *new* record that names the one it supersedes. The planning system ([backlog](../backlog/README.md), [MILESTONES](../assistant/MILESTONES.md)) records *what happened when*; an ADR records *what holds now and why*.

**Format.** The house shape (the CLI's discipline): one file per record, named for the decision alone; **Status**, **Date**, **Context**, **Decision**, **Consequences**, **Enforcement** — a rule with no teeth is a suggestion.

**Scope.** This directory is the mod's (`lib/src/mod` — the Claude plugin). The CLI's records are at [`cli/adrs/`](../../cli/adrs/README.md), the GUI's at [`gui/adrs/`](../../gui/adrs/README.md).

## Index

| ADR | Rule |
|---|---|
| [0001](layer-enforcement.md) | Four layers, dependencies point down — regardless of language and framework |
