# The Business layer

**Position.** Third of the four ([ADR 0001](../../../../documentation/cli/adrs/layer-enforcement.md)): what Apogee does -- the Harness and its router, the shared loop, tools, knowledge, the graph, models, training, tasks, symphonies. Behavior lives here once; every mode renders it ([ADR 0002](../../../../documentation/cli/adrs/mode-parity.md)).

**Modules:** `harness`, `agent`, `agentloop`, `knowledge`, `graph`, `tools`, `mcp`, `models`, `training`, `tasks`, `symphony`, `scaffold`, `forge`, `review`, `video` -- one static library each, `apogee_business_<module>`, linked as its row in [`cmake/modules.cmake`](../../cmake/modules.cmake) says ([ADR 0008](../../../../documentation/cli/adrs/module-map.md)).

**May include:** this layer, Data and Infrastructure.

**Enforced by:** the build -- a module sees its own layer's include root and those its links bring up, so an include up a layer does not compile, and the link policy refuses a link up, a cycle or an undeclared link at configure -- and [`harness.layering`](../../tests/scripts/cmake/layering.cmake), which holds every include of another module to the map and keeps the named rules. The rules here, finer than a layer: `harness/`, `agentloop/`, `agent/`, `tools/`, `mcp/`, `knowledge/`, `graph/`, `training/`, `tasks/` and `symphony/` never include `backends/` -- a backend's knowledge crosses as plain data (`ModelBehavior`) or a capability interface; `knowledge/`, `graph/`, `training/`, `tasks/` and `symphony/` are domain cores held to allow-lists, never a surface, model calls arriving as closures or, for a symphony's stages, through the one member call. `forge/` (48a: spawned `gh`/`glab`) includes only `contracts/`, `platform/` and itself; `review/` (48c) only `contracts/`, `forge/` and itself; `video/` (49b) only `training/`, `modelstore/`, `contracts/`, `platform/` and itself. The code graph (`graph/code_*`, 27k) is narrower still -- itself, `embedstore/`, `contracts/sha256.h` and `contracts/cancellation.h`, so no model is reachable from it -- and `graph/code_parser.cpp` alone includes tree-sitter.

**Changing this layer:**
- One implementation per decision chain; a second copy of one for another mode is the defect ([ADR 0002](../../../../documentation/cli/adrs/mode-parity.md)).
- State this layer writes -- stores, sessions, records -- is read compatibly or migrated by a named command ([ADR 0006](../../../../documentation/cli/adrs/backwards-compatibility.md)).
- A new module: its directory, its row, its sources in this layer's `CMakeLists.txt`, its tests in `tests/business/<module>/` ([ADR 0003](../../../../documentation/cli/adrs/granular-modules.md), [ADR 0004](../../../../documentation/cli/adrs/tests-mirror-architecture.md)).

**Depth:** [the ADRs](../../../../documentation/cli/adrs/README.md) · [CLAUDE.md](../../../../documentation/cli/assistant/CLAUDE.md) (→ "The harness never includes backends") · [DEVELOPER.md](../../../../documentation/cli/assistant/DEVELOPER.md).
