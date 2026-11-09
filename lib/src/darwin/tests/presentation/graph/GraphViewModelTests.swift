import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationGraph

/// The real binary's documents for a four-file Python tree (`graph build demo
/// --source src`, then `graph stats | query | explain`).
func fixture(_ name: String) -> String {
    let file = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name).json")
    return (try? String(contentsOf: file, encoding: .utf8)) ?? ""
}

@Suite("GraphViewModel", .timeLimit(.minutes(1)))
@MainActor
struct GraphViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func scripted() -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.saying(["__complete", "graph", "stats"], "demo\nlibs\tA collection's graph\n")
        runner.reading(["graph", "stats"], fixture("stats"))
        runner.reading(["graph", "query"], fixture("query"))
        runner.reading(["graph", "explain"], fixture("explain"))
        return runner
    }

    @Test("the graphs are the binary's own names for them; the first is read")
    func graphs() async {
        let runner = scripted()
        let model = GraphViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        #expect(runner.calls.first == "run __complete graph stats ")
        #expect(model.graphs == ["demo", "libs"])
        #expect(model.graph == "demo")
        #expect(model.stats?.nodes == 16 && model.stats?.edges == 21 && model.stats?.codeFiles == 4)
        #expect(model.stats?.nodesByType["function"] == 6)
        #expect(model.emptyText == "Search the graph: a function, a class, a file, or words naming some.")
    }

    @Test("a search is the query's document: its entities, the edges between them, laid out")
    func search() async {
        let runner = scripted()
        let model = GraphViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        model.question = "runner fetch"
        await model.search()
        #expect(runner.calls.last == "read graph query --graph demo -- runner fetch")
        let result = model.result!
        #expect(result.nodes.count == 8)
        #expect(result.nodes.filter { $0.hop == 0 }.map(\.short) == ["HttpClient", "Runner", "_fetch", "fetch.py"])
        #expect(result.edges.allSatisfy { e in result.nodes.contains { $0.name == e.from } && result.nodes.contains { $0.name == e.to } })
        #expect(Set(model.layout.positions.keys) == Set(result.nodes.map(\.name)))
        #expect(model.emptyText == nil)
    }

    @Test("a node is explained; a neighbour outside the picture is asked about, then explained")
    func explainAndWalk() async {
        let runner = scripted()
        let model = GraphViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        model.question = "runner fetch"
        await model.search()
        await model.select("ingest.runner.Runner")
        #expect(runner.calls.last == "read graph explain --graph demo -- ingest.runner.Runner")
        let explain = model.explain!
        #expect(explain.node.name == "ingest.runner.Runner")
        #expect(explain.degreeIn == 4 && explain.degreeOut == 1)
        #expect(explain.relations.map(\.heading) == ["called by", "defined in", "defines"])
        #expect(explain.statedAt == ["ingest/runner.py:4"])
        await model.walk(to: "ingest.cli.main")
        #expect(!runner.calls.contains("read graph query --graph demo -- ingest.cli.main"), "in the picture: no new query")
        await model.walk(to: "ingest.retry.backoff_delay")
        #expect(runner.calls.contains("read graph query --graph demo -- ingest.retry.backoff_delay"))
        #expect(model.selected == "ingest.retry.backoff_delay")
    }

    @Test("the layout is deterministic, inside its square, and keeps nodes apart")
    func layout() {
        let nodes = ["a", "b", "c", "d", "e"]
        let edges = [("a", "b"), ("b", "c"), ("c", "a"), ("d", "e")]
        let one = GraphLayout(nodes: nodes, edges: edges)
        let two = GraphLayout(nodes: nodes.reversed(), edges: edges)
        #expect(one == two, "the same graph, the same picture, whatever order its nodes came in")
        #expect(one != GraphLayout(nodes: nodes, edges: edges, seed: 7))
        for point in one.positions.values {
            #expect(point.x >= GraphLayout.inset - 1e-9 && point.x <= 1 - GraphLayout.inset + 1e-9)
            #expect(point.y >= GraphLayout.inset - 1e-9 && point.y <= 1 - GraphLayout.inset + 1e-9)
        }
        let points = Array(one.positions.values)
        for i in points.indices {
            for j in points.indices where i < j {
                let d = ((points[i].x - points[j].x) * (points[i].x - points[j].x) + (points[i].y - points[j].y) * (points[i].y - points[j].y)).squareRoot()
                #expect(d > 0.05, "no two nodes on top of each other")
            }
        }
        #expect(GraphLayout(nodes: ["only"], edges: []).positions["only"] == GraphLayout.Point(x: 0.5, y: 0.5))
        #expect(GraphLayout(nodes: [], edges: []).positions.isEmpty)
    }

    @Test("rebuild is graph update for a sourced graph, supervised, then read again; a new graph is graph build --source")
    func rebuild() async {
        let runner = scripted()
        let first = ScriptedJob()
        let second = ScriptedJob()
        let jobs = ScriptedJobs([first, second])
        var said: [String] = []
        let model = GraphViewModel(runner: runner, starter: jobs)
        model.say = { said.append($0) }
        await model.connect(binary: Self.binary)
        model.rebuild()
        #expect(jobs.starts.first?.arguments == ["graph", "update", "demo"])
        #expect(model.isBuilding)
        first.say(.said("Code graph:       16 node(s), 21 edge(s)"))
        first.end()
        // The toast comes after the read-back that follows the build's end,
        // so wait for it, not only for the build to stop.
        #expect(await eventually { !model.isBuilding && !said.isEmpty })
        #expect(said == ["Code graph:       16 node(s), 21 edge(s)"])
        model.build(name: "app", folder: URL(fileURLWithPath: "/Users/someone/app"))
        #expect(jobs.starts.last?.arguments == ["graph", "build", "app", "--source", "/Users/someone/app"])
        #expect(model.graph == "app")
        second.end(status: 1)
    }

    @Test("no graph says how to make one")
    func noGraph() async {
        let runner = ScriptedRunner()
        runner.saying(["__complete", "graph", "stats"], "")
        let model = GraphViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        #expect(model.graph == nil)
        #expect(model.emptyText?.hasPrefix("No code graph yet.") == true)
    }

    /// A collection's prose graph, recorded (tests/business/reads/Fixtures).
    static func prose(_ name: String) -> String {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../../business/reads/Fixtures/graph-prose-\(name).json")
        return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
    }

    @Test("a built collection graph reads its report; Find communities runs graph communities; explain carries both")
    func proseGraph() async {
        let runner = ScriptedRunner()
        runner.saying(["__complete", "graph", "stats"], "notes\n")
        runner.reading(["graph", "stats"], Self.prose("stats"))
        runner.reading(["graph", "report"], Self.prose("report"))
        runner.reading(["graph", "explain"], Self.prose("explain"))
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let model = GraphViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        #expect(model.stats?.isProse == true)
        #expect(model.report?.communities.count == 1 && model.report?.decisions.count == 1)
        #expect(model.emptyText == nil, "the report stands in for the empty search")
        await model.select("Heat pump")
        #expect(model.explain?.communities.first?.summary?.hasPrefix("The heat pump") == true)
        #expect(model.explain?.decisions.first?.status == "shipped")
        model.findCommunities()
        #expect(jobs.starts.first?.arguments == ["graph", "communities", "notes"])
        job.end()
        #expect(await eventually { !model.isBuilding })
    }

    @Test("an unbuilt collection graph says what building it costs, and has no report")
    func proseUnbuilt() async {
        let runner = ScriptedRunner()
        runner.saying(["__complete", "graph", "stats"], "notes\n")
        runner.reading(["graph", "stats"], #"{"built":false,"graph":"notes","kind":"collection","object":"graph.stats"}"#)
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let model = GraphViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        #expect(model.report == nil)
        #expect(model.emptyText?.hasPrefix("notes's graph isn't built yet") == true)
        model.rebuild()
        #expect(jobs.starts.first?.arguments == ["graph", "build", "notes"])
        job.end()
    }
}

