import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationKnowledge

/// Knowledge's lists do what they say (GUI 4h): a store that never captured
/// is the designed empty state -- `knowledge list` refuses it by design, exit
/// 1 in the CLI's words (`knowledge.cpp`'s `open_collection`, recorded from
/// the branch's binary on a fresh home) -- while every other failure stays
/// one; and a search's lines land on the collection they were asked of.
@Suite("Knowledge correctness", .timeLimit(.minutes(1)))
@MainActor
struct KnowledgeCorrectnessTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let exit1 = ChildExit(reason: .exited, status: 1)
    static let fresh = fixture("knowledge-list-fresh", "err").trimmingCharacters(in: .newlines)

    func records(_ outcome: ReadOutcome) async -> RecordsViewModel {
        let runner = ScriptedRunner()
        runner.failing(["knowledge", "list"], outcome)
        let model = RecordsViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        return model
    }

    @Test("a fresh install's Records tab is the designed empty state, not a failed read")
    func freshInstallIsEmpty() async {
        #expect(Self.fresh == "apogee knowledge: no knowledge collection named 'knowledge' -- capture a record first with 'apogee knowledge capture'")
        let model = await records(.failed(exit: Self.exit1, stderrTail: tail(Self.fresh)))
        #expect(model.listing == .read)
        #expect(model.records.isEmpty)
        #expect(model.emptyText == "No decision records yet. Capture one from a conversation: the clerk distils its reasoning and its decision.")
        #expect(model.failure == nil)
    }

    @Test("a collection the config names is empty the same way, by the CLI's words for it")
    func namedCollectionIsEmpty() async {
        let named = Self.fresh.replacingOccurrences(of: "named 'knowledge'", with: "named 'team-decisions'")
        let model = await records(.failed(exit: Self.exit1, stderrTail: tail(named)))
        #expect(model.listing == .read)
    }

    @Test("every other failed read stays a failure, said in the CLI's words")
    func otherFailuresStayFailures() async {
        let locked = "apogee knowledge: database is locked"
        var model = await records(.failed(exit: Self.exit1, stderrTail: tail(locked)))
        #expect(model.listing == .failed("\(locked) (exited with status 1)"))
        #expect(model.emptyText == "The records could not be read: \(locked) (exited with status 1)")
        model = await records(.timedOut(after: .seconds(60), stderrTail: tail(Self.fresh)))
        #expect(model.listing != .read, "a timed-out read is no refusal, whatever its stderr")
        model = await records(.failed(exit: ChildExit(reason: .signalled, status: 9), stderrTail: tail(Self.fresh)))
        #expect(model.listing != .read, "nor a killed one")
        model = await records(.failed(exit: Self.exit1, stderrTail: tail("warning: no knowledge collection named 'x' -- capture a record first with 'apogee knowledge capture' (in a quote)")))
        #expect(model.listing != .read, "nor the words anywhere but as the refusal")
        model = await records(.couldNotStart("no such file"))
        #expect(model.listing == .failed("no such file"))
    }

    @Test("a search's lines land on the collection they were asked of, never the one chosen since")
    func searchLandsWhereItStarted() async throws {
        let runner = ScriptedRunner()
        runner.reading(["embed", "list"], fixture("collections-list"))
        runner.reading(["embed", "info"], fixture("collection-info"))
        runner.answering(["embed", "query"], .done(stdout: fixture("collection-query", "txt")))
        let model = CollectionsViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        let notes = try #require(model.collections.first { $0.name == "notes" })
        let knowledge = try #require(model.collections.first { $0.name == "knowledge" })
        await model.select(notes)
        model.question = "how does a heat pump work"
        let gate = runner.holding(["embed", "query"])
        let searching = Task { await model.search() }
        #expect(await eventually { gate.arrived == 1 })
        await model.select(knowledge)
        gate.open()
        await searching.value
        #expect(model.selected == "knowledge")
        #expect(model.results == nil, "notes' lines never shown under knowledge")
        #expect(model.failure == nil)
        await model.search()
        #expect(model.results?.hasPrefix("0.000  [lexical]  heat-pumps.md#0") == true, "asked of knowledge, they land")
    }
}

