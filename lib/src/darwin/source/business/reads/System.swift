import DataProtocol
import Foundation

/// `system --output-format json`: the machine as the CLI reads it.
public struct SystemReading: Sendable, Equatable {
    public let cpuModel: String
    /// The CPU's utilization over the read's window, 0-100; `nil` when unknown.
    public let cpuPercent: Double?
    public let memoryTotal: Int?
    public let memoryUsed: Int?
    public let gpuName: String?
    /// What models may offload to; `nil` when the CLI cannot say.
    public let modelBudget: Int?
    public let diskAvailable: Int?
    public let diskCapacity: Int?
    public let storeBytes: Int?
    /// What apogee's own reading process holds, and the models it holds.
    public let apogeeBytes: Int?

    public init(document: ReadDocument) {
        let v = document.value
        func number(_ value: JSONValue?) -> Double? { if case .number(let n)? = value { n } else { nil } }
        cpuModel = v["cpu"]?["model"]?.stringValue ?? ""
        cpuPercent = number(v["cpu"]?["utilization"]?["percent"])
        memoryTotal = v["memory"]?["total_bytes"]?.intValue
        memoryUsed = v["memory"]?["used_bytes"]?.intValue
        gpuName = v["gpu"]?["name"]?.stringValue
        modelBudget = v["gpu"]?["model_budget_bytes"]?.intValue
        diskAvailable = v["disk"]?["volume_available_bytes"]?.intValue
        diskCapacity = v["disk"]?["volume_capacity_bytes"]?.intValue
        storeBytes = v["disk"]?["store_bytes"]?.intValue
        apogeeBytes = v["apogee"]?["process_bytes"]?.intValue
    }
}

/// A suite, as `config get suites.<name>` prints it: one member per role,
/// with the window a member is pinned to.
public struct Suite: Sendable, Equatable, Identifiable {
    public struct Member: Sendable, Equatable {
        public let role: String
        public let backend: String
        /// The window the suite pins, when it pins one.
        public let window: Int?

        public init(role: String, backend: String, window: Int?) {
            self.role = role
            self.backend = backend
            self.window = window
        }
    }

    public let name: String
    public let members: [Member]
    /// The lines that did not read as `role: backend`, kept as said.
    public let unread: [String]

    public var id: String { name }

    /// `config get suites` prints the names, one per line.
    public static func names(_ text: String) -> [String] {
        text.split(whereSeparator: \.isNewline).map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
    }

    /// `chat: actor` / `utility: actor (window 4096)`.
    public init(name: String, text: String) {
        self.name = name
        var members: [Member] = []
        var unread: [String] = []
        for raw in text.split(whereSeparator: \.isNewline) {
            let line = raw.trimmingCharacters(in: .whitespaces)
            guard !line.isEmpty else { continue }
            guard let colon = line.firstIndex(of: ":") else {
                unread.append(line)
                continue
            }
            let role = String(line[..<colon])
            let rest = line[line.index(after: colon)...].trimmingCharacters(in: .whitespaces)
            guard Self.roles.contains(role), let backend = rest.split(separator: " ").first.map(String.init) else {
                unread.append(line)
                continue
            }
            var window: Int?
            if let open = rest.range(of: "(window ") {
                window = Int(rest[open.upperBound...].prefix { $0.isNumber })
            }
            members.append(Member(role: role, backend: backend, window: window))
        }
        self.members = members
        self.unread = unread
    }

    /// The roles a suite speaks for.
    public static let roles: Set<String> = ["chat", "embedding", "extraction", "vision", "transcription", "utility"]
}
