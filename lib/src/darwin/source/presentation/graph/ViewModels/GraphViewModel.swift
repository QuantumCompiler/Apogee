import BusinessJobs
import BusinessReads
import Foundation
import Observation

/// The Code graph place: search a graph, see the neighbourhood laid out,
/// explain a node, walk to its neighbours, and rebuild.
///
/// SwiftUI-free (ADR 0002). Documents in, views out: every picture is the
/// graph reads' documents (`graph stats | query | explain`), refreshed on
/// coming into view and after a rebuild; the layout is arithmetic here over
/// the nodes a document counted, never an unbounded walk.
@MainActor
@Observable
public final class GraphViewModel {
    /// The graphs this machine has built, as the binary names them.
    public private(set) var graphs: [String] = []
    public private(set) var graph: String?
    public private(set) var stats: GraphStats?
    /// A built collection graph's report (`graph report --output-format json`):
    /// its summary, communities, decisions and busiest entities -- no model.
    public private(set) var report: GraphReport?
    public var question = ""
    public private(set) var result: GraphQuery?
    public private(set) var layout = GraphLayout(nodes: [], edges: [])
    public private(set) var selected: String?
    public private(set) var explain: GraphExplain?
    public private(set) var failure: String?
    /// A rebuild or a first build running now.
    public private(set) var building: JobProgress?
    public var say: (@MainActor (String) -> Void)?

    private let runner: any CommandRunning
    private let starter: any JobStarting
    private var binary: URL?
    private var job: (any JobRunning)?

    public init(runner: any CommandRunning, starter: any JobStarting) {
        self.runner = runner
        self.starter = starter
    }

    public func connect(binary: URL?) async {
        self.binary = binary
        await refresh()
    }

    public func appeared() async {
        await refresh()
    }

    /// The graphs (the binary's own completion of a graph's name -- no read
    /// lists them), the chosen one's counts, and the question asked again.
    public func refresh() async {
        guard let binary else {
            failure = "Apogee isn't installed, so there is no graph to read."
            return
        }
        if case .done(let names) = await runner.command(binary: binary, ["__complete", "graph", "stats", ""]) {
            // One candidate a line; a description, where one follows, after a tab.
            graphs = names.split(whereSeparator: \.isNewline)
                .map { String($0.split(separator: "\t").first ?? "").trimmingCharacters(in: .whitespaces) }
                .filter { !$0.isEmpty && !$0.hasPrefix("-") }
        }
        if graph == nil || !graphs.contains(graph!) { graph = graphs.first }
        guard let graph else {
            stats = nil
            result = nil
            return
        }
        let outcome = await runner.read(binary: binary, ["graph", "stats", graph])
        if let document = outcome.document {
            stats = GraphStats(document: document)
            failure = nil
        } else {
            failure = outcome.failure
        }
        if let stats, stats.isProse, stats.built {
            report = await runner.read(binary: binary, ["graph", "report", "--graph", graph]).document.flatMap(GraphReport.init)
        } else {
            report = nil
        }
        if result != nil { await search() }
        if let selected { await explain(selected) }
    }

    public func choose(_ graph: String) async {
        self.graph = graph
        result = nil
        selected = nil
        explain = nil
        layout = GraphLayout(nodes: [], edges: [])
        await refresh()
    }

    // MARK: - Reading

    /// `graph query <question> --graph <graph>`: the entities named and the
    /// neighbourhood around them, laid out.
    public func search() async {
        let question = question.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let binary, let graph, !question.isEmpty else { return }
        let outcome = await runner.read(binary: binary, ["graph", "query", question, "--graph", graph])
        guard let document = outcome.document else {
            failure = outcome.failure
            return
        }
        failure = nil
        let result = GraphQuery(document: document)
        self.result = result
        layout = GraphLayout(nodes: result.nodes.map(\.name), edges: result.edges.map { ($0.from, $0.to) })
    }

    /// Explains a node: `graph explain <name> --graph <graph>`.
    public func select(_ name: String) async {
        selected = name
        await explain(name)
    }

    private func explain(_ name: String) async {
        guard let binary, let graph else { return }
        let outcome = await runner.read(binary: binary, ["graph", "explain", name, "--graph", graph])
        guard selected == name else { return }
        if let document = outcome.document, let explained = GraphExplain(document: document) {
            explain = explained
            failure = nil
        } else {
            explain = nil
            failure = outcome.failure ?? "\(name) could not be explained"
        }
    }

    /// Walks to a neighbour: it is explained, and -- when the picture does not
    /// hold it -- asked about, so its neighbourhood is drawn.
    public func walk(to name: String) async {
        if result?.nodes.contains(where: { $0.name == name }) != true {
            question = name
            await search()
        }
        await select(name)
    }

    public func clearSelection() {
        selected = nil
        explain = nil
    }

    // MARK: - Building

    /// Re-parses the graph's sources where files changed (`graph update`), or
    /// builds it again (`graph build`) when it has none -- supervised, then
    /// read again.
    public func rebuild() {
        guard let graph else { return }
        let sourced = !(stats?.sources.isEmpty ?? true)
        run(sourced ? ["graph", "update", graph] : ["graph", "build", graph])
    }

    /// A collection graph's communities, detected and summarised by the
    /// utility model (`graph communities <graph>`), supervised.
    public func findCommunities() {
        guard let graph, stats?.isProse == true else { return }
        run(["graph", "communities", graph])
    }

    /// A new graph of a folder's code: `graph build <name> --source <folder>`.
    public func build(name: String, folder: URL) {
        let name = name.trimmingCharacters(in: .whitespaces)
        guard !name.isEmpty else { return }
        graph = name
        run(["graph", "build", name, "--source", folder.path])
    }

    private func run(_ arguments: [String]) {
        guard let binary, job == nil else { return }
        building = JobProgress()
        let job = starter.startJob(binary: binary, arguments: arguments, streaming: false)
        self.job = job
        Task {
            for await update in job.updates { building?.apply(update) }
            self.job = nil
            let progress = building
            building = nil
            await refresh()
            switch progress?.status {
            case .done: say?(progress?.notices.last ?? "Built")
            case .failed(let message): failure = message
            default: break
            }
        }
    }

    public var isBuilding: Bool { building != nil }

    /// The edges to draw, between nodes the layout placed.
    public var edges: [GraphEdge] { result?.edges ?? [] }

    public var emptyText: String? {
        if let failure { return failure }
        if graph == nil { return "No code graph yet. Build one from a folder of code: no model, no key, no network." }
        if let stats, stats.isProse, !stats.built {
            return "\(stats.graph)'s graph isn't built yet. Building it has a model read every chunk for the things it names and how they relate."
        }
        if result == nil && report != nil { return nil }
        if result == nil { return "Search the graph: a function, a class, a file, or words naming some." }
        if result?.nodes.isEmpty == true { return "Nothing in \(graph ?? "the graph") matches “\(question)”." }
        return nil
    }
}
