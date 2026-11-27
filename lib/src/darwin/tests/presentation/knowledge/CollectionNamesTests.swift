import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationKnowledge

/// The binary's verdict on `line` (`tests/business/reads/Fixtures/argv-end-
/// of-options-verdicts.txt`, recorded from the built CLI over a sandbox):
/// `help` or `exit <n>: <first line>`; nil when never recorded.
func recordedVerdict(_ line: [String]) -> String? {
    let wanted = line.joined(separator: " ")
    for row in fixture("argv-end-of-options-verdicts", "txt").split(separator: "\n") {
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

/// A listed name reaches the verb as listed (GUI M21). The CLI writes stores
/// named `-x`, `-h`, `--graph` and ` sp` today (`collections-list-dashes.json`,
/// recorded from the built CLI over a sandbox, as are the infos), and its
/// parser reads a dash-led name before `--` as an option: so each name before
/// `--` is scripted to answer CLI11's refusal, and after `--` the recorded
/// document -- a plain prefix on `embed|info` would load a `-x` page on the
/// old line too.
@Suite("Collection names", .timeLimit(.minutes(1)))
@MainActor
struct CollectionNamesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    static let required = ChildExit(reason: .exited, status: 106)

    func collections(_ jobs: [ScriptedJob] = []) async -> (CollectionsViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["embed", "list"], fixture("collections-list-dashes"))
        for name in ["-x", "--graph"] {
            runner.failing(["embed", "info", name], .failed(exit: Self.required, stderrTail: tail("collection is required\nRun with --help for more information.")))
        }
        runner.failing(["embed", "info", "-h"], .undecodable(stdout: fixture("embed-info-help", "txt"), stderrTail: BoundedTail()))
        runner.reading(["embed", "info", "--", "-x"], fixture("collection-info-dash-x"))
        runner.reading(["embed", "info", "--", "-h"], fixture("collection-info-dash-h"))
        runner.reading(["embed", "info", "--", "--graph"], fixture("collection-info-dash-graph"))
        runner.reading(["embed", "info", "--", " sp"], fixture("collection-info-space-sp"))
        runner.reading(["embed", "info", "--", "notes"], fixture("collection-info"))
        runner.answering(["embed", "query", "-x"], .failed(exit: Self.required, stderrTail: tail("collection is required\nRun with --help for more information.")))
        runner.answering(["embed", "query", "--", "-x"], .done(stdout: "0.000  [lexical]  a.txt#0\n"))
        runner.answering(["embed", "delete", "-x"], .failed(exit: Self.required, stderrTail: tail("collection is required\nRun with --help for more information.")))
        runner.answering(["embed", "delete", "--yes", "--", "-x"], .done(stdout: "will delete the whole collection:\n  -x\n\nremoved.\n"))
        let starter = ScriptedJobs(jobs)
        let model = CollectionsViewModel(runner: runner, starter: starter)
        await model.connect(binary: Self.binary)
        return (model, runner, starter)
    }

    func row(_ name: String, in model: CollectionsViewModel) throws -> CollectionRow {
        try #require(model.collections.first { $0.name == name }, "listed: \(model.collections.map(\.name))")
    }

    @Test("the list holds every name the CLI wrote, byte for byte")
    func listed() async {
        let (model, _, _) = await collections()
        #expect(model.collections.map(\.name) == [" sp", "-", "--graph", "--help", "-1", "-h", "-x", "my notes", "notes"])
    }

    @Test("selecting -x reads embed info -- -x, and its page draws")
    func dashXPageLoads() async throws {
        let (model, runner, _) = await collections()
        await model.select(try row("-x", in: model))
        #expect(runner.calls.contains("read embed|info|--|-x"))
        #expect(model.info?.name == "-x" && model.failure == nil, "failure: \(model.failure ?? "none")")
        let read = Argv.adding(["--output-format", "json"], to: ["embed", "info", "--", "-x"])
        #expect(!refusedByTheParse(recordedVerdict(read)), "\(read): \(recordedVerdict(read) ?? "never recorded")")
    }

    @Test("selecting --graph reads embed info -- --graph, and its page draws")
    func dashGraphPageLoads() async throws {
        let (model, runner, _) = await collections()
        await model.select(try row("--graph", in: model))
        #expect(runner.calls.contains("read embed|info|--|--graph"))
        #expect(model.info?.name == "--graph" && model.failure == nil, "failure: \(model.failure ?? "none")")
    }

    @Test("selecting -h reads embed info -- -h, and its page draws: never the verb's help as a document")
    func dashHPageLoads() async throws {
        let (model, runner, _) = await collections()
        await model.select(try row("-h", in: model))
        #expect(runner.calls.contains("read embed|info|--|-h"))
        #expect(model.info?.name == "-h" && model.failure == nil, "failure: \(model.failure ?? "none")")
    }

    @Test("a search puts the collection after --, its question with it: a dash-led question stays the question")
    func search() async throws {
        let (model, runner, _) = await collections()
        await model.select(try row("-x", in: model))
        model.question = "fox"
        await model.search()
        #expect(runner.calls.last == "run embed|query|--|-x|fox")
        #expect(model.results == "0.000  [lexical]  a.txt#0" && model.failure == nil)
        model.question = "-how"
        await model.search()
        #expect(runner.calls.last == "run embed|query|--|-x|-how")
        #expect(!refusedByTheParse(recordedVerdict(["embed", "query", "--", "-x", "fox"])))
    }

    @Test("Ingest more… into -x: its options, then -- and the collection and the path")
    func ingestMoreIntoDashX() async throws {
        let job = ScriptedJob()
        let (model, _, starter) = await collections([job])
        model.newIngest(into: "-x")
        model.ingestForm.path = "/Users/someone/notes"
        model.ingestForm.retriever = "lexical"
        model.ingestForm.graph = true
        #expect(model.ingestForm.arguments == ["embed", "ingest", "--retriever", "lexical", "--graph", "--", "-x", "/Users/someone/notes"])
        model.ingest()
        #expect(starter.starts == [["embed", "ingest", "--retriever", "lexical", "--graph", "--", "-x", "/Users/someone/notes"]])
        job.end()
        #expect(await eventually { !model.isRunning && model.selected == "-x" })
        #expect(!refusedByTheParse(recordedVerdict(["embed", "ingest", "--retriever", "lexical", "--graph", "--", "-x", "/Users/someone/docs/b.txt"])))
    }

    @Test("a delete puts its options before --: the whole collection's --yes, a source's --source")
    func deletes() async throws {
        let whole = CollectionsViewModel.PendingDelete(collection: "-x", source: nil)
        let source = CollectionsViewModel.PendingDelete(collection: "-x", source: "a.txt")
        #expect(whole.arguments == ["embed", "delete", "--yes", "--", "-x"])
        #expect(source.arguments == ["embed", "delete", "--source", "a.txt", "--", "-x"])
        for line in [whole.arguments, source.arguments] {
            #expect(!refusedByTheParse(recordedVerdict(line)), "\(line): \(recordedVerdict(line) ?? "never recorded")")
        }
        #expect(Argv.shown(whole.arguments) == "embed delete --yes -- -x")
        #expect(Argv.shown(CollectionsViewModel.PendingDelete(collection: "notes", source: nil).arguments) == "embed delete --yes notes",
                "the dialog's line shows -- only before a dash-led name")
        let (model, runner, _) = await collections()
        var said: [String] = []
        model.say = { said.append($0) }
        model.pendingDelete = whole
        model.pendingDelete = nil
        await model.confirmDelete(whole)
        #expect(runner.calls.contains("run embed|delete|--yes|--|-x"))
        #expect(said == ["removed."] && model.failure == nil, "failure: \(model.failure ?? "none")")
    }

    @Test("Ingest more… into ' sp' passes the listed name byte for byte, and selects it after")
    func listedNameKept() async throws {
        let job = ScriptedJob()
        let (model, runner, starter) = await collections([job])
        model.newIngest(into: " sp")
        model.ingestForm.path = "/Users/someone/more"
        #expect(model.ingestForm.arguments == ["embed", "ingest", "--", " sp", "/Users/someone/more"])
        model.ingest()
        #expect(starter.starts.first?[3] == " sp")
        job.end()
        // The read-back follows the job's end: wait for it, not only the end.
        #expect(await eventually { !model.isRunning && model.info?.name == " sp" })
        #expect(model.selected == " sp")
        #expect(runner.calls.contains("read embed|info|--| sp"))
    }

    @Test("a typed name is still trimmed -- a listed one edited in the sheet is typed")
    func typedNameTrimmed() async throws {
        var form = CollectionsViewModel.IngestForm()
        form.collection = "  notes  "
        form.path = "/Users/someone/notes"
        #expect(form.arguments == ["embed", "ingest", "--", "notes", "/Users/someone/notes"])
        let job = ScriptedJob()
        let (model, _, _) = await collections([job])
        model.newIngest(into: " sp")
        model.ingestForm.collection = " sp2 "
        model.ingestForm.path = "/Users/someone/notes"
        #expect(model.ingestForm.name == "sp2")
        model.ingestForm.collection = "  notes "
        model.ingest()
        job.end()
        #expect(await eventually { !model.isRunning && model.selected == "notes" })
    }
}
