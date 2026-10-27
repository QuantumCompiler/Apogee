import BusinessReads
import BusinessSession
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationSessions

/// The older binary's refusal of `--output-format` on a read that had none:
/// exit 109, its recorded stderr the tail.
func refusedFlag(_ name: String) -> ReadOutcome {
    var tail = BoundedTail()
    tail.append(Data(printed(name).utf8))
    return .failed(exit: ChildExit(reason: .exited, status: 109), stderrTail: tail)
}

/// GUI M15: a saved chat's Details… and the inspector's Recall read the
/// documents the CLI's M31 gave `chats info` and `chats recall` -- asked first
/// on a binary dated 2026-10-26 or later, read as text on an older one or on
/// a refusal of the flag -- and Forget names the summaries it erases.
@Suite("Sessions: details and recall as documents (GUI M15)", .timeLimit(.minutes(1)))
@MainActor
struct ReadsAsDocumentsTests {
    /// The three saved chats the branch binary listed over M15's sandbox:
    /// the first chat written by the installed, older binary.
    static let listed = printed("chats-list-recall.json")
    static let port = "20261027-015932-30ca"

    /// The library on a binary whose vocabulary is `vocabulary`, its list
    /// read, out of view: no session started.
    func library(_ runner: ScriptedRunner, vocabulary: String = schema("machine-schema-2026-10-26")) async -> SessionLibraryViewModel {
        runner.reading(["chats", "list"], Self.listed)
        runner.commanding(["__machine-schema"], .done(stdout: vocabulary))
        let library = SessionLibraryViewModel(
            starter: ScriptedStarter(sessions: []), runner: runner, preferences: Preferences(store: MemoryStore()))
        await library.start(inView: false)
        return library
    }

    func portChat(_ library: SessionLibraryViewModel) throws -> ChatSummary {
        try #require(library.chats.first { $0.id == Self.port })
    }

    // MARK: - Details…

    @Test("Details on an M31 binary is chats info's document: its typed facts drawn, nothing run as text")
    func detailsFromTheDocument() async throws {
        let runner = ScriptedRunner()
        runner.reading(["chats", "info"], printed("chats-info.json"))
        runner.commanding(["chats", "info"], .done(stdout: printed("chats-info-text.txt")))
        let library = await library(runner)
        #expect(library.readsDocuments)
        await library.showInfo(try portChat(library))
        #expect(runner.calls.contains("read chats info \(Self.port)"))
        #expect(!runner.calls.contains { $0.hasPrefix("run chats info") }, "the document answered: never both")
        let info = try #require(library.info)
        let details = try #require(info.details)
        #expect(info.failure == nil && info.fields == details.fields)
        #expect(details.messages == 4 && details.compactions == 0 && details.backend == "actor-port")
        #expect(info.fields.first { $0.label == "compacted" }?.value == "never", "a fact only the document states")
        for line in Field.lines(printed("chats-info-text.txt")) {
            #expect(info.fields.contains(line), "the text's \(line.label) drawn alike")
        }
    }

    @Test("an older binary's Details is read as today: the command, its lines")
    func detailsOnAnOlderBinary() async throws {
        let runner = ScriptedRunner()
        runner.commanding(["chats", "info"], .done(stdout: printed("chats-info-text.txt")))
        let library = await library(runner, vocabulary: schema("machine-schema"))
        #expect(!library.readsDocuments)
        await library.showInfo(try portChat(library))
        #expect(!runner.calls.contains { $0.hasPrefix("read chats info") })
        #expect(runner.calls.last == "run chats info \(Self.port)")
        #expect(library.info?.details == nil)
        #expect(library.info?.fields == Field.lines(printed("chats-info-text.txt")))
    }

    @Test("a binary dated M31 that refuses the flag -- a build between M29 and M31 -- falls back to the text")
    func detailsRefusedFallsBack() async throws {
        let runner = ScriptedRunner()
        runner.reading(["chats", "info"], refusedFlag("chats-info-json-refused-older.err"))
        runner.commanding(["chats", "info"], .done(stdout: printed("chats-info-text.txt")))
        let library = await library(runner)
        await library.showInfo(try portChat(library))
        #expect(runner.calls.suffix(2) == ["read chats info \(Self.port)", "run chats info \(Self.port)"])
        #expect(library.info?.details == nil && library.info?.failure == nil)
        #expect(library.info?.fields == Field.lines(printed("chats-info-text.txt")))
    }

