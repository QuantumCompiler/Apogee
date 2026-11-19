# The Infrastructure layer

**Position.** First of the four ([the design's ADR 0001](../../../../documentation/adrs/design/react-design-prototype.md), applying [GUI ADR 0001](../../../../documentation/adrs/gui/layer-enforcement.md)): the mechanisms everything stands on. In the prototype they are stand-ins: nothing here starts a process, reads a file at run time or reaches the network.

**Modules:** none yet. Item 1d adds clock (the 400 ms tick, stopped by still) and process (the stand-in for running apogee, answering from the bundled recordings in its three modes), as the ADR lists them.

**May import:** this layer only, and the packages `package.json` lists as dependencies. No React, no `.tsx` file, no Node built-in (the app runs in a browser), no module above.

**Enforced by:** [`tests/architecture/layering`](../../tests/architecture/layering/LayeringCheck.ts), which reads [`modules.json`](../../modules.json) and fails an upward or undeclared edge, React below Presentation, anything from outside the prototype, and this card when its **Modules** line disagrees with the map.

**Changing this layer:** a new module is a folder `source/infrastructure/<module>/`, its entry in `modules.json` with what it may depend on, its tests at `tests/infrastructure/<module>/`, and its name on this card's **Modules** line, in backticks. Its name and its interfaces' names are the Mac app's ([its card](../../../../src/darwin/source/infrastructure/CLAUDE.md)).

**Depth:** [the design's ADRs](../../../../documentation/adrs/design/README.md) · [CLAUDE.md](../../../../documentation/assistant/design/CLAUDE.md) · [DEVELOPER.md](../../../../documentation/assistant/design/DEVELOPER.md).
