/**
 * The machine-mode protocol this client is built against: the Mac app's
 * `MachineProtocol` (`DataProtocol`, `MachineProtocol.swift`), after
 * `lib/documentation/reference/cli/machine-mode.md`.
 */
export const MachineProtocol = {
  /** The `protocol_version` this client reads; a stream announcing another is refused rather than misread. */
  version: 1,
  /** `--output-format` / `--input-format`: one JSON object per line. */
  streamFormat: 'stream-json',
  /** The vocabulary's date from which a session names its suite on its `session` event (the CLI's M29). */
  namesSuite: '2026-10-26',

  understands(protocolVersion: number): boolean {
    return protocolVersion === MachineProtocol.version
  },
}
