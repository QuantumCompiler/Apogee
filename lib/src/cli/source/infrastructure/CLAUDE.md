# The Infrastructure layer

**Position.** The lowest of the four ([ADR 0001](../../../../documentation/cli/adrs/layer-enforcement.md)): what every other layer stands on. Infrastructure → Data → Business → Presentation, dependencies pointing down only.

**Modules:** `platform`, `events`, `ansi`, `version` -- one static library each, `apogee_infrastructure_<module>`, linked as its row in [`cmake/modules.cmake`](../../cmake/modules.cmake) says ([ADR 0008](../../../../documentation/cli/adrs/module-map.md)).

**May include:** this layer only; nothing sits below it.

**Enforced by:** the build -- a module sees its own layer's include root and those its links bring up, so an include up a layer does not compile, and the link policy refuses a link up, a cycle or an undeclared link at configure -- and [`harness.layering`](../../tests/scripts/cmake/layering.cmake), which holds every include of another module to the map and keeps the named rules. The rule here: `events/` is a leaf, including nothing from the project, so anything may publish to it.

**Changing this layer:**
- A new module: its directory here, a row in the map, its sources in this layer's `CMakeLists.txt`, its tests in `tests/infrastructure/<module>/` ([ADR 0003](../../../../documentation/cli/adrs/granular-modules.md), [ADR 0004](../../../../documentation/cli/adrs/tests-mirror-architecture.md)).
- `platform/` is the one home of platform `#ifdef`s; everything above asks it rather than growing its own.
- `version/` stamps the binary; its name and the `VERSION` discipline are part of the install contract and never move as a side effect ([ADR 0005](../../../../documentation/cli/adrs/install-mode-stability.md)).

**Depth:** [the ADRs](../../../../documentation/cli/adrs/README.md) · [CLAUDE.md](../../../../documentation/cli/assistant/CLAUDE.md) (invariants, code style) · [DEVELOPER.md](../../../../documentation/cli/assistant/DEVELOPER.md) (a row per module).
