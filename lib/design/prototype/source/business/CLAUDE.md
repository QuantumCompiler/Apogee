# The Business layer

**Position.** Third of the four ([the design's ADR 0001](../../../../documentation/adrs/design/react-design-prototype.md), applying [GUI ADR 0001](../../../../documentation/adrs/gui/layer-enforcement.md)): the services the view-models consume, behind the Mac app's own interfaces, so a later port swaps a real CLI in under the same names.

**Modules:** `reads` (CommandRunning, its five read outcomes and four command outcomes, and ReadRunner over the process stand-in), `jobs` (a job as a stream of updates, ended last, replayed from a recording on the clock), `session` (a chat from ChatStarting, replaying a recorded machine-mode stream, a turn a send, waiting at each question). All three are design 1d's; media follows with 1k, and the ADR lists all four.

**What they hold**, each under the Mac app's name: `CommandRunning`, `ReadOutcome`, `CommandOutcome`, `ReadWithStderr`, `ReadRunner` and `SystemReading` (the `system` document, 1e), and the reads a session's header and chooser take (1u): `Field`, `ModelKind`, `ModelRow` and `ModelInfo` (`models list` and `models info`), `SuiteFootprint` and `Suites` (`models status --suite` and `config get suites`), and `Symphony` (`symphonies list`), and a resumed chat's turns (1v): `SavedChat` (`chats show`); `JobStarting`, `JobRunning`, `JobUpdate`, `JobEnd`, `JobStarter` and `SupervisedJob` (its end held back by its rule's `exitAfter`, 1v); `ChatStarting`, `ChatSession`, `SessionEvent`, `SessionEnd`, `ChatLaunch`, `ApogeeClient` and `MachineSession` (a freeze at a named event, an exit replayed after the input closes, a restart, a continuation picked by what is written, a cancel ending its turn as a capture recorded it, 1v). A view-model sees the interfaces only, so a later port swaps a real CLI in under them.

**May import:** this layer, Data and Infrastructure, and the packages `package.json` lists as dependencies. No React, no `.tsx` file, no Node built-in, no module above.

**Enforced by:** [`tests/architecture/layering`](../../tests/architecture/layering/LayeringCheck.ts), which reads [`modules.json`](../../modules.json) and fails an upward or undeclared edge, React below Presentation, anything from outside the prototype, and this card when its **Modules** line disagrees with the map.

**Changing this layer:** a new module is a folder `source/business/<module>/`, its entry in `modules.json` with what it may depend on, its tests at `tests/business/<module>/`, and its name on this card's **Modules** line, in backticks. Its name and its interfaces' names are the Mac app's ([its card](../../../../src/darwin/source/business/CLAUDE.md)).

**Depth:** [the design's ADRs](../../../../documentation/adrs/design/README.md) · [CLAUDE.md](../../../../documentation/assistant/design/CLAUDE.md) · [DEVELOPER.md](../../../../documentation/assistant/design/DEVELOPER.md).
