import BusinessJobs
import BusinessReads
import Foundation
import Observation

/// The Code graph place: search a graph, see the neighbourhood laid out,
/// explain a node, walk to its neighbours, rebuild -- and, since GUI 4k,
/// export it, save its report, dedupe it and delete it.
///
/// SwiftUI-free (ADR 0002). Documents in, views out: every picture is the
/// graph reads' documents (`graph stats | query | explain`), refreshed on
/// coming into view and after a rebuild; the layout is arithmetic here over
/// the nodes a document counted, never an unbounded walk.
@MainActor
@Observable
public final class GraphViewModel {
    /// A format `graph export` writes -- never a server, never a model call.
    public enum ExportFormat: String, CaseIterable, Identifiable, Sendable {
        /// One self-contained page that opens offline.
        case html
        /// Every entity and relation, uncapped, for graph tooling.
        case graphml
        /// A flowchart for docs.
        case mermaid

        public var id: String { rawValue }

        /// The extension the CLI's own default file takes (`<graph>.html`,
        /// `.graphml`, `.mmd`).
        public var fileExtension: String {
            switch self {
            case .html: "html"
            case .graphml: "graphml"
            case .mermaid: "mmd"
            }
        }

        public var title: String {
            switch self {
            case .html: "HTML page"
            case .graphml: "GraphML"
            case .mermaid: "Mermaid diagram"
            }
        }
    }

    /// Where a dedupe stands: its dry run first, the merge only on the
    /// person's word.
    public enum Dedupe: Equatable, Sendable {
        /// `graph dedupe <graph> --dry-run` running.
        case planning
        /// What the dry run printed: the merge groups, or why there are none
        /// -- an older binary's text.
        case planned(String)
        /// The dry run's plan, as its document states it (the CLI's M31).
        case found(DedupePlan)
        /// The merge running, supervised.
        case merging
        /// What the merge printed.
        case merged(String)
        /// Why it could not run, in the CLI's words.
        case failed(String)
    }

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
    /// The dedupe shown: its dry run, then its merge.
    public private(set) var dedupe: Dedupe?
    /// The graph whose delete waits on the person's word.
    public var pendingDelete: String?
    /// A command running now -- an export, a report saved, a delete -- by
    /// what it is doing.
    public private(set) var working: String?
    /// Why the last of those didn't work, in the CLI's words: said apart
    /// from the graph's reads, which the place draws.
    public private(set) var problem: String?
    public var say: (@MainActor (String) -> Void)?
    /// The binary's vocabulary date, as the app read it at launch
    /// (`__machine-schema`, the session library's): from the CLI's M31 the
    /// dedupe's dry run is asked as its document first (GUI M15).
    public var vocabulary: @MainActor () -> String? = { nil }

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

    /// `graph query --graph <graph> -- <question>`: the entities named and
    /// the neighbourhood around them, laid out. The question after `--`, so
    /// the binary never reads it as a flag (`Argv`, GUI 4g).
    public func search() async {
        let question = question.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let binary, let graph, !question.isEmpty else { return }
        let outcome = await runner.read(binary: binary, ["graph", "query", "--graph", graph] + Argv.text(question))
        guard let document = outcome.document else {
            failure = outcome.failure
            return
        }
        failure = nil
        let result = GraphQuery(document: document)
        self.result = result
        layout = GraphLayout(nodes: result.nodes.map(\.name), edges: result.edges.map { ($0.from, $0.to) })
    }

    /// Explains a node: `graph explain --graph <graph> -- <name>` -- a name
    /// the graph gave it, which may begin with a dash (an Objective-C
    /// method's `-[Foo bar]`).
    public func select(_ name: String) async {
        selected = name
        await explain(name)
    }

    private func explain(_ name: String) async {
        guard let binary, let graph else { return }
        let outcome = await runner.read(binary: binary, ["graph", "explain", "--graph", graph] + Argv.text(name))
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

    private func run(_ arguments: [String], finished: (@MainActor (JobProgress?) -> Void)? = nil) {
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
            if let finished {
                finished(progress)
                return
            }
            switch progress?.status {
            case .done: say?(progress?.notices.last ?? "Built")
            case .failed(let message): failure = message
            default: break
            }
        }
    }

    // MARK: - Export, report, dedupe, delete (GUI 4k)

    /// `graph export <format> --graph <graph> -o <file>`: the file the person
    /// picked in a save panel -- the CLI writes it, never to a default
    /// place -- its own sentence said.
    public func export(_ format: ExportFormat, to file: URL) async {
        guard let graph else { return }
        await change(["graph", "export", format.rawValue, "--graph", graph, "-o", file.path], doing: "Exporting")
    }

