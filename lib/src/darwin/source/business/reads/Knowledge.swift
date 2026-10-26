import DataProtocol
import Foundation

/// One decision record, as `knowledge list | info --output-format json`
/// carries it: the reasoning, the decision, its branch, and where it came
/// from.
public struct KnowledgeRecord: Sendable, Equatable, Identifiable {
    public let id: String
    public let intent: String
    public let decision: String
    /// `shipped`, `rejected` or `superseded`.
    public let status: String
    public let discipline: String
    /// The artifact the decision produced, as `knowledge link` set it.
    public let link: String
    public let source: String
    public let attribution: String
    public let timestamp: String
    public let supersedes: String?
    /// Where the CLI archived the conversation the record was distilled
    /// from: whether there is one to show. Never opened here -- the
    /// conversation comes from `knowledge info --raw` (`RawConversation`).
    public let rawRef: String?

    public init?(_ value: JSONValue) {
        guard let id = value["id"]?.stringValue, !id.isEmpty else { return nil }
        self.id = id
        intent = value["intent"]?.stringValue ?? ""
        decision = value["decision"]?.stringValue ?? ""
        status = value["status"]?.stringValue ?? ""
        discipline = value["discipline"]?.stringValue ?? ""
        link = value["downstream_link"]?.stringValue ?? ""
        source = value["provenance"]?["source"]?.stringValue ?? ""
        attribution = value["provenance"]?["attribution"]?.stringValue ?? ""
        timestamp = value["timestamp"]?.stringValue ?? ""
        supersedes = value["supersedes"]?.stringValue.flatMap { $0.isEmpty ? nil : $0 }
        rawRef = value["raw_ref"]?.stringValue.flatMap { $0.isEmpty ? nil : $0 }
    }

    /// Every record of a `knowledge list` document, in its order (newest
    /// first).
    public static func list(from document: ReadDocument) -> [KnowledgeRecord] {
        (document.entries ?? []).compactMap(KnowledgeRecord.init)
    }

    /// The branches a record can be moved to, as `knowledge status` takes them.
    public static let statuses = ["shipped", "rejected", "superseded"]
    /// The disciplines a capture or a query may name.
    public static let disciplines = ["program", "product", "project", "ux", "eng"]
}

/// `knowledge query <text> --json`: the records that best answer a question,
/// each with its score on the retriever's own scale, and which retriever ran.
public struct KnowledgeQuery: Sendable, Equatable {
    public struct Match: Sendable, Equatable, Identifiable {
        public let record: KnowledgeRecord
        public let score: Double?
        public var id: String { record.id }
    }

    public let matches: [Match]
    /// `lexical`, `vector` or `hybrid`, as the CLI says it ran.
    public let retriever: String
    public let reranked: Bool

    public init?(document: ReadDocument) {
        let v = document.value
        guard let records = v["records"]?.arrayValue else { return nil }
        matches = records.compactMap { entry in
            guard let record = entry["record"].flatMap(KnowledgeRecord.init) else { return nil }
            let score: Double? = if case .number(let n)? = entry["score"] { n } else { nil }
            return Match(record: record, score: score)
        }
        retriever = v["retriever"]?.stringValue ?? ""
        reranked = v["reranked"]?.boolValue ?? false
    }
}

/// One collection, as `embed list --output-format json` lists it.
public struct CollectionRow: Sendable, Equatable, Identifiable {
    public let name: String
    public let chunks: Int
    public let sources: Int
    public var id: String { name }

    public static func list(from document: ReadDocument) -> [CollectionRow] {
        (document.entries ?? []).compactMap { entry in
            guard let name = entry["name"]?.stringValue, !name.isEmpty else { return nil }
            return CollectionRow(name: name, chunks: entry["chunks"]?.intValue ?? 0, sources: entry["sources"]?.intValue ?? 0)
        }
    }
}

/// `embed info <collection> --output-format json`: a collection's sources and
/// health, in the CLI's words.
public struct CollectionInfo: Sendable, Equatable {
    public let name: String
    public let chunks: Int
    /// The text index's health, as `embed info` says it (`ok` ...).
    public let index: String
    public let sources: [String]
    /// How many chunks carry vectors; `nil` when the collection is text only.
    public let vectors: Int?
    public let path: String

    public init?(document: ReadDocument) {
        let v = document.value
        guard let name = v["collection"]?.stringValue else { return nil }
        self.name = name
        chunks = v["chunks"]?.intValue ?? 0
        index = v["index"]?.stringValue ?? ""
        sources = (v["sources"]?.arrayValue ?? []).compactMap(\.stringValue)
        vectors = v["vectors"]?.intValue
        path = v["path"]?.stringValue ?? ""
    }
}

/// A record's archived raw conversation, as `knowledge info <id> --raw`
/// prints it -- in text mode only (`knowledge.cpp`): the record's fields,
/// then a line `-- Raw conversation --` and the conversation, or the line
/// `Raw conversation: (unavailable)` when the archive is gone. The app runs
/// the verb as a command and shows what follows the CLI's own marker, never
/// reading the archive's path itself (GUI 4k; ADR 0002).
public enum RawConversation: Sendable, Equatable {
    /// The conversation, as the CLI printed it.
    case text(String)
    /// The CLI could not read the archive.
    case unavailable

    static let marker = "-- Raw conversation --"
    static let unavailableLine = "Raw conversation: (unavailable)"

    /// What `knowledge info --raw` printed; `nil` when neither of the CLI's
    /// lines is there to say.
    public init?(printed: String) {
        let lines = printed.components(separatedBy: "\n")
        if let start = lines.firstIndex(of: Self.marker) {
            let body = lines[(start + 1)...].joined(separator: "\n")
            self = .text(body.trimmingCharacters(in: .newlines))
        } else if lines.contains(Self.unavailableLine) {
            self = .unavailable
        } else {
            return nil
        }
    }
}
