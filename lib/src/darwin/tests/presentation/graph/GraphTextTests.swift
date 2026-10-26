import BusinessJobs
import BusinessReads
import Foundation
import Testing

@testable import PresentationGraph

/// A question and a node's name are never options (GUI 4g): on the branch
/// binary `graph query "-x alpha"` answered "QUESTION is required" and
/// `graph explain "-[Foo bar]"` "NODE is required"; after `--` each is read
/// (the query's document echoes the question whole).
@Suite("Graph text after --", .timeLimit(.minutes(1)))
@MainActor
struct GraphTextTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    @Test("a dash-leading question and an Objective-C method's node are read after --, the graph named before it")
    func dashTextRead() async {
        let runner = ScriptedRunner()
        runner.saying(["__complete", "graph", "stats"], "demo\n")
        runner.reading(["graph", "stats"], fixture("stats"))
        runner.reading(["graph", "query"], fixture("query"))
        runner.reading(["graph", "explain"], fixture("explain"))
        let model = GraphViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        model.question = "-x runner fetch"
        await model.search()
        #expect(runner.calls.last == "read graph query --graph demo -- -x runner fetch")
        await model.select("-[Runner fetch:]")
        #expect(runner.calls.last == "read graph explain --graph demo -- -[Runner fetch:]")
    }
}
