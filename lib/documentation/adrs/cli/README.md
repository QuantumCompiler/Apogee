# Architecture Decision Records — the CLI

The standing rules of the CLI's architecture, each written once, dated, and kept beside the reasons it exists. An ADR is **append-only history**: a changed decision is a *new* record that names the one it supersedes — never an edit that rewrites the old one. The planning system ([backlog](../../backlog/README.md), [MILESTONES](../../assistant/MILESTONES.md)) records *what happened when*; an ADR records *what holds now and why*, in one place a builder can cite.

**Format.** One file per record, named for the decision alone (`layer-enforcement.md`) — the ADR number lives in the document's title and this index, not the filename. Each record carries **Status** (Accepted / Superseded by NNNN, or, when a later record replaces one part, Accepted with that part's supersession named), **Date**, **Context** (the evidence that forced the decision), **Decision** (the rule, stated so a change can be judged against it), **Consequences**, and **Enforcement** (where the rule is mechanical — a test, the build, a pipeline gate — because a rule with no teeth is a suggestion).

**Relation to the layer cards.** Each layer root in the source and test trees carries a short `CLAUDE.md` *summary card* ([A5](../../assistant/MILESTONES.md#milestone-aa--the-four-layers)) that states its layer's law in a few lines and links here — the card is the context a model gets for free; this directory is the depth it reaches for. Cards summarize and point; only ADRs carry rationale.

**Scope.** This directory is the CLI's (`lib/src/cli`). A sibling application (the planned GUIs) gets its own directory beside this one (`lib/documentation/adrs/<app>/`).

## Index

| ADR | Rule |
|---|---|
| [0001](layer-enforcement.md) | Four layers, dependencies point down |
| [0002](mode-parity.md) | A change propagates to every mode — CLI, HTTP, machine |
| [0003](granular-modules.md) | One concern per module; no monoliths |
| [0004](tests-mirror-architecture.md) | Tests live in the layer of what they test |
| [0005](install-mode-stability.md) | Changes never alter how Apogee installs or updates |
| [0006](backwards-compatibility.md) | Older versions keep working |
| [0007](tab-completion.md) | Tab completion is answered by the binary, from live state |
| [0008](module-map.md) | The module map is the layer law's one declaration |
| [0009](built-in-model-lists.md) | Model lists Apogee carries are kept current with their vendors |
| [0010](the-shell-is-a-mode.md) | The shell is a mode: every subcommand's place in it is classified |
