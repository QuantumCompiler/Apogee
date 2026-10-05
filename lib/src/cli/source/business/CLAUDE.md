# The Business layer

**Position.** Third of the four ([ADR 0001](../../../../documentation/adrs/cli/layer-enforcement.md)): what Apogee does -- the Harness and its router, the shared loop, tools, knowledge, the graph, models, training, tasks. Behavior lives here once; every mode renders it ([ADR 0002](../../../../documentation/adrs/cli/mode-parity.md)).

**Modules:** `harness`, `agent`, `agentloop`, `knowledge`, `graph`, `tools`, `mcp`, `models`, `training`, `tasks`, `scaffold` -- one static library each, `apogee_business_<module>`, linked as its row in [`cmake/modules.cmake`](../../cmake/modules.cmake) says ([ADR 0008](../../../../documentation/adrs/cli/module-map.md)).

**May include:** this layer, Data and Infrastructure.

**Enforced by:** the build -- a module sees its own layer's include root and those its links bring up, so an include up a layer does not compile, and the link policy refuses a link up, a cycle or an undeclared link at configure -- and [`harness.layering`](../../tests/scripts/cmake/layering.cmake), which holds every include of another module to the map and keeps the named rules. The rules here, finer than a layer: `harness/`, `agentloop/`, `agent/`, `tools/`, `mcp/`, `knowledge/`, `graph/`, `training/` and `tasks/` never include `backends/` -- what a backend knows crosses as plain data (`ModelBehavior`) or a capability interface; `knowledge/`, `graph/`, `training/` and `tasks/` are domain cores held to allow-lists, never a surface, with model calls -- a task's turns -- arriving as closures.

**Changing this layer:**
- One implementation per decision chain; a second copy of one for another mode is the defect ([ADR 0002](../../../../documentation/adrs/cli/mode-parity.md)).
- State this layer writes -- stores, sessions, records -- is read compatibly or migrated by a named command ([ADR 0006](../../../../documentation/adrs/cli/backwards-compatibility.md)).
- A new module: its directory, its row, its sources in this layer's `CMakeLists.txt`, its tests in `tests/business/<module>/` ([ADR 0003](../../../../documentation/adrs/cli/granular-modules.md), [ADR 0004](../../../../documentation/adrs/cli/tests-mirror-architecture.md)).

**Depth:** [the ADRs](../../../../documentation/adrs/cli/README.md) · [CLAUDE.md](../../../../documentation/assistant/CLAUDE.md) (→ "The harness never includes backends") · [DEVELOPER.md](../../../../documentation/assistant/DEVELOPER.md).
