# Architecture Decision Records — the GUI

The standing rules of the GUI's architecture, each written once, dated, and kept beside the reasons it exists. An ADR is **append-only history**: a changed decision is a *new* record that names the one it supersedes — never an edit that rewrites the old one. The planning system ([backlog](../backlog/README.md), [MILESTONES](../assistant/MILESTONES.md)) records *what happened when*; an ADR records *what holds now and why*, in one place a builder can cite.

**Format.** One file per record, named for the decision alone (`layer-enforcement.md`) — the ADR number lives in the document's title and this index, not the filename. Each record carries **Status**, **Date**, **Context** (the evidence that forced the decision), **Decision** (the rule, stated so a change can be judged against it), **Consequences**, and **Enforcement** (where the rule is mechanical — a test, the build, a pipeline gate — because a rule with no teeth is a suggestion). The CLI's discipline of layer summary cards at each source root carries over once the first application's tree exists: cards summarize and point; only ADRs carry rationale.

**Scope.** This directory is the GUI's (`lib/src/darwin`, `lib/src/linux`, `lib/src/windows` — one rule set across all three applications). The CLI's records are at [`cli/adrs/`](../../cli/adrs/README.md).

## Index

| ADR | Rule |
|---|---|
| [0001](layer-enforcement.md) | Four layers, dependencies point down — regardless of language and framework |