/// GUI 6h, finding 1: an explain's failure is the explain's, never the
/// canvas's. The refusal is the branch's `apogee-dev` over a sandbox home --
/// a three-file Python tree's graph `demo`, `helper` renamed, `graph update
/// demo`, then `graph explain --graph demo -- ingest.runner.helper`: exit 1,
/// its stderr `graph-explain-gone.err`.
@Suite("Graph: a node gone after Rebuild never takes the canvas (GUI 6h)", .timeLimit(.minutes(1)))
@MainActor
struct GraphGoneNodeTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let gone = "ingest.runner.helper"

    /// The CLI's refusal, recorded: its line, and the read's outcome.
    static var line: String { printedText("graph-explain-gone.err").trimmingCharacters(in: .newlines) }
    static var refusal: ReadOutcome {
        var tail = BoundedTail()
        tail.append(Data(printedText("graph-explain-gone.err").utf8))
        return .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail)
    }

    /// The code graph read, searched, and `helper` explained while the
    /// graph still held it.
    func explained(_ runner: ScriptedRunner, jobs: ScriptedJobs = ScriptedJobs([])) async -> GraphViewModel {
        runner.saying(["__complete", "graph", "stats"], "demo\n")
        runner.reading(["graph", "stats"], fixture("stats"))
        runner.reading(["graph", "query"], fixture("query"))
        runner.reading(["graph", "explain"], fixture("explain"))
        let model = GraphViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        model.question = "runner fetch"
        await model.search()
        await model.select(Self.gone)
        return model
    }

    @Test("a Rebuild that removes the explained node keeps the re-run search, drops the card, says the CLI's line once")
    func rebuildDropsTheGoneNode() async throws {
        #expect(Self.line.hasPrefix("apogee graph: no node named 'ingest.runner.helper' in graph 'demo' -- near matches: function:ingest.runner.helper_renamed"))
        let runner = ScriptedRunner()
        let job = ScriptedJob()
        let model = await explained(runner, jobs: ScriptedJobs([job]))
        var said: [String] = []
        model.say = { said.append($0) }
        #expect(model.explain != nil && model.selected == Self.gone)
        runner.reading(["graph", "explain"], Self.refusal)
        model.rebuild()
        job.say(.said("Code graph:       8 node(s), 8 edge(s) (nodes +1 ~0 -1; edges +3 -3)"))
        job.end()
        #expect(await eventually { !model.isBuilding && said.count == 2 })
        #expect(runner.calls.contains("read graph query --graph demo -- runner fetch"))
        #expect(model.emptyText == nil, "the canvas is the search, never the explain's error")
        #expect(model.failure == nil && model.problem == nil)
        #expect(model.result?.nodes.count == 8, "the re-run search")
        #expect(model.selected == nil && model.explain == nil, "the card is gone with its node")
        #expect(said == ["Code graph:       8 node(s), 8 edge(s) (nodes +1 ~0 -1; edges +3 -3)", "\(Self.line) (exited with status 1)"],
                "the rebuild's line, then why the card went -- last, so it is the toast that stays")
        let explains = runner.calls.filter { $0.hasPrefix("read graph explain") }.count
        await model.refresh()
        await model.appeared()
        #expect(runner.calls.filter { $0.hasPrefix("read graph explain") }.count == explains, "no refresh explains it again")
        #expect(said.count == 2, "said once")
        #expect(model.emptyText == nil)
    }

    @Test("coming back to the place after the node went says it once too, and the canvas stays the search")
    func appearanceDropsTheGoneNode() async {
        let runner = ScriptedRunner()
        let model = await explained(runner)
        var said: [String] = []
        model.say = { said.append($0) }
        runner.reading(["graph", "explain"], Self.refusal)
        await model.appeared()
        #expect(said == ["\(Self.line) (exited with status 1)"])
        #expect(model.selected == nil && model.explain == nil && model.emptyText == nil && model.result != nil)
        await model.appeared()
        #expect(said.count == 1)
    }

    @Test("a click's failed explain is the problem alert, in the CLI's words; the canvas kept; Clear closes both")
    func clickedExplainRefused() async {
        let runner = ScriptedRunner()
        let model = await explained(runner)
        runner.reading(["graph", "explain"], Self.refusal)
        await model.select(Self.gone)
        #expect(model.problem == "\(Self.line) (exited with status 1)")
        #expect(model.failure == nil && model.emptyText == nil, "never the canvas's failure")
        #expect(model.result?.nodes.count == 8 && model.explain == nil)
        model.clearSelection()
        #expect(model.problem == nil && model.selected == nil && model.explain == nil)
    }

    @Test("the refusal alert's OK drops the refused node's selection; the canvas kept; no refresh asks it again")
    func refusalsOKClearsTheSelection() async {
        let runner = ScriptedRunner()
        let model = await explained(runner)
        var said: [String] = []
        model.say = { said.append($0) }
        runner.reading(["graph", "explain"], Self.refusal)
        await model.select(Self.gone)
        #expect(model.selected == Self.gone, "highlighted while its alert says why")
        model.dismissProblem()
        #expect(model.problem == nil)
        #expect(model.selected == nil && model.explain == nil, "OK is the Clear: no highlight left without a card to close")
        #expect(model.result?.nodes.count == 8 && model.emptyText == nil, "the search stays")
        let explains = runner.calls.filter { $0.hasPrefix("read graph explain") }.count
        await model.appeared()
        await model.refresh()
        #expect(runner.calls.filter { $0.hasPrefix("read graph explain") }.count == explains, "no refresh asks the refused node again")
        #expect(said.isEmpty, "the alert said it; no toast says it again")
    }

    @Test("a Rebuild ending while the refusal's alert is up does not ask the refused node again")
    func rebuildUnderTheRefusalAsksNothing() async {
        let runner = ScriptedRunner()
        let job = ScriptedJob()
        let model = await explained(runner, jobs: ScriptedJobs([job]))
        var said: [String] = []
        model.say = { said.append($0) }
        runner.reading(["graph", "explain"], Self.refusal)
        await model.select(Self.gone)
        let explains = runner.calls.filter { $0.hasPrefix("read graph explain") }.count
        model.rebuild()
        job.say(.said("Code graph:       8 node(s), 8 edge(s) (nodes +1 ~0 -1; edges +3 -3)"))
        job.end()
        #expect(await eventually { !model.isBuilding && !said.isEmpty })
        #expect(runner.calls.filter { $0.hasPrefix("read graph explain") }.count == explains, "the alert already says why")
        #expect(said == ["Code graph:       8 node(s), 8 edge(s) (nodes +1 ~0 -1; edges +3 -3)"], "the refusal is not toasted under its own alert")
        #expect(model.problem == "\(Self.line) (exited with status 1)" && model.selected == Self.gone)
        model.dismissProblem()
        #expect(model.selected == nil && model.problem == nil)
    }

    @Test("another act's refusal taking the alert drops the refused node's selection with the refusal it replaced")
    func anExportRefusalReplacesTheExplainRefusal() async {
        let runner = ScriptedRunner()
        let model = await explained(runner)
        runner.reading(["graph", "explain"], Self.refusal)
        await model.select(Self.gone)
        runner.refusing(["graph", "export"], refusedWith("graph-export-deleted.err"))
        await model.export(.html, to: URL(fileURLWithPath: "/Users/someone/demo.html"))
        #expect(model.problem?.hasPrefix("apogee graph: graph 'spare' has not been built yet") == true)
        #expect(model.selected == nil && model.explain == nil, "no OK is left that would clear it")
        model.dismissProblem()
        #expect(model.problem == nil && model.result?.nodes.count == 8)
    }

    @Test("clearing the selection leaves another act's refusal standing; its OK leaves the card")
    func clearKeepsAnExportRefusal() async {
        let runner = ScriptedRunner()
        let model = await explained(runner)
        runner.refusing(["graph", "export"], refusedWith("graph-export-deleted.err"))
        await model.export(.html, to: URL(fileURLWithPath: "/Users/someone/demo.html"))
        #expect(model.problem?.hasPrefix("apogee graph: graph 'spare' has not been built yet") == true)
        model.dismissProblem()
        #expect(model.selected == Self.gone && model.explain != nil, "OK on the export's refusal is not a Clear")
        await model.export(.html, to: URL(fileURLWithPath: "/Users/someone/demo.html"))
        model.clearSelection()
        #expect(model.problem != nil, "the export's refusal is not the explain's to clear")
    }
}
