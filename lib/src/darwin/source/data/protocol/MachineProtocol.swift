/// The machine-mode protocol this client is built against
/// (lib/documentation/cli/reference/machine-mode.md).
///
/// The vocabulary itself -- the events, the driver's lines -- arrives with the
/// client core; this is the floor it stands on: which protocol, and the format
/// word both directions of a driven session take.
public enum MachineProtocol {
    /// The `protocol_version` this client reads. A binary announcing another
    /// number has changed what an existing event means, and its stream is
    /// refused rather than misread (machine-mode.md -> The stability promise).
    public static let version = 1

    /// `--output-format` / `--input-format`: one JSON object per line.
    public static let streamFormat = "stream-json"

    /// The vocabulary's date from which a session names its suite -- and an
    /// `execute` session whether it orchestrates -- on its `session` event,
    /// a launch's suite lines are notices after it, and a media runtime's
    /// refusal ends its job stream on `error` (the CLI's M29, 2026-10-26).
    public static let namesSuite = "2026-10-26"

    /// Whether a stream announcing `protocolVersion` can be read by this client.
    public static func understands(protocolVersion: Int) -> Bool {
        protocolVersion == version
    }
}
