# The Data layer

**Position.** Second of the four ([ADR 0001](../../../../documentation/adrs/cli/layer-enforcement.md)): how state and the outside world are reached -- the contracts every implementor reads, the wire, the stores, the backends.

**Modules:** `contracts`, `transport`, `modelstore`, `logger`, `secrets`, `embedstore`, `backends` -- one static library each, `apogee_data_<module>`, linked as its row in [`cmake/modules.cmake`](../../cmake/modules.cmake) says ([ADR 0008](../../../../documentation/adrs/cli/module-map.md)).

**May include:** this layer and Infrastructure.

**Enforced by:** the build -- a module sees its own layer's include root and those its links bring up, so an include up a layer does not compile, and the link policy refuses a link up, a cycle or an undeclared link at configure -- and [`harness.layering`](../../tests/scripts/cmake/layering.cmake), which holds every include of another module to the map and keeps the named rules. The rules here: `contracts/` is the floor and includes only `platform/`; `modelstore/` and `transport/` stand on `contracts/` and `platform/` alone; `secrets/` on `contracts/` alone.

**Changing this layer:**
- What two layers both need sinks to `contracts/`; what a lower layer needs from a higher one crosses as an interface declared here and implemented above (`ProviderRegistry`), never an include up.
- A capability a backend has becomes an interface in `contracts/provider.h`, discovered by the Harness -- never a cast at a call site ([CLAUDE.md](../../../../documentation/assistant/CLAUDE.md) → invariants).
- A model list Apogee carries for a vendor CLI that prints none (`backends/model_roster.cpp`'s `built_in_rosters()`) is kept current with its vendor by the change that finds it stale, with its reviewed date moved ([ADR 0009](../../../../documentation/adrs/cli/built-in-model-lists.md)).
- Every config write goes through `contracts/config_edit.h`, the one writer every mode shares ([ADR 0002](../../../../documentation/adrs/cli/mode-parity.md)); an older config still loads, and a store migrates loudly, never silently ([ADR 0006](../../../../documentation/adrs/cli/backwards-compatibility.md)).
- A new module: its directory, its row, its sources in this layer's `CMakeLists.txt`, its tests in `tests/data/<module>/` ([ADR 0003](../../../../documentation/adrs/cli/granular-modules.md), [ADR 0004](../../../../documentation/adrs/cli/tests-mirror-architecture.md)).

**Depth:** [the ADRs](../../../../documentation/adrs/cli/README.md) · [CLAUDE.md](../../../../documentation/assistant/CLAUDE.md) · [DEVELOPER.md](../../../../documentation/assistant/DEVELOPER.md).
