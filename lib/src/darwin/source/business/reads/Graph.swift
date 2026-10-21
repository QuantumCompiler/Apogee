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
    }
}
