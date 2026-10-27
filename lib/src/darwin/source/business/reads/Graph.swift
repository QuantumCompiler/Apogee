import DataProtocol
import Foundation

/// A code graph's node, as the graph documents name it.
public struct GraphNode: Sendable, Equatable, Hashable, Identifiable {
    public let name: String
    /// `class`, `function`, `file`, `name` ...
    public let type: String
    public let description: String
    public let file: String?
    public let line: Int?
    /// How far a query walked to it: 0 for what the question named.
    public let hop: Int?

    public var id: String { name }

    init(_ value: JSONValue) {
        name = value["name"]?.stringValue ?? ""
        type = value["type"]?.stringValue ?? ""
        description = value["description"]?.stringValue ?? ""
        file = value["file"]?.stringValue
        line = value["line"]?.intValue
        hop = value["hop"]?.intValue
    }

    /// Where it is written, `file:line`.
    public var place: String? { file.map { $0 + (line.map { ":\($0)" } ?? "") } }

    /// Its last part: `ingest.runner.Runner._fetch` is `_fetch`.
    public var short: String {
        if type == "file" { return (name as NSString).lastPathComponent }
        return name.split(separator: ".").last.map(String.init) ?? name
    }
}

/// An edge between two nodes.
public struct GraphEdge: Sendable, Equatable, Hashable {
    public let from: String
    public let to: String
    public let relation: String
    /// `extracted` or `inferred`.
    public let origin: String
}

/// `graph query <question> --output-format json`: the entities the question
/// names and the bounded neighbourhood around them.
public struct GraphQuery: Sendable, Equatable {
    public let question: String
    public let nodes: [GraphNode]
    public let edges: [GraphEdge]
    /// `names`, `words` ... how the question matched.
    public let match: String

    public init(document: ReadDocument) {
        let v = document.value
        question = v["question"]?.stringValue ?? ""
        match = v["match"]?.stringValue ?? ""
        nodes = (v["entities"]?.arrayValue ?? []).map(GraphNode.init).filter { !$0.name.isEmpty }
        let names = Set(nodes.map(\.name))
        edges = (v["relations"]?.arrayValue ?? []).compactMap { r in
            guard let from = r["from"]?.stringValue, let to = r["to"]?.stringValue, names.contains(from), names.contains(to)
            else { return nil }
            return GraphEdge(from: from, to: to, relation: r["relation"]?.stringValue ?? "", origin: r["origin"]?.stringValue ?? "")
        }
    }
}

/// `graph explain <node> --output-format json`: everything around one node.
public struct GraphExplain: Sendable, Equatable {
    public struct Relation: Sendable, Equatable, Identifiable {
        public let relation: String
        /// `in` or `out`.
        public let direction: String
        public let neighbors: [GraphNode]
        /// How many there are in all; `neighbors` may list fewer.
        public let total: Int
        public var id: String { relation + ":" + direction }

        /// `calls ←`, read as a person says it.
        public var heading: String {
            switch (relation, direction) {
            case ("calls", "in"): "called by"
            case ("calls", "out"): "calls"
            case ("defined_in", "in"): "defines"
            case ("defined_in", "out"): "defined in"
            case ("imports", "in"): "imported by"
            case ("imports", "out"): "imports"
            default: direction == "in" ? "\(relation) (in)" : relation
            }
        }
    }

    public let node: GraphNode
    public let description: String
    public let degreeIn: Int
    public let degreeOut: Int
    public let relations: [Relation]
    /// Where it is stated in the code, `file:line`.
    public let statedAt: [String]
    /// The communities it belongs to, with their summaries where stored.
    public let communities: [GraphCommunity]
    /// The decision records attached to it (a record about it captured into
    /// the graph's collection).
    public let decisions: [GraphDecision]

