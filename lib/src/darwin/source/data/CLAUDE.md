# The Data layer

**Position.** Second of the four ([ADR 0001](../../../../documentation/gui/adrs/layer-enforcement.md)): the contracts floor -- the machine-mode protocol as types, decoding that tolerates what it does not know, and the GUI's own persisted state. How the outside world, chiefly the CLI, is reached and represented.

**Modules:** `protocol` -- the machine-mode protocol as types: the events read tolerantly (ProtocolEvent, EventDecoder), the lines written (DriverLine), the reads' documents (ReadDocument), any JSON kept whole (JSONValue); `preferences` -- the GUI's own settings, never the CLI's config: the window's state, the open session, the media folders added (Preferences); `discovery` -- the apogee binary found and verified by running it (BinaryDiscovery).

**May import:** this layer, Infrastructure, and the system's non-UI frameworks. No UI framework, no module above.

**Enforced by:** [`tests/architecture/layering`](../../tests/architecture/layering/LayeringCheck.swift), which reads [`Apogee.xcodeproj`](../../Apogee.xcodeproj/project.pbxproj) and fails an upward or undeclared edge and a UI framework below Presentation.

**Changing this layer:** a new module is a directory `source/data/<package>/`, a framework target named `Data<Package>` in `Apogee.xcodeproj` building that folder, its test bundle at `tests/data/<package>/` in the shared scheme, and its name on this card's **Modules** line. The protocol grows additively ([the CLI's ADR 0006](../../../../documentation/cli/adrs/backwards-compatibility.md)): a decoder here never fails on an event or field it does not know.

**Depth:** [the GUI's ADRs](../../../../documentation/gui/adrs/README.md) · [the machine-mode protocol](../../../../documentation/cli/reference/machine-mode.md) · [DEVELOPER.md](../../../../documentation/gui/assistant/DEVELOPER.md).
