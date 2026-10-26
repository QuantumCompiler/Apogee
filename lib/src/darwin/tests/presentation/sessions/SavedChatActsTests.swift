import BusinessReads
import BusinessSession
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationSessions

/// A text the branch's binary printed over a sandbox home, the home written
/// `/Users/someone` (tests/business/reads/Fixtures).
func printed(_ name: String) -> String {
    let tests = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
    return (try? String(contentsOf: tests.appendingPathComponent("business/reads/Fixtures/\(name)"), encoding: .utf8)) ?? ""
}

/// The binary's refusal: exit 1, its recorded stderr the tail.
func refusal(_ name: String) -> CommandOutcome {
    var tail = BoundedTail()
    tail.append(Data(printed(name).utf8))
    return .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail)
}

/// GUI 4k, the first gap: a saved conversation's details, its compaction --
/// asked first -- and what recall holds of earlier chats, with Forget beside
/// it, all through the binary's own verbs, read back.
@Suite("Sessions: a saved chat's details, compaction and recall (GUI 4k)", .timeLimit(.minutes(1)))
@MainActor
struct SavedChatActsTests {
    /// Two saved chats, as the branch's binary listed them.
    static let listed = printed("chats-list-two.json")
    static let first = "20261026-142409-5159"

    /// The library on a binary whose vocabulary is `vocabulary`, its list
    /// read, out of view: no session started.
    func library(
        _ runner: ScriptedRunner, vocabulary: String = schema("machine-schema"), sessions: [ScriptedSession] = [],
        open: String? = nil
    ) async -> (SessionLibraryViewModel, ScriptedStarter) {
        runner.reading(["chats", "list"], Self.listed)
        runner.commanding(["__machine-schema"], .done(stdout: vocabulary))
        let starter = ScriptedStarter(sessions: sessions)
        let library = SessionLibraryViewModel(
            starter: starter, runner: runner, preferences: Preferences(store: MemoryStore(open.map { ["openChat": $0] } ?? [:])))
        await library.start(inView: open != nil)
        return (library, starter)
    }

    @Test("Details is chats info, run as a command and read a label and value a line")
    func infoReadAsText() async throws {
        let runner = ScriptedRunner()
        runner.commanding(["chats", "info"], .done(stdout: printed("chats-info.txt")))
        let (library, _) = await library(runner)
        let chat = try #require(library.chats.first)
        await library.showInfo(chat)
        #expect(runner.calls.last == "run chats info \(Self.first)")
        let info = try #require(library.info)
        #expect(info.chat == chat && info.failure == nil)
        #expect(info.fields.map(\.label) == ["id", "title", "backend", "started", "updated", "turns", "messages"])
        #expect(info.fields.first { $0.label == "started" }?.value == "2026-10-26T14:24:09Z", "a value's own colons kept")
        #expect(info.fields.first { $0.label == "messages" }?.value == "4")
    }

