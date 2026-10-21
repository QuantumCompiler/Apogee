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
        #expect(runner.calls.last == "read graph query runner fetch --graph demo")
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
        #expect(runner.calls.last == "read graph explain ingest.runner.Runner --graph demo")
        let explain = model.explain!
        #expect(explain.node.name == "ingest.runner.Runner")
        #expect(explain.degreeIn == 4 && explain.degreeOut == 1)
        #expect(explain.relations.map(\.heading) == ["called by", "defined in", "defines"])
        #expect(explain.statedAt == ["ingest/runner.py:4"])
        await model.walk(to: "ingest.cli.main")
        #expect(!runner.calls.contains("read graph query ingest.cli.main --graph demo"), "in the picture: no new query")
        await model.walk(to: "ingest.retry.backoff_delay")
        #expect(runner.calls.contains("read graph query ingest.retry.backoff_delay --graph demo"))
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
        #expect(await eventually { !model.isBuilding })
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
}
