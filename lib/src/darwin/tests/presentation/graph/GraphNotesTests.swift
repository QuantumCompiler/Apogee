import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationGraph

/// A run recorded from the built CLI (`tests/business/jobs/Fixtures/<name>.out`,
/// `.err`, `.exit`), replayed on a scripted job: each stdout line said, then
/// its end with its stderr as the tail.
func replay(_ name: String, on job: ScriptedJob) {
    func read(_ ext: String) -> String {
        let file = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../business/jobs/Fixtures/\(name).\(ext)")
        return (try? String(contentsOf: file, encoding: .utf8)) ?? ""
    }
    for line in read("out").split(separator: "\n") { job.say(.said(String(line))) }
    job.end(status: Int32(read("exit").trimmingCharacters(in: .whitespacesAndNewlines)) ?? -1, stderr: read("err").trimmingCharacters(in: .newlines))
}

/// A recorded run's stdout and stderr lines.
func recorded(_ name: String, _ ext: String) -> [String] {
    let file = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../business/jobs/Fixtures/\(name).\(ext)")
    return ((try? String(contentsOf: file, encoding: .utf8)) ?? "").split(separator: "\n").map(String.init)
}

/// GUI M22: a build's, update's, communities' or merge's notes reach the
/// Code graph place on its card, word for word, apart from its toasts.
@Suite("Graph's job notes", .timeLimit(.minutes(1)))
@MainActor
struct GraphNotesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let unbuilt = #"{"built":false,"graph":"my notes","kind":"collection","object":"graph.stats"}"#

    /// The collection `my notes`, its graph not built, and `notes` beside it.
    func collection(_ jobs: [ScriptedJob]) async -> (GraphViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.saying(["__complete", "graph", "stats"], "my notes\nnotes\n")
        runner.reading(["graph", "stats"], Self.unbuilt)
        let jobs = ScriptedJobs(jobs)
        let model = GraphViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        return (model, runner, jobs)
    }

    @Test("a build the config can't register toasts its last line, and the card keeps both notes word for word")
    func collectionBuild() async {
        let job = ScriptedJob()
        let (model, _, jobs) = await collection([job])
        var said: [String] = []
        model.say = { said.append($0) }
        model.rebuild()
        #expect(jobs.starts.first?.arguments == ["graph", "build", "--", "my notes"])
        replay("graph-build-unregistrable", on: job)
        #expect(await eventually { !model.isBuilding && !said.isEmpty })
        #expect(said == [recorded("graph-build-unregistrable", "out").last!])
        let notes = recorded("graph-build-unregistrable", "err").filter { $0.hasPrefix("apogee graph: ") }
        #expect(notes.count == 2)
        #expect(model.doneNotes == GraphViewModel.DoneNotes(graph: "my notes", title: "What the rebuild said", notes: JobNotes(lines: notes)))
    }

    @Test("a new graph whose second tree is gone keeps the CLI's warning, never the parse narration")
    func newGraph() async {
        let job = ScriptedJob()
        let runner = ScriptedRunner()
        runner.saying(["__complete", "graph", "stats"], "code\n")
        runner.reading(["graph", "stats"], fixture("stats"))
        let model = GraphViewModel(runner: runner, starter: ScriptedJobs([job]))
        var said: [String] = []
        model.say = { said.append($0) }
        await model.connect(binary: Self.binary)
        model.build(name: "code", folder: URL(fileURLWithPath: "/Users/someone/src"))
        replay("graph-build-source-gone", on: job)
        #expect(await eventually { !model.isBuilding && !said.isEmpty })
        #expect(model.doneNotes?.graph == "code")
        #expect(model.doneNotes?.title == "What the build said")
        #expect(model.doneNotes?.notes.lines == ["apogee graph: warning: source tree '/Users/someone/src2' is not a directory"])
        #expect(said == [recorded("graph-build-source-gone", "out").last!])
    }

    @Test("an update that said nothing on stderr leaves no card -- its stdout warning is the CLI's to move (CLI-M58)")
    func quietUpdate() async {
        let job = ScriptedJob()
        let runner = ScriptedRunner()
        runner.saying(["__complete", "graph", "stats"], "code\n")
        runner.reading(["graph", "stats"], fixture("stats"))
        let model = GraphViewModel(runner: runner, starter: ScriptedJobs([job]))
        var said: [String] = []
        model.say = { said.append($0) }
        await model.connect(binary: Self.binary)
        model.rebuild()
        replay("graph-update-source-gone", on: job)
        #expect(await eventually { !model.isBuilding && !said.isEmpty })
        #expect(model.doneNotes == nil)
    }

    @Test("the card belongs to its graph: another chosen drops it, a node's selection never does")
    func choosing() async {
        let job = ScriptedJob()
        let (model, runner, _) = await collection([job])
        model.rebuild()
        replay("graph-build-unregistrable", on: job)
        #expect(await eventually { !model.isBuilding && model.doneNotes != nil })
        runner.reading(["graph", "explain"], fixture("explain"))
        await model.select("Heat pump")
        #expect(model.doneNotes != nil, "a node's selection keeps the card")
        model.clearSelection()
        #expect(model.doneNotes != nil)
        await model.choose("notes")
        #expect(model.doneNotes == nil)
    }

    @Test("a graph chosen while the run goes keeps the run's notes for its own graph, shown once it is chosen again; another chosen then drops them")
    func chosenMidRun() async {
        let job = ScriptedJob()
        let (model, _, _) = await collection([job])
        model.rebuild()
        await model.choose("notes")
        replay("graph-build-unregistrable", on: job)
        #expect(await eventually { !model.isBuilding && model.doneNotes != nil })
        #expect(model.graph == "notes", "the person's choice stands")
        #expect(model.doneNotes?.graph == "my notes", "the notes are the run's graph's, kept for it")
        await model.choose("my notes")
        #expect(model.doneNotes?.graph == "my notes")
        await model.choose("notes")
        #expect(model.doneNotes == nil)
    }

    @Test("a refresh that falls to another graph drops the card; one that keeps the graph keeps it")
    func rereadFalls() async {
        let job = ScriptedJob()
        let (model, runner, _) = await collection([job])
        model.rebuild()
        replay("graph-build-unregistrable", on: job)
        #expect(await eventually { !model.isBuilding && model.doneNotes != nil })
        await model.refresh()
        #expect(model.doneNotes != nil)
        runner.saying(["__complete", "graph", "stats"], "notes\n")
        await model.refresh()
        #expect(model.graph == "notes")
        #expect(model.doneNotes == nil)
    }

    @Test("the card goes when the next run starts, and with Dismiss")
    func cleared() async {
        let first = ScriptedJob()
        let second = ScriptedJob()
        let (model, _, _) = await collection([first, second])
        model.rebuild()
        replay("graph-build-unregistrable", on: first)
        #expect(await eventually { !model.isBuilding && model.doneNotes != nil })
        model.rebuild()
        #expect(model.isBuilding)
        #expect(model.doneNotes == nil, "the next run's start clears the last one's notes")
        second.end(stderr: "apogee graph: a note")
        #expect(await eventually { !model.isBuilding && model.doneNotes != nil })
        #expect(model.doneNotes?.notes.lines == ["apogee graph: a note"])
        model.dismissNotes()
        #expect(model.doneNotes == nil)
    }

    @Test("a rebuild that drops the explained node toasts its line and the dropped one, the notes on the card apart")
    func apartFromDropped() async throws {
        let runner = ScriptedRunner()
        let job = ScriptedJob()
        runner.saying(["__complete", "graph", "stats"], "demo\n")
        runner.reading(["graph", "stats"], fixture("stats"))
        runner.reading(["graph", "query"], fixture("query"))
        runner.reading(["graph", "explain"], fixture("explain"))
        let model = GraphViewModel(runner: runner, starter: ScriptedJobs([job]))
        await model.connect(binary: Self.binary)
        model.question = "runner fetch"
        await model.search()
        await model.select(GraphGoneNodeTests.gone)
        var said: [String] = []
        model.say = { said.append($0) }
        runner.reading(["graph", "explain"], GraphGoneNodeTests.refusal)
        model.rebuild()
        job.say(.said("Code graph:       8 node(s), 8 edge(s) (nodes +1 ~0 -1; edges +3 -3)"))
        job.end(stderr: "apogee graph: warning: source tree '/Users/someone/src2' is not a directory")
        #expect(await eventually { !model.isBuilding && said.count == 2 })
        #expect(said == ["Code graph:       8 node(s), 8 edge(s) (nodes +1 ~0 -1; edges +3 -3)", "\(GraphGoneNodeTests.line) (exited with status 1)"])
        #expect(model.doneNotes?.notes.lines == ["apogee graph: warning: source tree '/Users/someone/src2' is not a directory"])
    }

    @Test("a merge's notes are kept under its own title, its lines still the sheet's (a stderr line written here, no merge recorded one)")
    func merge() async {
        let runner = ScriptedRunner()
        runner.saying(["__complete", "graph", "stats"], "demo\n")
        runner.reading(["graph", "stats"], fixture("stats"))
        runner.saying(["graph", "dedupe"], printedText("graph-dedupe-dry-run-merges-handmade.txt"))
        let job = ScriptedJob()
        let model = GraphViewModel(runner: runner, starter: ScriptedJobs([job]))
        await model.connect(binary: Self.binary)
        await model.planDedupe()
        model.confirmDedupe()
        for line in printedText("graph-dedupe-merged-handmade.txt").split(separator: "\n") { job.say(.said(String(line))) }
        job.end(stderr: "apogee graph: a note about the merge")
        #expect(await eventually { if case .merged = model.dedupe { true } else { false } })
        #expect(model.doneNotes == GraphViewModel.DoneNotes(graph: "demo", title: "What the merge said", notes: JobNotes(lines: ["apogee graph: a note about the merge"])))
    }

    @Test("a failed build says its last line, and draws no card")
    func failed() async {
        let job = ScriptedJob()
        let (model, _, _) = await collection([job])
        model.rebuild()
        job.end(status: 1, stderr: "apogee graph: a note first\napogee graph: the build failed")
        #expect(await eventually { !model.isBuilding && model.failure != nil })
        #expect(model.failure == "apogee graph: the build failed")
        #expect(model.doneNotes == nil)
    }
}
