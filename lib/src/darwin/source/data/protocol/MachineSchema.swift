import Foundation

/// What a binary's machine mode speaks, as `apogee __machine-schema` prints
/// it: the vocabulary a driver can know before any session starts -- its
/// date, the protocol version, every event the build can write and every line
/// it reads (machine-mode.md; the schema the CLI's 28h ships). The command
/// answers with no config and no backend, so the app asks it once a binary is
/// found, and every capability flag is known from launch (GUI 4k); a
/// session's own `capabilities` stay a refresh of the same facts.
public struct MachineSchema: Sendable, Equatable {
    /// The vocabulary's version, a date: `x-apogee.schema`, else the last
    /// part of the schema's `$id`.
    public let schema: String
    /// `x-apogee.protocol_version`, when the schema states it.
    public let protocolVersion: Int?
    /// Every event type the build can write: the `$defs` named
    /// `event_<type>`, in name order.
    public let events: [String]
    /// Every line type a driver may write: the `$defs` named `line_<type>`,
    /// in name order -- what a session of this build reads (an execute
    /// session's own `accepts` may name fewer; its `session` event says).
    public let accepts: [String]

    public init(schema: String, protocolVersion: Int?, events: [String], accepts: [String]) {
        self.schema = schema
        self.protocolVersion = protocolVersion
        self.events = events
        self.accepts = accepts
    }

    /// The schema the command printed; `nil` for anything else -- no JSON
    /// object, no `$defs`, or no date -- so nothing is assumed of a binary
    /// that did not say. Fields the schema adds later are ignored.
    public init?(text: String) {
        guard let value = JSONValue.parse(text), let defs = value["$defs"]?.objectValue else { return nil }
        let stated = value["x-apogee"]?["schema"]?.stringValue
        let fromID = value["$id"]?.stringValue.flatMap { $0.split(separator: "/").last.map(String.init) }
        guard let schema = (stated ?? fromID).flatMap({ $0.isEmpty ? nil : $0 }) else { return nil }
        let names = defs.keys.sorted()
        self.init(
            schema: schema, protocolVersion: value["x-apogee"]?["protocol_version"]?.intValue,
            events: names.filter { $0.hasPrefix("event_") }.map { String($0.dropFirst("event_".count)) },
            accepts: names.filter { $0.hasPrefix("line_") }.map { String($0.dropFirst("line_".count)) })
    }
}
