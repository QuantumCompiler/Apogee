import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationKnowledge

/// GUI 4k, the sixth gap: a record's archived raw conversation, viewable --
/// `knowledge info <id> --raw`, which prints it in text mode alone, run as a
/// command and shown as printed; never read from the archive's own path.
@Suite("Knowledge: a record's raw conversation (GUI 4k)", .timeLimit(.minutes(1)))
@MainActor
struct RawConversationTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let id = "kr-20261026T142411Z-2e8272"

    /// The record the branch's binary captured over a sandbox, as `knowledge
    /// list` read it.
    func records(_ runner: ScriptedRunner) async throws -> (RecordsViewModel, KnowledgeRecord) {
        runner.reading(["knowledge", "list"], fixture("knowledge-list-one"))
        let model = RecordsViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        let record = try #require(model.records.first)
        model.select(record)
        return (model, record)
    }

    @Test("a record with an archive offers its conversation; asked, it is knowledge info <id> --raw, the text after the CLI's own marker")
    func shown() async throws {
        let runner = ScriptedRunner()
        runner.answering(["knowledge", "info"], .done(stdout: fixture("knowledge-info-raw", "txt")))
        let (model, record) = try await records(runner)
        #expect(record.id == Self.id && record.rawRef != nil)
        #expect(model.raw == nil, "asked for, never read on its own")
        await model.showRaw(record)
        #expect(runner.calls.last == "run knowledge|info|\(Self.id)|--raw")
        #expect(!runner.calls.contains { $0.hasPrefix("read knowledge|info") }, "text mode: never with --output-format")
        #expect(model.raw == .init(record: Self.id, state: .shown(.text(
            "Ada: testers keep hitting cancel when they mean back. Grace: then we drop the cancel button from checkout. Ada: agreed, ship it."))))
        model.hideRaw()
        #expect(model.raw == nil)
    }

    @Test("an archive the CLI can't read is said as it says it; a record it can't find, in its words")
    func unavailableAndRefused() async throws {
        let runner = ScriptedRunner()
        runner.answering(["knowledge", "info"], .done(stdout: fixture("knowledge-info-raw-unavailable", "txt")))
        let (model, record) = try await records(runner)
        await model.showRaw(record)
        #expect(model.raw?.state == .shown(.unavailable))
        var tail = BoundedTail()
        tail.append(Data(fixture("knowledge-info-raw-missing", "err").utf8))
        runner.answering(["knowledge", "info"], .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail))
        await model.showRaw(record)
        #expect(model.raw?.state == .failed("apogee knowledge: no record 'kr-nosuch' in 'knowledge' (exited with status 1)"))
    }

    @Test("choosing another record puts the conversation away")
    func anotherRecord() async throws {
        let runner = ScriptedRunner()
        runner.answering(["knowledge", "info"], .done(stdout: fixture("knowledge-info-raw", "txt")))
        runner.reading(["knowledge", "list"], fixture("knowledge-list"))
        let model = RecordsViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        let first = try #require(model.records.first)
        let second = try #require(model.records.dropFirst().first)
        model.select(first)
        await model.showRaw(first)
        #expect(model.raw?.record == first.id)
        model.select(second)
        #expect(model.raw == nil)
    }
}
