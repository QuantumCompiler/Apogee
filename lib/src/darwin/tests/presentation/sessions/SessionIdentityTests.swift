import BusinessReads
import BusinessSession
import DataPreferences
import DataProtocol
import Foundation
import PresentationSession
import Testing

@testable import PresentationSessions

/// GUI M10, the second defect: a new session opened from an unsent new one
/// is the chat it is -- the one its `session` event names (the CLI's M28
/// `chat_id`), else the app's guess made right: measured against `chats
/// list` read afresh once the session left has closed, passing over a chat
/// with no turns -- and Rename, Delete and a relaunch act on it.
///
/// Over the binaries' own recordings, two scripted mocks in a sandbox
/// (`claude` the default, `Gemma4-26B-Instruct` standing for the local
/// Gemma): a new session closed unsent, then a new session that answers on
/// claude, is switched by `set`, and answers on the local model -- the list
/// read after each turn. On the branch binary nothing of the unsent session
/// is written; a binary before M28 named no chat and saved the unsent one
/// empty (its file, kept from M28's own probe, listed as `chats-list-unsent-
/// older.json`).
@Suite("A new session is the chat it is (M10)", .timeLimit(.minutes(1)))
@MainActor
struct SessionIdentityTests {
    static func stream(_ name: String) -> [String] {
        let tests = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let text = (try? String(contentsOf: tests.appendingPathComponent("data/protocol/Fixtures/\(name).jsonl"), encoding: .utf8)) ?? ""
        return text.split(separator: "\n").map(String.init)
    }

    static func type(_ line: String) -> String? { JSONValue.parse(line)?["type"]?.stringValue }

    /// The branch binary's: the unsent session (it named `unsentID`, wrote
    /// nothing), then the switched one (it named `switchedID`).
    static let unsent = stream("new-unsent")
    static let switched = stream("switched-to-local")
    static let unsentID = "20261026-172749-5dec"
    static let switchedID = "20261026-172749-d4cb"
    /// The same, as a binary before M28 wrote them: no `chat_id`.
    static let unsentOlder = stream("new-unsent-older-handmade")
    static let switchedOlder = stream("switched-to-local-older-handmade")
    static let switchedOlderID = "20261026-172800-d639"
    /// The empty chat a binary before M28 saved when a new session on
    /// claude closed unsent.
    static let emptyID = "20261025-050011-73de"
    static let turns = ["Which port does the dev server use?", "And on the local model?"]

