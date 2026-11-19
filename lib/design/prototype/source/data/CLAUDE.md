# The Data layer

**Position.** Second of the four ([the design's ADR 0001](../../../../documentation/adrs/design/react-design-prototype.md), applying [GUI ADR 0001](../../../../documentation/adrs/gui/layer-enforcement.md)): the CLI's documents and events as types, decoded as tolerantly as the Mac app decodes them.

**Modules:** none yet. Item 1d adds protocol (the CLI's documents and stream events as types, an unknown event kept rather than thrown), as the ADR lists it. There is no discovery or preferences module: the process stand-in's modes take discovery's place, and the person's choices live in view-model state.

**May import:** this layer and Infrastructure, and the packages `package.json` lists as dependencies. No React, no `.tsx` file, no Node built-in, no module above.

**Enforced by:** [`tests/architecture/layering`](../../tests/architecture/layering/LayeringCheck.ts), which reads [`modules.json`](../../modules.json) and fails an upward or undeclared edge, React below Presentation, anything from outside the prototype, and this card when its **Modules** line disagrees with the map.

**Changing this layer:** a new module is a folder `source/data/<module>/`, its entry in `modules.json` with what it may depend on, its tests at `tests/data/<module>/`, and its name on this card's **Modules** line, in backticks. Its name and its types' names are the Mac app's ([its card](../../../../src/darwin/source/data/CLAUDE.md)).

**Depth:** [the design's ADRs](../../../../documentation/adrs/design/README.md) · [CLAUDE.md](../../../../documentation/assistant/design/CLAUDE.md) · [DEVELOPER.md](../../../../documentation/assistant/design/DEVELOPER.md).
