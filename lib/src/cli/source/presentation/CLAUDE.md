# The Presentation layer

**Position.** The top of the four ([ADR 0001](../../../../documentation/adrs/cli/layer-enforcement.md)): how Apogee is shown and driven -- the command line, the terminal views, the full-screen shell, machine mode, the HTTP server.

**Modules:** `markdown`, `render`, `views`, `machine`, `operations`, `httpserver`, `tui`, `cli` -- one static library each, `apogee_presentation_<module>`, linked as its row in [`cmake/modules.cmake`](../../cmake/modules.cmake) says ([ADR 0008](../../../../documentation/adrs/cli/module-map.md)).

**May include:** every layer.

**Enforced by:** the build -- a module sees its own layer's include root and those its links bring up, so an include up a layer does not compile, and the link policy refuses a link up, a cycle or an undeclared link at configure -- and [`harness.layering`](../../tests/scripts/cmake/layering.cmake), which holds every include of another module to the map and keeps the named rules. The rules here: `markdown/` includes only `ansi/`; `views/` paints and never parses argv (no `cli/`, `machine/` or CLI11); `machine/` never paints; `cli/` is the composition root; `operations/` holds what the command line and the HTTP server both run, so neither includes the other; `tui/` paints the full-screen shell (32b) and is the one module that links FTXUI -- privately, its headers naming none of FTXUI's types, the link policy refusing any other target that links it -- and includes only itself, `ansi/`, `markdown/` and the seams it renders by name (32d) -- reading the machine only through `platform/system_info` (32e) -- while `cli/` builds its views over the cores. Each subcommand's place in the shell is classified in `cli/tui_parity.cpp` ([ADR 0010](../../../../documentation/adrs/cli/the-shell-is-a-mode.md)).

**Changing this layer:**
- A change reaches every mode it applies to -- the command line, HTTP, machine -- in the same change, or the skipped mode is named ([ADR 0002](../../../../documentation/adrs/cli/mode-parity.md)).
- A new or changed verb, flag, value set or name kind ships with its completion, answered by the binary from live state ([ADR 0007](../../../../documentation/adrs/cli/tab-completion.md)).
- The machine protocol and `__complete` grow additively ([ADR 0006](../../../../documentation/adrs/cli/backwards-compatibility.md)); install and update paths never move as a side effect ([ADR 0005](../../../../documentation/adrs/cli/install-mode-stability.md)).
- A new module: its directory, its row, its sources in this layer's `CMakeLists.txt`, its tests in `tests/presentation/<module>/` ([ADR 0003](../../../../documentation/adrs/cli/granular-modules.md), [ADR 0004](../../../../documentation/adrs/cli/tests-mirror-architecture.md)).

**Depth:** [the ADRs](../../../../documentation/adrs/cli/README.md) · [CLAUDE.md](../../../../documentation/assistant/CLAUDE.md) · [DEVELOPER.md](../../../../documentation/assistant/DEVELOPER.md).
