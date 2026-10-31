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
