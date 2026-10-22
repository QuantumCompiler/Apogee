# The Infrastructure layer

**Position.** First of the four ([ADR 0001](../../../../documentation/gui/adrs/layer-enforcement.md)): OS mechanisms behind one interface -- spawning and holding the `apogee` child, file-system paths, the platform's own services. What everything stands on.

**Modules:** `process` -- the child process: what to spawn (ChildCommand), and a child held by its three pipes and nothing else (RunningChild, ProcessSpawner) -- stdout framed into lines, stderr kept as a bounded tail, a write to a child that stopped reading an error rather than a SIGPIPE, an interrupt as Ctrl-C's -- a one-shot run captured whole within a time limit (CapturedRun), the PATH every child runs with, the person's login shell's, asked once a launch with a deadline and a fallback (ShellPath, LoginShell), and a process's resident memory as the system counts it (ProcessMemory).

**May import:** this layer and the system's non-UI frameworks (Foundation, Darwin, os). No UI framework, no module above.

**Enforced by:** [`tests/architecture/layering`](../../tests/architecture/layering/LayeringCheck.swift), which reads [`Apogee.xcodeproj`](../../Apogee.xcodeproj/project.pbxproj) and fails an upward or undeclared edge and a UI framework below Presentation.

**Changing this layer:** a new module is a directory `source/infrastructure/<package>/`, a framework target named `Infrastructure<Package>` in `Apogee.xcodeproj` building that folder, its test bundle at `tests/infrastructure/<package>/` in the shared scheme, and its name on this card's **Modules** line.

**Depth:** [the GUI's ADRs](../../../../documentation/gui/adrs/README.md) · [CLAUDE.md](../../../../documentation/gui/assistant/CLAUDE.md) · [DEVELOPER.md](../../../../documentation/gui/assistant/DEVELOPER.md).
