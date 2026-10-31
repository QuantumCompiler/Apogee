import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationKnowledge

/// GUI M15, Knowledge's side: on a binary dated 2026-10-26 or later a
/// record's raw conversation is `knowledge info --raw`'s document's `raw`
/// (`null` where the archive can't be read); a document without `raw` -- the
/// older binary drops it silently -- and an older binary read the text as
/// before.
@Suite("Knowledge: a record's raw conversation from its document (GUI M15)", .timeLimit(.minutes(1)))
@MainActor
struct RawDocumentTests {
    static let id = "kr-20261027T015935Z-e9ccd8"

    /// The record the branch binary captured over M15's sandbox, as `knowledge
    /// list` read it, on a binary whose vocabulary is `vocabulary`.
    func records(_ runner: ScriptedRunner, vocabulary: String?) async throws -> (RecordsViewModel, KnowledgeRecord) {
        runner.reading(["knowledge", "list"], fixture("knowledge-list-raw"))
        let model = RecordsViewModel(runner: runner, starter: ScriptedJobs([]))
        model.vocabulary = { vocabulary }
        await model.connect(binary: RawConversationTests.binary)
        let record = try #require(model.records.first)
        model.select(record)
        return (model, record)
    }

    @Test("an M31 binary's raw conversation is its document's raw, exactly; nothing run as text")
    func fromTheDocument() async throws {
        let runner = ScriptedRunner()
        runner.reading(["knowledge", "info"], fixture("knowledge-info-raw"))
        let (model, record) = try await records(runner, vocabulary: "2026-10-26")
        #expect(record.id == Self.id && record.rawRef != nil)
        await model.showRaw(record)
        #expect(runner.calls.last == "read knowledge|info|\(Self.id)|--raw")
        #expect(!runner.calls.contains { $0.hasPrefix("run knowledge|info") }, "the document answered: never both")
        #expect(model.raw == .init(record: Self.id, state: .shown(.text(
            "Ada: testers keep hitting cancel when they mean back. Grace: then we drop the cancel button from checkout. Ada: agreed, ship it."))))
        #expect(model.raw?.state == .shown(RawConversation(printed: fixture("knowledge-info-raw-text", "txt"))!), "the conversation the text shows")
    }

    @Test("raw null is an archive the CLI can't read, said so")
    func nullIsUnavailable() async throws {
        let runner = ScriptedRunner()
        runner.reading(["knowledge", "info"], fixture("knowledge-info-raw-null"))
        let (model, record) = try await records(runner, vocabulary: "2026-10-26")
        await model.showRaw(record)
        #expect(model.raw?.state == .shown(.unavailable))
        #expect(!runner.calls.contains { $0.hasPrefix("run knowledge|info") })
    }

    @Test("a document without raw -- the older binary's -- falls back to the text, cut at the CLI's marker")
    func noRawFallsBack() async throws {
        let runner = ScriptedRunner()
        runner.reading(["knowledge", "info"], fixture("knowledge-info-raw-older"))
        runner.answering(["knowledge", "info"], .done(stdout: fixture("knowledge-info-raw-text", "txt")))
        let (model, record) = try await records(runner, vocabulary: "2026-10-26")
        await model.showRaw(record)
        #expect(runner.calls.suffix(2) == ["read knowledge|info|\(Self.id)|--raw", "run knowledge|info|\(Self.id)|--raw"])
        #expect(model.raw?.state == .shown(RawConversation(printed: fixture("knowledge-info-raw-text", "txt"))!))
    }

    @Test("an older binary is read as today: the command alone")
    func olderBinary() async throws {
        let runner = ScriptedRunner()
        runner.answering(["knowledge", "info"], .done(stdout: fixture("knowledge-info-raw-text", "txt")))
        let (model, record) = try await records(runner, vocabulary: "2026-10-25")
        await model.showRaw(record)
        #expect(runner.calls.last == "run knowledge|info|\(Self.id)|--raw")
        #expect(!runner.calls.contains { $0.hasPrefix("read knowledge|info") })
        if case .shown(.text(let text))? = model.raw?.state {
            #expect(text.hasPrefix("Ada: testers keep hitting cancel"))
        } else {
            Issue.record("not shown: \(String(describing: model.raw))")
        }
    }
}