    public init?(document: ReadDocument) {
        let v = document.value
        guard let node = v["node"], node["name"]?.stringValue != nil else { return nil }
        self.node = GraphNode(node)
        description = v["description"]?.stringValue ?? ""
        degreeIn = v["degree"]?["in"]?.intValue ?? 0
        degreeOut = v["degree"]?["out"]?.intValue ?? 0
        relations = (v["relations"]?.arrayValue ?? []).map { r in
            Relation(
                relation: r["relation"]?.stringValue ?? "", direction: r["direction"]?.stringValue ?? "",
                neighbors: (r["neighbors"]?.arrayValue ?? []).map(GraphNode.init),
                total: r["total"]?.intValue ?? 0)
        }
        statedAt = (v["provenance"]?["code"]?.arrayValue ?? []).compactMap { c in
            c["file"]?.stringValue.map { $0 + (c["line"]?.intValue.map { ":\($0)" } ?? "") }
        }
        communities = (v["communities"]?.arrayValue ?? []).compactMap(GraphCommunity.init)
        decisions = (v["decisions"]?.arrayValue ?? []).compactMap { GraphDecision($0) }
    }
}

/// `graph stats <name> --output-format json`: a graph's counts.
public struct GraphStats: Sendable, Equatable {
    public let graph: String
    public let built: Bool
    public let nodes: Int
    public let edges: Int
    public let codeFiles: Int
    public let nodesByType: [String: Int]
    public let staleFiles: Int
    public let sources: [String]
    /// `collection` for a collection's own (prose) graph, else the named
    /// graph's kind.
    public let kind: String
    public let communities: Int
    public let communitiesUnsummarised: Int
    public let failedChunks: Int

    /// A collection's own graph: prose, its entities drawn by a model.
    public var isProse: Bool { kind == "collection" }

    public init(document: ReadDocument) {
        let v = document.value
        graph = v["graph"]?.stringValue ?? ""
        built = v["built"]?.boolValue ?? false
        nodes = v["nodes"]?.intValue ?? 0
        edges = v["edges"]?.intValue ?? 0
        codeFiles = v["code_files"]?.intValue ?? 0
        nodesByType = (v["nodes_by_type"]?.objectValue ?? [:]).compactMapValues(\.intValue)
        staleFiles = v["stale_files"]?.intValue ?? 0
        sources = (v["sources"]?.arrayValue ?? []).compactMap(\.stringValue)
        kind = v["kind"]?.stringValue ?? ""
        communities = v["communities"]?.intValue ?? 0
        communitiesUnsummarised = v["communities_unsummarised"]?.intValue ?? 0
        failedChunks = v["failed_chunks"]?.intValue ?? 0
    }
}

/// A community of a graph: its members' count, and the summary a model wrote
/// for it when one is stored.
public struct GraphCommunity: Sendable, Equatable, Identifiable {
    public let id: Int
    public let size: Int
    public let summary: String?
    /// Its members, where the document names them (the report does).
    public let members: [GraphNode]

    public init?(_ value: JSONValue) {
        guard let id = value["id"]?.intValue else { return nil }
        self.id = id
        size = value["size"]?.intValue ?? 0
        summary = value["summary"]?.stringValue.flatMap { $0.isEmpty ? nil : $0 }
        members = (value["members"]?.arrayValue ?? []).map(GraphNode.init)
    }
}

/// A decision record attached to a graph: its node's name (the record's id),
/// its branch and discipline, and the decision in the CLI's words.
public struct GraphDecision: Sendable, Equatable, Identifiable {
    public let name: String
    public let status: String
    public let discipline: String
    public let decision: String
    public var id: String { name }

    /// From an explain's decision (the node's own fields) or a report's (the
    /// node under `node`).
    public init?(_ value: JSONValue) {
        let node = value["node"] ?? value
        guard let name = node["name"]?.stringValue else { return nil }
        self.name = name
        status = node["status"]?.stringValue ?? ""
        discipline = node["discipline"]?.stringValue ?? ""
        decision = value["decision"]?.stringValue ?? ""
    }
}

/// `graph report --output-format json`: the graph's own summary -- its
/// communities, the decisions attached, its busiest entities -- from the
/// store, with no model call.
public struct GraphReport: Sendable, Equatable {
    /// One of the busiest entities, with its relation count.
    public struct Hub: Sendable, Equatable, Identifiable {
        public let node: GraphNode
        public let degree: Int
        public var id: String { node.name }
    }