    @Test("a chat the CLI can't find says so in its words")
    func infoRefused() async throws {
        let runner = ScriptedRunner()
        runner.commanding(["chats", "info"], refusal("chats-info-missing.err"))
        let (library, _) = await library(runner)
        await library.showInfo(try #require(library.chats.first))
        #expect(library.info?.fields == [])
        #expect(library.info?.failure?.hasPrefix("apogee chats: no chat named") == true)
    }

    @Test("Compact asks first, then runs chats compact --yes, reads back and says the CLI's line")
    func compactAsksThenRuns() async throws {
        let runner = ScriptedRunner()
        runner.commanding(["chats", "compact"], .done(stdout: printed("chats-compact.txt")))
        runner.commanding(["chats", "info"], .done(stdout: printed("chats-info-compacted.txt")))
        let (library, _) = await library(runner)
        var said: [String] = []
        library.say = { said.append($0) }
        let chat = try #require(library.chats.first)
        #expect(library.canCompact(chat))
        await library.showInfo(chat)
        library.askToCompact(chat)
        #expect(library.pending == .compact(chat))
        #expect(!runner.calls.contains { $0.hasPrefix("run chats compact") }, "nothing runs before the person's word")
        let reads = runner.calls.filter { $0 == "read chats list" }.count
        await library.confirm()
        #expect(runner.calls.contains("run chats compact \(Self.first) --yes"), "the CLI refuses it on a pipe without --yes")
        #expect(runner.calls.filter { $0 == "read chats list" }.count == reads + 1, "read back")
        #expect(library.info?.fields.first { $0.label == "compacted" }?.value == "1 time(s)", "its details read again")
        #expect(said.last == "compacted \(Self.first): 4 messages into 2, by compactor")
        #expect(library.failure == nil)
    }

    @Test("nothing to fold is said as the CLI says it, and a dismissed question runs nothing")
    func compactNothingAndDismissed() async throws {
        let runner = ScriptedRunner()
        runner.commanding(["chats", "compact"], .done(stdout: printed("chats-compact-again.txt")))
        let (library, _) = await library(runner)
        var said: [String] = []
        library.say = { said.append($0) }
        let chat = try #require(library.chats.first)
        library.askToCompact(chat)
        library.dismissPending()
        #expect(!runner.calls.contains { $0.hasPrefix("run chats compact") })
        library.askToCompact(chat)
        await library.confirm()
        #expect(said.last == "nothing to compact in \(Self.first) -- no exchange to fold yet")
    }

    @Test("the open session's own conversation, its child running, is compacted by its session -- never under it")
    func openSessionCompactsInPlace() async throws {
        let runner = ScriptedRunner()
        let child = ScriptedSession()
        let (library, _) = await library(runner, sessions: [child], open: Self.first)
        child.emit(#"{"type":"session","protocol_version":1,"model":"actor","next_turn":3,"chat_id":"20261026-142409-5159","capabilities":{"events":["session"],"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"tools":true,"ask":true,"schema":"2026-10-25"}}"#)
        #expect(await eventually { library.current.transcript.status == .ready })
        let chat = try #require(library.chats.first { $0.id == Self.first })
        #expect(library.canCompact(chat))
        library.askToCompact(chat)
        await library.confirm()
        #expect(await eventually { child.calls.contains("compact") }, "the session's own compact line")
        #expect(!runner.calls.contains { $0.hasPrefix("run chats compact") })
    }

    @Test("a binary before track 52 has no chats compact or chats recall: neither is offered, and nothing runs")
    func olderBinaryOffersNeither() async throws {
        let runner = ScriptedRunner()
        let (library, _) = await library(runner, vocabulary: schema("machine-schema-pre52-handmade"))
        let chat = try #require(library.chats.first)
        #expect(!library.canCompact(chat))
        library.askToCompact(chat)
        #expect(library.pending == nil)
        await library.refreshRecall()
        await library.askToForgetRecall()
        #expect(library.recall == nil && library.pending == nil)
        #expect(!runner.calls.contains { $0.hasPrefix("run chats") })
    }

    @Test("recall's holdings are read as the CLI says them, and forgotten only once seen and confirmed -- with --yes")
    func recallSeenThenForgotten() async throws {
        let runner = ScriptedRunner()
        runner.commanding(["chats", "recall"], .done(stdout: printed("chats-recall.txt")))
        let (library, _) = await library(runner)
        var said: [String] = []
        library.say = { said.append($0) }
        await library.refreshRecall()
        #expect(library.recall == "recall holds 2 chat summaries in /Users/someone/.apogee/memory/chats.db")
        #expect(library.inspector.recall == library.recall)
        await library.askToForgetRecall()
        #expect(library.pending == .forgetRecall)
        #expect(!runner.calls.contains { $0.hasPrefix("run chats recall --forget") })
        runner.commanding(["chats", "recall", "--forget"], .done(stdout: printed("chats-recall-forget.txt")))
        await library.confirm()
        #expect(runner.calls.contains("run chats recall --forget --yes"))
        #expect(said.last?.hasPrefix("forgot 2 chat summaries -- every transcript stays") == true)
        #expect(runner.calls.last == "run chats recall", "read back")
    }
}
