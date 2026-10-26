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

/// A suite as the CLI prices it on this machine (its 27e, with 53d's shared
/// bases and adapters and M25's drafters): the `footprint` field `models
/// status --suite <name>` states -- the suite, a line per member, then the
/// total and its verdict. The app shows it as said and decides nothing from
/// its numbers: the verdict is the CLI's, the same one `chat --suite` admits
/// or refuses a session by.
public struct SuiteFootprint: Sendable, Equatable {
    /// How the total ends, in the CLI's words (`admission_line`).
    public enum Verdict: Sendable, Equatable {
        /// `-- fits`.
        case fits
        /// `-- fits as far as known`: a member's size is not stated, and the
        /// sum is a floor.
        case fitsAsFarAsKnown
        /// `-- it does not fit`: `chat --suite` refuses it unless forced.
        case doesNotFit
        /// `-- this machine's budget is not known here`.
        case budgetUnknown
        /// `no member holds memory on this machine`.
        case nothingHeld
        /// A total this app does not know how to read; its words still shown.
        case unread
    }

    /// One member's line: the backend, the roles it speaks for, and what it
    /// holds, as the CLI says it.
    public struct Member: Sendable, Equatable, Identifiable {
        public let backend: String
        public let roles: [String]
        /// After the colon: `136 MiB -- shares root's 40960 MiB of weights, …`,
        /// or `nothing held here`.
        public let said: String
        public var id: String { backend }
    }

    public let suite: String
    public let members: [Member]
    /// The total, after `total: `: the sum, the machine's budget, each
    /// member's part and the verdict -- the line the session's admission says.
    public let total: String
    public let verdict: Verdict

    /// The footprint `models status --suite <name> --output-format json`
    /// carries; `nil` when it carries none (no suite active, or a binary
    /// before the CLI's 27e).
    public init?(document: ReadDocument) {
        guard let text = Field.list(from: document).first(where: { $0.label == "footprint" })?.value else { return nil }
        self.init(text: text)
    }

    /// `suite shared` / `root (chat): …` / `total: suite shared needs …`.
    public init?(text: String) {
        let lines = text.split(whereSeparator: \.isNewline).map { $0.trimmingCharacters(in: .whitespaces) }
        guard let head = lines.first, head.hasPrefix("suite "),
            let total = lines.last(where: { $0.hasPrefix("total: ") }).map({ String($0.dropFirst("total: ".count)) })
        else { return nil }
        suite = String(head.dropFirst("suite ".count))
        members = lines.dropFirst().compactMap { line in
            guard !line.hasPrefix("total: "), let open = line.range(of: " ("), let close = line.range(of: "): ", range: open.upperBound..<line.endIndex)
            else { return nil }
            return Member(
                backend: String(line[..<open.lowerBound]),
                roles: line[open.upperBound..<close.lowerBound].components(separatedBy: ", "),
                said: String(line[close.upperBound...]))
        }
        self.total = total
        verdict = Self.verdict(of: total)
    }

    /// The verdict a total line ends in.
    static func verdict(of total: String) -> Verdict {
        if total.hasSuffix(": no member holds memory on this machine") { return .nothingHeld }
        guard let dash = total.range(of: " -- ", options: .backwards) else { return .unread }
        switch total[dash.upperBound...] {
        case "fits": return .fits
        case "fits as far as known": return .fitsAsFarAsKnown
        case "it does not fit", "over budget, run anyway (--force)": return .doesNotFit
        case "this machine's budget is not known here": return .budgetUnknown
        default: return .unread
        }
    }

    /// The verdict in a few words, the CLI's own: what the total ends in.
    public var verdictWords: String {
        if verdict == .nothingHeld { return "no member holds memory on this machine" }
        guard let dash = total.range(of: " -- ", options: .backwards) else { return total }
        return String(total[dash.upperBound...])
    }
}