    public let summary: String
    public let communities: [GraphCommunity]
    public let communitiesTotal: Int
    public let decisions: [GraphDecision]
    public let decisionsTotal: Int
    public let hubs: [Hub]
    public let orphans: Int

    public init?(document: ReadDocument) {
        let v = document.value
        guard v["object"]?.stringValue == "graph.report" else { return nil }
        summary = v["human_summary"]?.stringValue ?? ""
        communities = (v["communities"]?["shown"]?.arrayValue ?? []).compactMap(GraphCommunity.init)
        communitiesTotal = v["communities"]?["total"]?.intValue ?? 0
        decisions = (v["decisions"]?["shown"]?.arrayValue ?? []).compactMap { GraphDecision($0) }
        decisionsTotal = v["decisions"]?["total"]?.intValue ?? 0
        hubs = (v["hubs"]?["shown"]?.arrayValue ?? []).compactMap { hub in
            hub["node"].map { Hub(node: GraphNode($0), degree: hub["degree"]?["total"]?.intValue ?? 0) }
        }
        orphans = v["orphans"]?["total"]?.intValue ?? 0
    }
}

/// A dedupe's plan, as `graph dedupe <graph> --dry-run --output-format json`
/// states it since the CLI's M31 -- byte for byte the body its admin twin
/// serves: the groups that would merge, each kept entity and the names that
/// fold into it, at what threshold, and why the vector pass found nothing to
/// compare where it didn't run. Before it, the dry run printed text alone.
public struct DedupePlan: Sendable, Equatable {
    /// One group: the entity kept, its type, and the entities that merge into
    /// it.
    public struct Group: Sendable, Equatable, Identifiable {
        public let kept: String
        public let keptType: String
        public let merged: [String]
        public var id: String { keptType + ":" + kept }
    }

    public let groups: [Group]
    /// How many entities merge, across the groups.
    public let mergedNodes: Int
    /// The cosine similarity above which two entities merge; `nil` unsaid.
    public let threshold: Double?
    /// Whether nothing was written: always so for the dry run the app asks.
    public let dryRun: Bool
    /// A code graph's entities, each one node per qualified name as it was
    /// built -- never merged by resemblance -- and how many were stated at
    /// more than one place.
    public let codeEntities: Int
    public let codeIdentityMerges: Int
    /// Why the vector pass compared nothing, where it didn't run; `nil` when
    /// it ran.
    public let proseSkipped: String?

    /// The body's facts; `nil` for a document with no `groups`.
    public init?(document: ReadDocument) {
        let v = document.value
        guard let groups = v["groups"]?.arrayValue else { return nil }
        self.groups = groups.compactMap { group in
            guard let kept = group["kept"]?.stringValue else { return nil }
            return Group(
                kept: kept, keptType: group["kept_type"]?.stringValue ?? "",
                merged: (group["merged"]?.arrayValue ?? []).compactMap(\.stringValue))
        }
        mergedNodes = v["merged_nodes"]?.intValue ?? 0
        threshold = if case .number(let n)? = v["threshold"] { n } else { nil }
        dryRun = v["dry_run"]?.boolValue ?? true
        codeEntities = v["code_entities"]?.intValue ?? 0
        codeIdentityMerges = v["code_identity_merges"]?.intValue ?? 0
        proseSkipped = v["prose_skipped"]?.stringValue.flatMap { $0.isEmpty ? nil : $0 }
    }

    /// Whether it names anything to merge: Merge is offered only then.
    public var merges: Bool { groups.contains { !$0.merged.isEmpty } }

    /// The plan in a sentence, from its facts: `2 groups, 4 entities would
    /// merge at threshold 0.92`, or that nothing would.
    public var summary: String {
        let at = threshold.map { " at threshold \(Self.number($0))" } ?? ""
        guard merges else { return "Nothing would merge\(at)." }
        let groupWords = groups.count == 1 ? "1 group" : "\(groups.count) groups"
        let entityWords = mergedNodes == 1 ? "1 entity" : "\(mergedNodes) entities"
        return "\(groupWords), \(entityWords) would merge\(at)."
    }

    /// `0.92`, `1`.
    static func number(_ value: Double) -> String {
        value.rounded() == value ? String(Int(value)) : String(value)
    }
}
