# The Business layer

**Position.** Third of the four ([ADR 0001](../../../../documentation/gui/adrs/layer-enforcement.md)): the client core -- the machine-mode session (spawned through Infrastructure, spoken in Data's types), the reads, long-running jobs. What the GUI does; every capability itself is the CLI's.

**Modules:** `session` -- a driven chat session: its child started (ChatLaunch), its events and lines (MachineSession), where it stands (TurnTracker), and the front door (ApogeeClient); `reads` -- one read run and its document decoded (ReadRunner).

**May import:** this layer, Data, Infrastructure, and the system's non-UI frameworks. No UI framework, nothing of Presentation.

**Enforced by:** [`tests/architecture/layering`](../../tests/architecture/layering/LayeringCheck.swift), which reads [`Apogee.xcodeproj`](../../Apogee.xcodeproj/project.pbxproj) and fails an upward or undeclared edge and a UI framework below Presentation.

**Changing this layer:** a new module is a directory `source/business/<package>/`, a framework target named `Business<Package>` in `Apogee.xcodeproj` building that folder, its test bundle at `tests/business/<package>/` in the shared scheme, and its name on this card's **Modules** line. A capability the CLI lacks is built in the CLI first, never here.

**Depth:** [the GUI's ADRs](../../../../documentation/gui/adrs/README.md) · [CLAUDE.md](../../../../documentation/gui/assistant/CLAUDE.md) (-> "The GUI is a machine-mode client") · [DEVELOPER.md](../../../../documentation/gui/assistant/DEVELOPER.md).