    func library(_ runner: ScriptedRunner, _ starter: ScriptedStarter, _ store: MemoryStore) -> SessionLibraryViewModel {
        SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: store))
    }

    /// The library started in view on an empty list, its first session --
    /// a new one -- having said `session`; returns once it is ready.
    func unsentOpen(_ runner: ScriptedRunner, _ starter: ScriptedStarter, _ store: MemoryStore, saying line: String) async -> SessionLibraryViewModel {
        runner.reading(["chats", "list"], printed("chats-list-unsent.json"))
        let library = library(runner, starter, store)
        await library.start(inView: true)
        _ = await eventually { starter.openedCount == 1 }
        starter.handed[0].emit(line)
        _ = await eventually { library.current.transcript.status == .ready }
        return library
    }

    /// The open session's first turn sent and answered on claude, as recorded.
    func firstTurn(_ library: SessionLibraryViewModel, _ child: ScriptedSession, _ lines: [String]) async {
        library.current.draft = Self.turns[0]
        library.current.submit()
        _ = await eventually { child.calls.contains("user: \(Self.turns[0])") }
        let result = lines.firstIndex { Self.type($0) == "result" }!
        lines[1...result].forEach(child.emit)
        _ = await eventually { library.current.transcript.acceptsInput }
    }

    // MARK: - A binary before M28: the guess, made right

    @Test("before M28: a new session opened from an unsent one is never the unsent one's empty chat -- it is its own, once it appears")
    func olderBinaryGuessesRight() async throws {
        let runner = ScriptedRunner()
        let store = MemoryStore()
        let starter = ScriptedStarter(sessions: [ScriptedSession("unsent"), ScriptedSession("next"), ScriptedSession("after")])
        let library = await unsentOpen(runner, starter, store, saying: Self.unsentOlder[0])
        // Its input closed, a binary before M28 saves the unsent chat, empty.
        runner.reading(["chats", "list"], printed("chats-list-unsent-older.json"))
        await library.newSession()
        #expect(starter.handed[0].calls == ["stop"])
        #expect(await eventually { starter.openedCount == 2 })
        let next = starter.handed[1]
        next.emit(Self.switchedOlder[0])
        #expect(await eventually { library.current.transcript.status == .ready })
        #expect(library.chats.map(\.id) == [Self.emptyID], "the empty chat is listed")
        #expect(library.current.chatID == nil, "nothing of the new session is saved yet: the empty chat is not it")
        runner.reading(["chats", "list"], printed("chats-list-switched-turn-older.json"))
        await firstTurn(library, next, Self.switchedOlder)
        #expect(await eventually { library.current.chatID == Self.switchedOlderID })
        #expect(library.current.chatID != Self.emptyID)
        #expect(store.string(forKey: "openChat") == Self.switchedOlderID)
        #expect(library.currentChat?.turns == 1)
        #expect(library.isOpen(try #require(library.chats.first { $0.id == Self.switchedOlderID })))
    }

    @Test("before M28: Rename and Delete from the window name the conversation shown, and a relaunch reopens it")
    func olderBinaryActsOnTheRightChat() async throws {
        let runner = ScriptedRunner()
        let store = MemoryStore()
        let starter = ScriptedStarter(sessions: (0..<5).map { ScriptedSession("child \($0 + 1)") })
        let library = await unsentOpen(runner, starter, store, saying: Self.unsentOlder[0])
        runner.reading(["chats", "list"], printed("chats-list-unsent-older.json"))
        await library.newSession()
        #expect(await eventually { starter.openedCount == 2 })
        starter.handed[1].emit(Self.switchedOlder[0])
        #expect(await eventually { library.current.transcript.status == .ready })
        runner.reading(["chats", "list"], printed("chats-list-switched-turn-older.json"))
        await firstTurn(library, starter.handed[1], Self.switchedOlder)
        #expect(await eventually { library.current.chatID == Self.switchedOlderID })

        // A relaunch -- a new library over the same preferences -- reopens it.
        let relaunched = ScriptedStarter(sessions: [ScriptedSession()])
        let again = self.library(runner, relaunched, store)
        await again.start(inView: true)
        #expect(await eventually { relaunched.openedCount == 1 })
        #expect(relaunched.launches.map(\.resume) == [Self.switchedOlderID])

        library.current.onAction?(.rename)
        let renaming = try #require(library.renaming)
        #expect(renaming.id == Self.switchedOlderID)
        await library.rename(renaming, to: "Port Notes")
        #expect(runner.calls.contains("run chats title \(Self.switchedOlderID) -- Port Notes"))
        #expect(!runner.calls.contains { $0.hasPrefix("run chats title \(Self.emptyID)") })
        #expect(await eventually { starter.openedCount == 3 }, "this binary would put its own title back: paused, renamed, resumed")
        #expect(starter.launches[2].resume == Self.switchedOlderID)

        library.current.onAction?(.delete)
        let asked = try #require(library.pending)
        #expect(asked == .delete(try #require(library.chats.first { $0.id == Self.switchedOlderID })))
        await library.confirm()
        #expect(runner.calls.contains("run chats delete \(Self.switchedOlderID)"))
        #expect(!runner.calls.contains("run chats delete \(Self.emptyID)"))
    }

    @Test("before M28: an empty chat appearing while a session runs is passed over -- a chat with no turns can't be one that has run a turn")
    func emptyChatPassedOver() async {
        let runner = ScriptedRunner()
        let store = MemoryStore()
        let starter = ScriptedStarter(sessions: [ScriptedSession()])
        let library = await unsentOpen(runner, starter, store, saying: Self.switchedOlder[0])
        // Another apogee leaves an empty chat while this session runs.
        runner.reading(["chats", "list"], printed("chats-list-unsent-older.json"))
        await library.refresh()
        #expect(library.current.chatID == nil)
        runner.reading(["chats", "list"], printed("chats-list-switched-turn-older.json"))
        await firstTurn(library, starter.handed[0], Self.switchedOlder)
        #expect(await eventually { library.current.chatID == Self.switchedOlderID }, "its own, the empty one beside it passed over")
    }

    @Test("before M28: a chat the session left saved as it closed -- a turn and all -- is never the next one's: the list is read afresh after the close")
    func leftSessionsChatNeverTaken() async {
        let runner = ScriptedRunner()
        let starter = ScriptedStarter(sessions: [ScriptedSession(), ScriptedSession()])
        let library = await unsentOpen(runner, starter, MemoryStore(), saying: Self.switchedOlder[0])
        await firstTurn(library, starter.handed[0], Self.switchedOlder)
        #expect(library.chats.isEmpty, "its save not listed yet when the list was read after its turn")
        // ⌘N at once; the session left is listed once its child has gone.
        runner.reading(["chats", "list"], #"{"data":[{"backend":"claude","id":"20261026-172800-d639","model":"","name":"20261026-172800-d639","turns":1,"updated":"2026-10-26T17:28:00Z"}],"object":"list"}"#)
        await library.newSession()
        #expect(await eventually { starter.openedCount == 2 })
        starter.handed[1].emit(Self.unsentOlder[0])
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.refresh()
        #expect(library.current.chatID == nil, "the chat the session before it left is not this one")
    }

    @Test("before 52d's --private: a private session closed unsent is still deleted -- its own chat, saved empty as it closed")
    func unsentPrivateFallbackDeleted() async {
        let ledger = Ledger()
        let runner = ScriptedRunner(ledger: ledger)
        let starter = ScriptedStarter(sessions: [ScriptedSession(), ScriptedSession("private", ledger: ledger), ScriptedSession()])
        let library = await unsentOpen(runner, starter, MemoryStore(), saying: Lifecycle.older)
        #expect(!library.privateIsNothingWritten)
        await library.newPrivateSession()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.last == ChatLaunch(noRecall: true))
        starter.handed[1].emit(Lifecycle.older)
        #expect(await eventually { library.current.transcript.status == .ready })
        // Its input closed, this binary saves the private chat, empty.
        runner.reading(["chats", "list"], #"{"object":"list","data":[{"id":"p0","name":"p0","turns":0,"updated":"2026-10-20T17:00:00Z"}]}"#)
        await library.newSession()
        #expect(ledger.all == ["private: stop", "private: ended", "run chats delete p0"])
    }

    // MARK: - Since M28: the chat the session names

    @Test("since M28: a session that names its chat is identified by it at once -- never by what appears in the list")
    func namedAtOnce() async throws {
        let runner = ScriptedRunner()
        let store = MemoryStore()
        let starter = ScriptedStarter(sessions: (0..<4).map { ScriptedSession("child \($0 + 1)") })
        let library = await unsentOpen(runner, starter, store, saying: Self.unsent[0])
        #expect(library.current.chatID == Self.unsentID)
        // A store an older binary left an empty chat in, before the upgrade.
        runner.reading(["chats", "list"], printed("chats-list-unsent-older.json"))
        await library.newSession()
        #expect(await eventually { starter.openedCount == 2 })
        starter.handed[1].emit(Self.switched[0])
        #expect(await eventually { library.current.chatID == Self.switchedID }, "the chat its session event names, before any turn")
        #expect(store.string(forKey: "openChat") == Self.switchedID)
        await library.refresh()
        #expect(library.current.chatID == Self.switchedID, "never the empty chat the list holds")
        runner.reading(["chats", "list"], printed("chats-list-switched-turn.json"))
        await firstTurn(library, starter.handed[1], Self.switched)
        #expect(await eventually { library.currentChat?.id == Self.switchedID })

        library.current.onAction?(.rename)
        let renaming = try #require(library.renaming)
        #expect(renaming.id == Self.switchedID)
        await library.rename(renaming, to: "Port Notes")
        #expect(runner.calls.contains("run chats title \(Self.switchedID) -- Port Notes"))
        #expect(starter.openedCount == 2, "a binary that keeps outside renames: renamed in place")

        let relaunched = ScriptedStarter(sessions: [ScriptedSession()])
        let again = self.library(runner, relaunched, store)
        await again.start(inView: true)
        #expect(await eventually { relaunched.openedCount == 1 })
        #expect(relaunched.launches.map(\.resume) == [Self.switchedID], "a relaunch reopens it")

        library.current.onAction?(.delete)
        #expect(library.pending == .delete(try #require(library.chats.first { $0.id == Self.switchedID })))
        await library.confirm()
        #expect(runner.calls.contains("run chats delete \(Self.switchedID)"))
        #expect(!runner.calls.contains { $0.hasPrefix("run chats delete") && !$0.hasSuffix(Self.switchedID) })
    }

    @Test("the inspector on the identified chat: its Model the one that answered, its Backend the chat's own row, read after each turn")
    func inspectorOnTheIdentifiedChat() async throws {
        let runner = ScriptedRunner()
        let starter = ScriptedStarter(sessions: [ScriptedSession()])
        let library = await unsentOpen(runner, starter, MemoryStore(), saying: Self.switched[0])
        let child = starter.handed[0]
        runner.reading(["chats", "list"], printed("chats-list-switched-turn.json"))
        await firstTurn(library, child, Self.switched)
        #expect(await eventually { library.inspector.modelFacts == [Field("Model", "claude"), Field("Backend", "claude")] })
        await library.current.apply(RunSettings(model: "Gemma4-26B-Instruct"))
        let notice = Self.switched.firstIndex { Self.type($0) == "notice" }!
        child.emit(Self.switched[notice])
        #expect(await eventually { library.current.transcript.items.last?.content == .notice("switched to Gemma4-26B-Instruct -- from the next turn") })
        #expect(library.inspector.modelFacts.first == Field("Model", "claude"), "nothing has answered on it yet")
        runner.reading(["chats", "list"], printed("chats-list-switched.json"))
        library.current.draft = Self.turns[1]
        library.current.submit()
        Self.switched[(notice + 1)...].forEach(child.emit)
        #expect(await eventually { library.currentChat?.turns == 2 })
        #expect(library.inspector.modelFacts == [Field("Model", "Gemma4-26B-Instruct"), Field("Backend", "Gemma4-26B-Instruct")])
        #expect(library.inspector.session == [
            Field("Name", "Dev Server Port"), Field("Turns", "2"),
            Field("Updated", try #require(library.currentChat?.updated).formatted(date: .abbreviated, time: .shortened)),
            Field("Id", Self.switchedID),
        ])
    }

    @Test("a session named but not yet written is never resumed by that id: left unsent and paused, it carries on afresh, named anew")
    func namedUnsentStartsAfresh() async {
        let runner = ScriptedRunner()
        let store = MemoryStore()
        let starter = ScriptedStarter(sessions: [ScriptedSession(), ScriptedSession()])
        let library = await unsentOpen(runner, starter, store, saying: Self.unsent[0])
        await library.leftView()
        #expect(library.current.isPaused)
        await library.cameIntoView()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.map(\.resume) == [nil, nil], "its chat was never written: --resume would be refused")
        starter.handed[1].emit(Self.switched[0])
        #expect(await eventually { library.current.chatID == Self.switchedID }, "the new child's chat")
        #expect(store.string(forKey: "openChat") == Self.switchedID)

        let relaunched = ScriptedStarter(sessions: [ScriptedSession()])
        let again = self.library(runner, relaunched, store)
        await again.start(inView: true)
        #expect(await eventually { relaunched.openedCount == 1 })
        #expect(relaunched.launches.map(\.resume) == [nil], "not listed, so not reopened: a new session")
    }

    @Test("a private session names no chat: nothing written, never guessed, never reopened")
    func privateNamesNone() async {
        let runner = ScriptedRunner()
        let store = MemoryStore()
        let starter = ScriptedStarter(sessions: [ScriptedSession(), ScriptedSession()])
        let library = await unsentOpen(runner, starter, store, saying: Self.unsent[0])
        #expect(library.privateIsNothingWritten)
        await library.newPrivateSession()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.last == ChatLaunch(privately: true))
        starter.handed[1].emit(Self.stream("private-unsent")[0])
        #expect(await eventually { library.current.transcript.isPrivate })
        #expect(library.current.chatID == nil && library.current.privateMode == .nothingWritten)
        runner.reading(["chats", "list"], printed("chats-list-switched-turn.json"))
        await library.refresh()
        #expect(library.current.chatID == nil, "a chat appearing is someone else's")
        #expect(store.string(forKey: "openChat") == nil)
    }
}