/// GUI 6h: Knowledge shows what it was asked about. The collections are the
/// branch's `apogee-dev` over a sandbox home -- `mixed` (three notes under
/// one mock embedder, a fourth under another) and `notes` (three notes,
/// `--retriever vector`) -- and a question's answers are `knowledge
/// query`'s recorded document over every branch, narrowed here to one
/// branch's records for that branch's reply.
@Suite("Knowledge shows what it was asked about (GUI 6h)", .timeLimit(.minutes(1)))
@MainActor
struct KnowledgeCurrentTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let question = "why was the cancel button removed"

    /// The recorded every-branch answer, narrowed to `status`'s records -- a
    /// handmade reply from the recorded one, as `--status <status>` would
    /// answer.
    static func only(_ status: String, of document: String) -> String {
        var object = try! JSONSerialization.jsonObject(with: Data(document.utf8)) as! [String: Any]
        let records = object["records"] as! [[String: Any]]
        object["records"] = records.filter { (($0["record"] as? [String: Any])?["status"] as? String) == status }
        return String(decoding: try! JSONSerialization.data(withJSONObject: object), as: UTF8.self)
    }

    func records() async -> (RecordsViewModel, ScriptedRunner) {
        let runner = ScriptedRunner()
        runner.reading(["knowledge", "list"], fixture("knowledge-list"))
        let every = fixture("knowledge-query")
        runner.answering(["knowledge", "query", "--status", ""], .done(stdout: every))
        runner.answering(["knowledge", "query", "--status", "shipped"], .done(stdout: Self.only("shipped", of: every)))
        runner.answering(["knowledge", "query", "--status", "rejected"], .done(stdout: Self.only("rejected", of: every)))
        let model = RecordsViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        return (model, runner)
    }

    func asked(_ runner: ScriptedRunner) -> [String] {
        runner.calls.filter { $0.hasPrefix("run knowledge|query") }
    }

    @Test("a Branch change asks the standing question again under the new branch; its matches are the new reply's")
    func branchChangeReasks() async {
        let (model, runner) = await records()
        model.branch = "shipped"
        model.question = Self.question
        await model.search()
        #expect(model.query?.matches.map(\.record.status) == ["shipped", "shipped"])
        model.question = "typed since, not asked"
        model.branch = nil
        await model.branchChanged()
        #expect(asked(runner).last == "run knowledge|query|--status||--json|--|\(Self.question)", "the question shown, asked over every branch")
        #expect(model.query?.matches.count == 3 && model.query?.matches.first?.record.status == "rejected")
        #expect(model.asked == RecordsViewModel.Asked(question: Self.question, branch: nil))
        model.branch = "rejected"
        await model.branchChanged()
        #expect(asked(runner).last == "run knowledge|query|--status|rejected|--json|--|\(Self.question)")
        #expect(model.query?.matches.map(\.record.status) == ["rejected"])
    }

    @Test("the old branch's matches go at the change; a reply held past a second change is dropped")
    func heldReplyDropped() async {
        let (model, runner) = await records()
        model.branch = "shipped"
        model.question = Self.question
        await model.search()
        let held = runner.holding(["knowledge", "query", "--status", ""])
        model.branch = nil
        let everyBranch = Task { await model.branchChanged() }
        #expect(await eventually { held.arrived == 1 })
        #expect(model.query == nil, "the shipped matches never stand under Every branch")
        model.branch = "rejected"
        await model.branchChanged()
        #expect(model.query?.matches.map(\.record.status) == ["rejected"])
        held.open()
        await everyBranch.value
        #expect(model.query?.matches.map(\.record.status) == ["rejected"], "every branch's late reply is dropped")
        #expect(model.failure == nil)
    }

    @Test("with no question standing the list just filters; Clear drops a reply still coming")
    func noQuestionFilters() async {
        let (model, runner) = await records()
        model.branch = "rejected"
        await model.branchChanged()
        #expect(asked(runner).isEmpty, "nothing asked")
        #expect(model.shown.map(\.status) == ["rejected"])
        model.question = Self.question
        let held = runner.holding(["knowledge", "query", "--status", "rejected"])
        let asking = Task { await model.search() }
        #expect(await eventually { held.arrived == 1 })
        model.clearSearch()
        held.open()
        await asking.value
        #expect(model.query == nil && model.asked == nil, "cleared: its reply never lands")
        model.branch = nil
        await model.branchChanged()
        #expect(asked(runner).count == 1, "nothing standing to ask again")
    }

    func collections(_ jobs: [ScriptedJob]) async -> (CollectionsViewModel, ScriptedRunner) {
        let runner = ScriptedRunner()
        runner.reading(["embed", "list"], fixture("collections-list-vectors"))
        runner.reading(["embed", "info", "mixed"], fixture("collection-info-vectors-mixed"))
        runner.reading(["embed", "info", "notes"], fixture("collection-info-vectors"))
        runner.answering(["embed", "query"], .done(stdout: fixture("collection-query", "txt")))
        let model = CollectionsViewModel(runner: runner, starter: ScriptedJobs(jobs))
        await model.connect(binary: Self.binary)
        return (model, runner)
    }

    @Test("an ingest into another collection drops the shown one's results and page, at once")
    func ingestIntoAnotherDropsResults() async throws {
        let job = ScriptedJob()
        let (model, runner) = await collections([job])
        await model.select(try #require(model.collections.first { $0.name == "mixed" }))
        model.question = "how does a heat pump work"
        await model.search()
        #expect(model.results != nil && model.info?.name == "mixed")
        model.newIngest()
        model.ingestForm.collection = "notes"
        model.ingestForm.path = "/Users/someone/notes"
        model.ingest()
        let reading = runner.holding(["embed", "info", "notes"])
        job.end()
        #expect(await eventually { reading.arrived == 1 })
        #expect(model.selected == "notes")
        #expect(model.info == nil && model.results == nil, "no moment holds mixed's page or lines under notes")
        reading.open()
        #expect(await eventually { !model.isRunning && model.info?.name == "notes" })
        #expect(model.results == nil)
        #expect(model.info.map(CollectionsViewModel.vectors) == "3 of 3 chunks, 8-d [mock-embed]")
    }

    @Test("an ingest into the collection shown keeps its results: they are its own")
    func ingestMoreKeepsResults() async throws {
        let job = ScriptedJob()
        let (model, _) = await collections([job])
        await model.select(try #require(model.collections.first { $0.name == "notes" }))
        model.question = "how does a heat pump work"
        await model.search()
        let lines = try #require(model.results)
        model.newIngest(into: "notes")
        model.ingestForm.path = "/Users/someone/more-notes"
        model.ingest()
        job.end()
        #expect(await eventually { !model.isRunning })
        #expect(model.results == lines && model.info?.name == "notes")
    }
}
