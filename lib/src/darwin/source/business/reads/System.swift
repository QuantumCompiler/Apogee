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
/// bases and adapters and M25's drafters), as `models status --suite <name>`
/// states it: since the CLI's M31 its typed `footprint` object -- each
/// member's exact bytes, whether they are all of it, whose base it shares,
/// and the total, margin, budget and verdict -- and before it the
/// `footprint` field's text alone: the suite, a line per member, then the
/// total and its verdict. The app shows it and decides nothing from its
/// numbers: the verdict is the CLI's, the same one `chat --suite` admits or
/// refuses a session by, and the typed and the text reading of one suite say
/// the same verdict.
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

    /// One member: the backend, the roles it speaks for, and what it holds.
    public struct Member: Sendable, Equatable, Identifiable {
        public let backend: String
        public let roles: [String]
        /// What it holds, in words: read from the text, the CLI's own after
        /// the colon (`136 MiB -- shares root's 40960 MiB of weights, …`, or
        /// `nothing held here`); from the typed facts, said from them.
        public let said: String
        /// The typed facts (the CLI's M31); `nil` read from the text. What
        /// the total counts of it, exactly -- all of it where `known`, else
        /// what is known of it --, whether its memory is on this machine, the
        /// member whose loaded base it runs on (empty for its own), and why
        /// its size is unknown (empty when it is known).
        public let bytes: Int?
        public let known: Bool?
        public let local: Bool?
        public let shares: String?
        public let note: String?
        public var id: String { backend }

        init(backend: String, roles: [String], said: String) {
            self.backend = backend
            self.roles = roles
            self.said = said
            bytes = nil
            known = nil
            local = nil
            shares = nil
            note = nil
        }

        init?(typed value: JSONValue) {
            guard let backend = value["backend"]?.stringValue, let bytes = value["bytes"]?.intValue else { return nil }
            self.backend = backend
            roles = (value["roles"]?.arrayValue ?? []).compactMap(\.stringValue)
            self.bytes = bytes
            let known = value["known"]?.boolValue ?? true
            let local = value["local"]?.boolValue ?? true
            let shares = value["shares"]?.stringValue ?? ""
            let note = value["note"]?.stringValue ?? ""
            self.known = known
            self.local = local
            self.shares = shares
            self.note = note
            let size = SuiteFootprint.size(bytes)
            if !local {
                said = note.isEmpty ? "nothing held here" : note
            } else if !known {
                let why = note.isEmpty ? "its size is not stated" : note
                said = bytes > 0 ? "at least \(size) -- unknown: \(why)" : "unknown -- \(why)"
            } else {
                said = shares.isEmpty ? size : "\(size) -- on \(shares)'s weights, counted there"
            }
        }
    }

    public let suite: String
    public let members: [Member]
    /// The total in words: read from the text, its line after `total: ` --
    /// the sum, the machine's budget, each member's part and the verdict,
    /// the line the session's admission says; from the typed facts, said from
    /// them.
    public let total: String
    public let verdict: Verdict
    /// The typed totals (the CLI's M31); `nil` read from the text: what the
    /// suite needs here exactly -- the members' bytes and the margin -- the
    /// margin held back once for the set, and the machine's memory for
    /// models (`nil` too where the CLI doesn't know it).
    public let totalBytes: Int?
    public let marginBytes: Int?
    public let budgetBytes: Int?
    /// The verdict as the document words it (`does not fit`); `nil` read from
    /// the text, whose total ends in its own words.
    let verdictWord: String?

    /// Whether these are the CLI's typed facts rather than its text read.
    public var isTyped: Bool { verdictWord != nil }

    /// The footprint `models status --suite <name> --output-format json`
    /// carries: its typed object where the document has one (the CLI's M31;
    /// GUI M15), else its `footprint` field's text; `nil` when it carries
    /// neither (no suite active, or a binary before the CLI's 27e).
    public init?(document: ReadDocument) {
        if let typed = document.value["footprint"].flatMap(SuiteFootprint.init(typed:)) {
            self = typed
            return
        }
        guard let text = Field.list(from: document).first(where: { $0.label == "footprint" })?.value else { return nil }
        self.init(text: text)
    }

    /// The typed `footprint` object (the CLI's M31): `{suite, members,
    /// margin_bytes, total_bytes, budget_bytes, verdict}`; `nil` when it is
    /// not one.
    public init?(typed value: JSONValue) {
        guard let suite = value["suite"]?.stringValue, let members = value["members"]?.arrayValue,
            let total = value["total_bytes"]?.intValue, let word = value["verdict"]?.stringValue
        else { return nil }
        self.suite = suite
        self.members = members.compactMap(Member.init(typed:))
        totalBytes = total
        let margin = value["margin_bytes"]?.intValue ?? 0
        marginBytes = margin
        budgetBytes = value["budget_bytes"]?.intValue
        verdictWord = word
        verdict = Self.verdict(word: word)
        if verdict == .nothingHeld {
            self.total = "suite \(suite): \(word)"
        } else {
            let floor = self.members.contains { $0.known == false } ? "at least " : ""
            let of = budgetBytes.map { " of this machine's \(Self.size($0))" } ?? ""
            let held = margin > 0 ? ", \(Self.size(margin)) of it margin" : ""
            self.total = "suite \(suite) needs \(floor)\(Self.size(total))\(of)\(held) -- \(word)"
        }
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
        totalBytes = nil
        marginBytes = nil
        budgetBytes = nil
        verdictWord = nil
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

    /// The verdict a typed footprint words (`footprint_verdict`): one of the
    /// CLI's five.
    static func verdict(word: String) -> Verdict {
        switch word {
        case "fits": .fits
        case "fits as far as known": .fitsAsFarAsKnown
        case "does not fit": .doesNotFit
        case "budget not known here": .budgetUnknown
        case "no member holds memory": .nothingHeld
        default: .unread
        }
    }

    /// The verdict in a few words, the CLI's own: the typed word, or what
    /// the text's total ends in.
    public var verdictWords: String {
        if let verdictWord { return verdictWord }
        if verdict == .nothingHeld { return "no member holds memory on this machine" }
        guard let dash = total.range(of: " -- ", options: .backwards) else { return total }
        return String(total[dash.upperBound...])
    }

    /// Bytes as the app says memory: `40.13 GB`, `136 MB`.
    static func size(_ bytes: Int) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(bytes), countStyle: .memory)
    }
}
