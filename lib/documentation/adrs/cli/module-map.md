# ADR 0008 — The module map is the layer law's one declaration

**Status:** Accepted · **Date:** 2026-10-03

## Context

ADR [0001](layer-enforcement.md) fixed the four layers and the rule between them, and named their packages in prose, before the module set settled. Its list is short of what shipped: it places `transport` in Infrastructure, where A1 put it in Data (it speaks the contracts' cancellation token and errors); it names `commands`, which A3 split into `cli`, `views` and `machine`, beside which A4 carved `operations`; and it omits `agent`, `mcp` and `scaffold`. Since A4 (2026-10-03) one file declares every module's layer and links -- `lib/src/cli/cmake/modules.cmake` -- and both the build and the layering test read it ([Milestone AA](../../assistant/MILESTONES.md#milestone-aa--the-four-layers)).

## Decision

**`cmake/modules.cmake` is the one declaration of which module sits in which layer, and this table restates it.** ADR 0001's rule stands unchanged: a module depends only on modules in its own layer or in **any** layer below it -- not only the adjacent one, because Presentation reads `contracts/` directly and that is the graph as built. This record supersedes 0001's package list alone.

| Layer | Modules | May depend on |
|---|---|---|
| Presentation | `markdown`, `render`, `views`, `machine`, `operations`, `httpserver`, `cli` | Presentation, Business, Data, Infrastructure |
| Business | `harness`, `agent`, `agentloop`, `knowledge`, `graph`, `tools`, `mcp`, `models`, `training`, `tasks`, `scaffold` | Business, Data, Infrastructure |
| Data | `contracts`, `transport`, `modelstore`, `logger`, `secrets`, `embedstore`, `backends` | Data, Infrastructure |
| Infrastructure | `platform`, `events`, `ansi`, `version` | Infrastructure |

Which modules a module links, within those bounds, is the map's business: each row there is reviewed where it changes.

## Consequences

- A new module is a row in the map and a name in this table, in the same change; either alone fails the check below.
- A module moving layers is a new record superseding this one -- the table is history at the date it holds.
- Tightening the rule to adjacent layers only would be a new ADR with its migration, not an edit here.

## Enforcement

`harness.layer_context` (`lib/src/cli/tests/scripts/cmake/layer_context.cmake`) holds this table equal to `cmake/modules.cmake` -- each layer's modules, the layer order and the may-depend-on column -- and holds ADR 0001's numbered layers and its rule sentence to the same map. The map itself is enforced by the build (one library per module, an include up a layer does not compile, `cmake/ApogeeLinkPolicy.cmake` at configure) and by `harness.layering` (the includes held to the map in both directions).