    @Test("a chat the CLI can't find is said in its words, from the document's refusal; nothing run again")
    func detailsOfAMissingChat() async throws {
        let runner = ScriptedRunner()
        var tail = BoundedTail()
        tail.append(Data(printed("chats-info-missing-json.err").utf8))
        runner.reading(["chats", "info"], .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail))
        let library = await library(runner)
        await library.showInfo(try portChat(library))
        #expect(library.info?.failure == "apogee chats: no chat named 'no-such-chat' (exited with status 1)")
        #expect(library.info?.fields == [] && library.info?.details == nil)
        #expect(!runner.calls.contains { $0.hasPrefix("run chats info") })
    }

    // MARK: - Recall

    @Test("Recall on an M31 binary lists each summary's name and date; a summary's text is shown once chosen")
    func recallListed() async throws {
        let runner = ScriptedRunner()
        runner.reading(["chats", "recall"], printed("chats-recall.json"))
        let library = await library(runner)
        await library.refreshRecall()
        #expect(runner.calls.contains("read chats recall"))
        #expect(!runner.calls.contains("run chats recall"))
        #expect(library.recall == "recall holds 3 chat summaries in /Users/someone/.apogee/memory/chats.db", "the count line, as the text says it")
        let listed = try #require(library.inspector.recallSummaries)
        #expect(listed.map(\.title) == ["Launch Emails", "Dev Server Port", "Release Checklist"])
        #expect(listed.allSatisfy { $0.updated != nil })
        #expect(library.inspector.recallSelected == nil, "no text until one is chosen")
        library.selectRecall(Self.port)
        #expect(library.inspector.recallSelected?.summary == "Asked the dev server's port (8080) and confirmed the launch on 2026-11-12.")
        library.selectRecall(nil)
        #expect(library.inspector.recallSelected == nil)
    }

    @Test("Forget's question says how many and names them; it runs chats recall --forget --yes as text, then reads the listing back")
    func forgetNamesThem() async throws {
        let runner = ScriptedRunner()
        runner.reading(["chats", "recall"], printed("chats-recall.json"))
        runner.commanding(["chats", "recall", "--forget"], .done(stdout: printed("chats-recall-forget-three.txt")))
        let library = await library(runner)
        var said: [String] = []
        library.say = { said.append($0) }
        library.selectRecall(Self.port)
        await library.askToForgetRecall()
        #expect(library.pending == .forgetRecall)
        #expect(library.canForgetRecall)
        let asked = library.forgetRecallMessage
        #expect(asked.contains("3 chat summaries"))
        for name in ["Launch Emails", "Dev Server Port", "Release Checklist"] {
            #expect(asked.contains("“\(name)”"), "names \(name)")
        }
        #expect(!runner.calls.contains { $0.hasPrefix("run chats recall --forget") }, "nothing runs before the person's word")
        runner.reading(["chats", "recall"], printed("chats-recall-empty.json"))
        await library.confirm()
        #expect(runner.calls.contains("run chats recall --forget --yes"), "the CLI refuses --forget with --output-format: it answers in text")
        #expect(!runner.calls.contains { $0.hasPrefix("read chats recall --forget") })
        #expect(runner.calls.last == "read chats recall", "read back as the document")
        #expect(said.last?.hasPrefix("forgot 3 chat summaries") == true)
        #expect(library.inspector.recallSummaries == [] && library.recallSelection == nil, "the chosen one went with them")
        #expect(!library.canForgetRecall, "nothing left to forget")
        await library.askToForgetRecall()
        #expect(library.pending == nil, "nothing asked of an empty recall: the CLI says nothing to forget")
    }

    @Test("an older binary's Recall is today's count line and its question today's; a refused flag falls back to it too")
    func recallOnAnOlderBinary() async throws {
        let older = ScriptedRunner()
        older.commanding(["chats", "recall"], .done(stdout: printed("chats-recall-older.txt")))
        let library = await library(older, vocabulary: schema("machine-schema"))
        await library.refreshRecall()
        #expect(older.calls.last == "run chats recall" && !older.calls.contains("read chats recall"))
        #expect(library.recall == "recall holds 3 chat summaries in /Users/someone/.apogee/memory/chats.db")
        #expect(library.inspector.recallSummaries == nil, "no summaries claimed where the binary listed none")
        await library.askToForgetRecall()
        #expect(library.forgetRecallMessage.hasPrefix("Now: recall holds 3 chat summaries"))
        #expect(library.canForgetRecall)

        let refusing = ScriptedRunner()
        refusing.reading(["chats", "recall"], refusedFlag("chats-recall-json-refused-older.err"))
        refusing.commanding(["chats", "recall"], .done(stdout: printed("chats-recall-older.txt")))
        let fallback = await self.library(refusing)
        await fallback.refreshRecall()
        #expect(refusing.calls.suffix(2) == ["read chats recall", "run chats recall"])
        #expect(fallback.recall == library.recall && fallback.recallListing == nil && fallback.recallFailure == nil)
    }

    @Test("the text fallback reads the count line even where an M31 binary's text lists the summaries after it")
    func countLineFromAListingText() async {
        let runner = ScriptedRunner()
        runner.commanding(["chats", "recall"], .done(stdout: printed("chats-recall-listing.txt")))
        let library = await library(runner, vocabulary: schema("machine-schema"))
        await library.refreshRecall()
        #expect(library.recall == "recall holds 3 chat summaries in /Users/someone/.apogee/memory/chats.db")
    }
}
