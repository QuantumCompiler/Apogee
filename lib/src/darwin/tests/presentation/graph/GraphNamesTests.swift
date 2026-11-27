import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationGraph

/// The binary's verdict on `line` (`tests/business/reads/Fixtures/argv-end-
/// of-options-verdicts.txt`, recorded from the built CLI over a sandbox):
/// `help` or `exit <n>: <first line>`; nil when never recorded.
func recordedVerdict(_ line: [String]) -> String? {
    let wanted = line.joined(separator: " ")
    for row in printedText("argv-end-of-options-verdicts.txt").split(separator: "\n") {
        let parts: [String] = row.split(separator: "\t", maxSplits: 1).map(String.init)
        if parts.count == 2, parts[1] == wanted { return parts[0] }
    }
    return nil
}

/// Whether a verdict is CLI11's, before the verb ran: exit 106, 109 or help.
func refusedByTheParse(_ verdict: String?) -> Bool {
    guard let verdict else { return true }
    return verdict == "help" || verdict.hasPrefix("exit 106:") || verdict.hasPrefix("exit 109:")
}

/// A listed graph reaches the verb as listed (GUI M21): the list is the
/// binary's completion asked after `--` (`graph-stats-complete-dashes.txt`,
/// recorded from the built CLI over a sandbox holding stores the CLI wrote:
/// ` sp`, `-`, `--graph`, `--help`, `-1`, `-h`, `-x`), every name kept byte
/// for byte, and every act puts the graph after `--`. A name before `--` is
/// scripted to answer CLI11's refusal.
@Suite("Graph names", .timeLimit(.minutes(1)))
@MainActor
struct GraphNamesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let required: ReadOutcome = {
        var tail = BoundedTail()
        tail.append(Data("NAME is required\nRun with --help for more information.\n".utf8))
        return .failed(exit: ChildExit(reason: .exited, status: 106), stderrTail: tail)
    }()

    func scripted() -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.saying(["__complete", "graph", "stats", "--"], printedText("graph-stats-complete-dashes.txt"))
        runner.saying(["__complete", "graph", "stats", ""], "notes\n")
        runner.reading(["graph", "stats", "-x"], Self.required)
        runner.reading(["graph", "stats", "--", "-x"], printedText("graph-stats-dash-x.json"))
        runner.reading(["graph", "stats", "--", " sp"], printedText("graph-stats-dash-x.json"))
        return runner
    }

    @Test("the graphs are the completion asked after --, every name byte for byte: none dropped for a dash, none trimmed")
    func listed() async {
        let runner = scripted()
        let model = GraphViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        #expect(runner.calls.first == "run __complete graph stats -- ")
        #expect(model.graphs == [" sp", "-", "--graph", "--help", "-1", "-h", "-x", "my notes", "notes"])
        #expect(runner.calls.contains("read graph stats --  sp"), "the first, as listed")
    }

    @Test("a candidate's description, after its tab, is dropped; the candidate is kept as written")
    func descriptionDropped() async {
        let runner = ScriptedRunner()
        runner.saying(["__complete", "graph", "stats", "--"], "-x\tA collection's graph\n sp\n")
        runner.reading(["graph", "stats", "--", "-x"], printedText("graph-stats-dash-x.json"))
        let model = GraphViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        #expect(model.graphs == ["-x", " sp"])
    }

    @Test("choosing -x reads graph stats -- -x, and its counts draw")
    func choose() async {
        let runner = scripted()
        let model = GraphViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        await model.choose("-x")
        #expect(runner.calls.last == "read graph stats -- -x")
        #expect(model.stats?.graph == "-x" && model.failure == nil, "failure: \(model.failure ?? "none")")
        #expect(!refusedByTheParse(recordedVerdict(["graph", "stats", "--output-format", "json", "--", "-x"])))
    }

    @Test("rebuild, communities, both dedupes and delete each put the graph after --")
    func acts() async throws {
        let runner = scripted()
        runner.reading(["graph", "stats", "--", "-x"], #"{"built":true,"graph":"-x","kind":"collection","object":"graph.stats","nodes":2,"edges":1}"#)
        runner.reading(["graph", "report"], #"{"graph":"-x"}"#)
        runner.saying(["graph", "dedupe"], "Would merge into a: b\n")
        runner.saying(["graph", "delete", "--", "-x"], "No graph to delete for \"-x\".\n")
        let (built, clustered, merged) = (ScriptedJob(), ScriptedJob(), ScriptedJob())
        let jobs = ScriptedJobs([built, clustered, merged])
        let model = GraphViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        await model.choose("-x")
        model.rebuild()
        #expect(jobs.starts.last?.arguments == ["graph", "build", "--", "-x"], "a collection's graph has no sources: built again")
        try #require(jobs.starts.count == 1)
        built.end()
        #expect(await eventually { !model.isBuilding })
        model.findCommunities()
        #expect(jobs.starts.last?.arguments == ["graph", "communities", "--", "-x"])
        clustered.end()
        #expect(await eventually { !model.isBuilding })
        await model.planDedupe()
        #expect(runner.calls.contains("run graph dedupe --dry-run -- -x"), "the dry run first: a read-back may still be answering")
        model.confirmDedupe()
        #expect(jobs.starts.last?.arguments == ["graph", "dedupe", "--", "-x"])
        merged.end()
        #expect(await eventually { !model.isBuilding })
        await model.confirmDelete("-x")
        #expect(runner.calls.contains("run graph delete -- -x"))
        for line in [["graph", "build", "--", "-x"], ["graph", "communities", "--", "-x"], ["graph", "dedupe", "--dry-run", "--", "-x"],
                     ["graph", "dedupe", "--", "-x"], ["graph", "delete", "--", "-x"]] {
            #expect(!refusedByTheParse(recordedVerdict(line)), "\(line): \(recordedVerdict(line) ?? "never recorded")")
        }
    }

    @Test("a sourced graph's rebuild is graph update -- <graph>")
    func update() async {
        let runner = scripted()
        runner.reading(["graph", "stats", "--", "-x"],
                       #"{"built":true,"graph":"-x","kind":"named","object":"graph.stats","sources":["/Users/someone/code"]}"#)
        let jobs = ScriptedJobs([ScriptedJob()])
        let model = GraphViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        await model.choose("-x")
        model.rebuild()
        #expect(jobs.starts.last?.arguments == ["graph", "update", "--", "-x"])
        #expect(!refusedByTheParse(recordedVerdict(["graph", "update", "--", "-x"])))
    }

    @Test("the dialogs and sheets show the lines the acts run, as a person types them")
    func shownLines() {
        let folder = URL(fileURLWithPath: "/Users/someone/code")
        #expect(GraphViewModel.deleteArguments("-x") == ["graph", "delete", "--", "-x"])
        #expect(GraphViewModel.dedupeArguments("-x", dryRun: true) == ["graph", "dedupe", "--dry-run", "--", "-x"])
        #expect(GraphViewModel.dedupeArguments("-x", dryRun: false) == ["graph", "dedupe", "--", "-x"])
        #expect(GraphViewModel.buildArguments(name: "app", folder: folder) == ["graph", "build", "--source", "/Users/someone/code", "--graph", "app"])
        #expect(Argv.shown(GraphViewModel.deleteArguments("demo")) == "graph delete demo")
        #expect(Argv.shown(GraphViewModel.dedupeArguments("-x", dryRun: true)) == "graph dedupe --dry-run -- -x")
        #expect(Argv.shown(GraphViewModel.buildArguments(name: "-g", folder: folder)) == "graph build --source /Users/someone/code --graph -g")
    }

    @Test("New graph names its graph with --graph, so the repeatable --source never meets a --")
    func newGraph() async {
        let runner = scripted()
        let jobs = ScriptedJobs([ScriptedJob()])
        let model = GraphViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        model.build(name: " -g ", folder: URL(fileURLWithPath: "/Users/someone/code"))
        #expect(jobs.starts.last?.arguments == ["graph", "build", "--source", "/Users/someone/code", "--graph", "-g"], "a typed name, trimmed")
        #expect(model.graph == "-g")
        #expect(recordedVerdict(["graph", "build", "--source", "/Users/someone/code", "--graph", "-g"])?.contains("graph name '-g' is not one both config formats can edit") == true,
                "a name the CLI refuses is refused in its words, never the parser's")
        #expect(recordedVerdict(["graph", "build", "--source", "/Users/someone/code", "--", "-x"]) == "exit 109: The following argument was not expected: -x",
                "--source takes the -- as its own end")
    }
}
