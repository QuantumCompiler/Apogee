import DataProtocol
import Foundation
import InfrastructureProcess

/// The fields Xcode's Add an Agent sheet takes, as `acp registration
/// --output-format json` prints them (the CLI's 56d, `object:
/// "acp.registration"`): Settings › Xcode draws each as printed, with Copy,
/// and composes none -- not the binary's path, not Xcode's menu path, not the
/// approval's words (GUI 5a, the machine-mode client rule).
public struct AgentRegistration: Sendable, Equatable {
    /// One environment variable the agent is started with.
    public struct Variable: Sendable, Equatable, Identifiable {
        public let name: String
        public let value: String
        public var id: String { name }

        public init(name: String, value: String) {
            self.name = name
            self.value = value
        }

        /// The entry as the CLI's text prints it under Environment, one a
        /// line: `NAME=value`.
        public var entry: String { "\(name)=\(value)" }
    }

    /// The document's `object`.
    public static let object = "acp.registration"

    /// Where the sheet is in Xcode, as the CLI says it: `Settings ›
    /// Intelligence › Agents › Add an Agent` (the document's `where`).
    public let location: String
    /// `Apogee`, `Apogee (dev)` or `Apogee (test)`, by the build's channel.
    public let name: String
    /// The printing binary's absolute path, symlinks resolved: Xcode does no
    /// `~` or `PATH` lookup.
    public let executable: String
    /// `nil` -- the document's `null` -- for a binary, which is the sheet's
    /// Interpreter left empty.
    public let interpreter: String?
    /// One entry a line in the sheet: a root flag ahead of `acp`, and the
    /// flags `acp` itself was given after it.
    public let arguments: [String]
    /// `APOGEE_HOME`, `APOGEE_CONFIG` and `PATH`, exactly when the printing
    /// process has them, in the order the document prints them (by name).
    public let environment: [Variable]
    /// `release`, `dev` or `test`.
    public let channel: String?
    /// The Xcode that first hosts ACP agents: `26.6`.
    public let xcodeMinimum: String?
    /// What the CLI says of Xcode's approval of the binary (wall W3).
    public let approval: String?

    /// The document, when it is a registration with an Executable; `nil` for
    /// any other -- a field it never printed is never drawn.
    public init?(document: ReadDocument) {
        let value = document.value
        guard document.object == Self.object, let executable = value["executable"]?.stringValue, !executable.isEmpty else {
            return nil
        }
        self.executable = executable
        location = value["where"]?.stringValue ?? ""
        name = value["name"]?.stringValue ?? ""
        interpreter = value["interpreter"]?.stringValue
        arguments = (value["arguments"]?.arrayValue ?? []).compactMap(\.stringValue)
        environment = (value["environment"]?.objectValue ?? [:])
            .compactMap { key, value in value.stringValue.map { Variable(name: key, value: $0) } }
            .sorted { $0.name < $1.name }
        channel = value["channel"]?.stringValue
        xcodeMinimum = value["xcode_minimum"]?.stringValue
        approval = value["approval"]?.stringValue
    }

    /// The line the CLI prints above the fields: `Add Apogee (dev) to Xcode
    /// 26.6 or later: Settings › Intelligence › Agents › Add an Agent`
    /// (`render_acp_registration`), from the document's own words; `nil`
    /// where it printed no place.
    public var heading: String? {
        guard !location.isEmpty else { return nil }
        let xcode = xcodeMinimum.map { "Xcode \($0) or later" } ?? "Xcode"
        return "Add \(name) to \(xcode): \(location)"
    }
}

/// `check`'s `Xcode` section (the CLI's 56d), by the names it gives its rows:
/// `xcode`, then for an Xcode `mcpbridge`, `acp agents` and `approval` --
/// every row `ok` or `skipped`, never a warning or a failure.
public enum XcodeCheck {
    /// The section's name in `check`'s document.
    public static let section = "Xcode"
    /// The Xcode selected, or why there is none.
    public static let xcode = "xcode"
    /// Whether this Xcode serves its tools to Apogee's agents.
    public static let bridge = "mcpbridge"
    /// Whether this Xcode hosts Apogee as its agent.
    public static let agents = "acp agents"
    /// Xcode's approval of the binary (wall W3).
    public static let approval = "approval"
}

/// Whether an MCP server is in the config, as `config get mcp_servers.<name>`
/// says it -- its command, or `no such config key` when there is none: a
/// plain config read, which connects to nothing (`mcp list` connects to every
/// server, and would start Xcode's bridge).
public enum MCPServerEntry: Sendable, Equatable {
    /// In the config: the command `config get` printed.
    case registered(command: String)
    /// Not in the config: the CLI's `no such config key`.
    case notRegistered
    /// Unread, in the CLI's words.
    case unreadable(String)

    /// The key `config get` reads a server by.
    public static func key(of server: String) -> String {
        "mcp_servers.\(server)"
    }

    /// The words `config get` refuses a key the config does not hold with.
    public static let noSuchKey = "no such config key"

    /// What `config get`'s outcome says.
    public init(_ outcome: CommandOutcome) {
        switch outcome {
        case .done(let stdout):
            self = .registered(command: stdout.trimmingCharacters(in: .whitespacesAndNewlines))
        case .failed(_, let tail) where tail.text.contains(Self.noSuchKey):
            self = .notRegistered
        case let other:
            self = .unreadable(other.failure ?? "it could not be read")
        }
    }
}