    /// `graph report --graph <graph> --out <file>`: the Markdown summary the
    /// CLI assembles from the store, written where the person picked.
    public func saveReport(to file: URL) async {
        guard let graph else { return }
        await change(["graph", "report", "--graph", graph, "--out", file.path], doing: "Saving the report")
    }

    /// The name a save panel offers for `format`: the CLI's own default file
    /// name, `<graph>.<ext>`.
    public func exportName(_ format: ExportFormat) -> String {
        "\(graph ?? "graph").\(format.fileExtension)"
    }

    /// The name a save panel offers for the report.
    public var reportName: String { "\(graph ?? "graph")-report.md" }

    /// A dedupe's dry run -- `graph dedupe <graph> --dry-run`, which changes
    /// nothing -- shown before anything merges: its document's plan on a
    /// binary that has one (the CLI's M31, the admin route's body byte for
    /// byte; GUI M15), else the text it prints, and the text too where the
    /// binary refuses the flag. `--dry-run` is always asked: without it the
    /// read would merge.
    public func planDedupe() async {
        guard let binary, let graph, job == nil else { return }
        dedupe = .planning
        let asks = ProseReads.asksDocuments(vocabulary: vocabulary())
        switch await runner.documentOrText(binary: binary, ["graph", "dedupe", graph, "--dry-run"], asksDocument: asks, DedupePlan.init(document:)) {
        case .document(let plan): dedupe = .found(plan)
        case .text(let text): dedupe = .planned(text.trimmingCharacters(in: .newlines))
        case .failed(let why): dedupe = .failed(why)
        }
    }

    /// Whether the dry run named entities to merge: its plan's groups where
    /// the document states them, else a line the CLI starts `Would merge
    /// into` (`graph.cpp`'s dedupe); nothing is offered to merge where it
    /// found nothing.
    public var dedupeMerges: Bool {
        switch dedupe {
        case .found(let plan)?: plan.merges
        case .planned(let text)?: text.split(whereSeparator: \.isNewline).contains { $0.hasPrefix("Would merge into ") }
        default: false
        }
    }

    /// The merge the dry run showed, on the person's word: `graph dedupe
    /// <graph>`, supervised, then read again, its lines kept on the sheet.
    public func confirmDedupe() {
        guard let graph, dedupeMerges else { return }
        dedupe = .merging
        run(["graph", "dedupe", graph]) { [weak self] progress in
            guard let self else { return }
            switch progress?.status {
            case .done: dedupe = .merged(progress?.notices.joined(separator: "\n") ?? "")
            case .failed(let message): dedupe = .failed(message)
            default: dedupe = .failed("the merge was stopped")
            }
        }
    }

    public func dismissDedupe() {
        guard dedupe != .merging else { return }
        dedupe = nil
    }

    /// Asks before deleting the chosen graph.
    public func askToDelete() {
        pendingDelete = graph
    }

    /// `graph delete <graph>` once the person said yes -- a named graph's
    /// database removed, its config entry kept; a collection's graph cleared,
    /// its chunks kept -- read back, the CLI's own sentence said.
    public func confirmDelete() async {
        guard let name = pendingDelete else { return }
        pendingDelete = nil
        result = nil
        selected = nil
        explain = nil
        layout = GraphLayout(nodes: [], edges: [])
        await change(["graph", "delete", name], doing: "Deleting")
    }

    /// Runs a command, reads again, and says the CLI's first line -- or its
    /// failure.
    private func change(_ arguments: [String], doing: String) async {
        guard let binary, working == nil else { return }
        working = doing
        let outcome = await runner.command(binary: binary, arguments)
        working = nil
        await refresh()
        if let failure = outcome.failure {
            problem = failure
        } else if case .done(let stdout) = outcome {
            problem = nil
            say?(stdout.split(whereSeparator: \.isNewline).first.map(String.init) ?? "Done")
        }
    }

    public func dismissProblem() {
        problem = nil
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
        if let stats, !stats.isProse, !stats.built {
            return "\(stats.graph) has no graph built. Rebuild parses its sources again -- no model, no key, no network."
        }
        if result == nil && report != nil { return nil }
        if result == nil { return "Search the graph: a function, a class, a file, or words naming some." }
        if result?.nodes.isEmpty == true { return "Nothing in \(graph ?? "the graph") matches “\(question)”." }
        return nil
    }
}
