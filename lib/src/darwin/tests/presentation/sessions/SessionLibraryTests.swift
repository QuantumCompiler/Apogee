import BusinessReads
import BusinessSession
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import PresentationSession
import Testing

@testable import PresentationSessions

@Suite("SessionLibrary", .timeLimit(.minutes(1)))
@MainActor
struct SessionLibraryTests {
    static let session = #"{"type":"session","protocol_version":1,"model":"actor","next_turn":1}"#
    static let twoChats = #"{"object":"list","data":[{"id":"c2","name":"Plans","turns":3,"updated":"2026-10-20T15:00:00Z"},{"id":"c1","name":"Recipes","turns":1,"updated":"2026-10-12T09:00:00Z"}]}"#
    /// 2026-10-20 18:00 UTC.
    static let now = Date(timeIntervalSince1970: 1_792_519_200)

    func library(_ runner: ScriptedRunner, _ starter: ScriptedStarter, _ store: MemoryStore = MemoryStore()) -> SessionLibraryViewModel {
        SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: store), now: { Self.now })
    }

    @Test("at launch with Sessions in view: the list read, and the session left open last time resumed")
    func launchResumesTheLastOpen() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.twoChats)
        let child = ScriptedSession()
        let starter = ScriptedStarter(sessions: [child])
        let library = library(runner, starter, MemoryStore(["openChat": "c1"]))
        await library.start(inView: true)
        #expect(library.listing == .read)
        #expect(library.chats.map(\.id) == ["c2", "c1"])
        #expect(await eventually { starter.openedCount == 1 })
        #expect(starter.launches.map(\.resume) == ["c1"])
        #expect(library.current.chatID == "c1")
        #expect(library.isOpen(library.chats[1]))
    }

    @Test("at launch with another place in view, no child starts until Sessions comes into view")
    func launchOutOfView() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.twoChats)
        let starter = ScriptedStarter(sessions: [ScriptedSession()])
        let library = library(runner, starter)
        await library.start(inView: false)
        try? await Task.sleep(for: .milliseconds(50))
        #expect(starter.openedCount == 0)
        await library.cameIntoView()
        #expect(await eventually { starter.openedCount == 1 })
        #expect(starter.launches.map(\.resume) == [nil], "nothing was open: a new session")
    }

    @Test("exactly one child: opening another session ends the open one's first")
    func oneChildAtATime() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.twoChats)
        let first = ScriptedSession()
        let second = ScriptedSession()
        let third = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second, third])
        let store = MemoryStore()
        let library = library(runner, starter, store)
        await library.start(inView: true)
        first.emit(Self.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.open(.chat("c2"))
        #expect(first.calls == ["stop"], "the open session's child ended before the next opened")
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.map(\.resume) == [nil, "c2"])
        #expect(store.string(forKey: "openChat") == "c2")
        await library.open(.chat("c2"))
        #expect(starter.openedCount == 2, "the open one again opens nothing")
        await library.newSession()
        #expect(second.calls == ["stop"])
        #expect(await eventually { starter.openedCount == 3 })
        #expect(starter.launches.last?.resume == nil)
        #expect(store.string(forKey: "openChat") == nil)
    }

    @Test("opening another while a turn runs asks first; yes stops the turn, no leaves it")
    func midTurnAsks() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.twoChats)
        let first = ScriptedSession()
        let second = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second])
        let library = library(runner, starter)
        await library.start(inView: true)
        first.emit(Self.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        library.current.draft = "a long one"
        library.current.submit()
        await library.open(.chat("c1"))
        #expect(library.pending == .stopTurn(for: .chat("c1")))
        library.dismissPending()
        #expect(library.pending == nil)
        #expect(starter.openedCount == 1)
        await library.open(.chat("c1"))
        await library.confirm()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(first.calls.contains("stop"))
    }

    @Test("leaving Sessions pauses an idle session; coming back resumes it, and reads the list again")
    func leaveAndReturnIdle() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.twoChats)
        let first = ScriptedSession()
        let second = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second])
        let library = library(runner, starter, MemoryStore(["openChat": "c2"]))
        await library.start(inView: true)
        first.emit(Self.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.leftView()
        #expect(first.calls == ["stop"])
        #expect(library.current.isPaused)
        let readsBefore = runner.calls.filter { $0 == "read chats list" }.count
        await library.cameIntoView()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.map(\.resume) == ["c2", "c2"])
        #expect(runner.calls.filter { $0 == "read chats list" }.count == readsBefore + 1)
    }

    @Test("leaving mid-turn keeps the child until the turn ends, then pauses it")
    func leaveMidTurn() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.twoChats)
        let child = ScriptedSession()
        let starter = ScriptedStarter(sessions: [child])
        let library = library(runner, starter, MemoryStore(["openChat": "c2"]))
        await library.start(inView: true)
        child.emit(Self.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        library.current.draft = "go on"
        library.current.submit()
        await library.leftView()
        #expect(!child.calls.contains("stop"), "mid-turn: the turn finishes")
        child.emit(#"{"type":"result","text":"done","turn":1}"#)
        #expect(await eventually { child.calls.contains("stop") })
        #expect(await eventually { library.current.isPaused })
    }

    @Test("a new session is identified by the one id that appears in the list after its first turn")
    func newSessionIdentified() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.twoChats)
        let child = ScriptedSession()
        let store = MemoryStore()
        let library = library(runner, ScriptedStarter(sessions: [child]), store)
        await library.start(inView: true)
        child.emit(Self.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        #expect(library.current.chatID == nil)
        runner.reading(["chats", "list"], #"{"object":"list","data":[{"id":"c3","name":"New one","turns":1,"updated":"2026-10-20T17:59:00Z"},{"id":"c2","name":"Plans","turns":3,"updated":"2026-10-20T15:00:00Z"},{"id":"c1","name":"Recipes","turns":1,"updated":"2026-10-12T09:00:00Z"}]}"#)
        library.current.draft = "hello"
        library.current.submit()
        child.emit(#"{"type":"result","text":"hi","turn":1,"usage":{"input_tokens":12,"output_tokens":3}}"#)
        #expect(await eventually { library.current.chatID == "c3" })
        #expect(store.string(forKey: "openChat") == "c3")
        #expect(library.currentChat?.name == "New one")
        #expect(library.inspector.turnFacts == [Field("In", "12"), Field("Out", "3")])
    }

    @Test("two ids appearing at once name none, rather than guess")
    func ambiguousNewSession() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.twoChats)
        let child = ScriptedSession()
        let library = library(runner, ScriptedStarter(sessions: [child]))
        await library.start(inView: true)
        child.emit(Self.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        runner.reading(["chats", "list"], #"{"object":"list","data":[{"id":"c4","name":"x","turns":1,"updated":"2026-10-20T17:59:00Z"},{"id":"c3","name":"y","turns":1,"updated":"2026-10-20T17:59:00Z"},{"id":"c2","name":"Plans","turns":3,"updated":"2026-10-20T15:00:00Z"}]}"#)
        await library.refresh()
        #expect(library.current.chatID == nil)
    }

    @Test("a rename is the command a person types, read back, and said")
    func rename() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.twoChats)
        var said: [String] = []
        let library = library(runner, ScriptedStarter(sessions: [ScriptedSession()]))
        library.say = { said.append($0) }
        await library.start(inView: false)
        await library.rename(library.chats[1], to: "  Weeknight recipes ")
        #expect(runner.calls.suffix(2) == ["run chats title c1 Weeknight recipes", "read chats list"])
        #expect(said == ["Renamed to Weeknight recipes"])
        await library.rename(library.chats[1], to: "   ")
        #expect(runner.calls.last == "read chats list", "an empty name runs nothing")
        runner.commanding(["chats", "title"], .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail("apogee chats: no chat named 'c1'")))
        await library.rename(library.chats[1], to: "Other")
        #expect(library.failure == "apogee chats: no chat named 'c1' (exited with status 1)")
        #expect(said.last == "Not renamed: apogee chats: no chat named 'c1' (exited with status 1)")
    }

    @Test("a delete asks, runs, reads back; deleting the open session ends it and opens a new one")
    func delete() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.twoChats)
        let first = ScriptedSession()
        let second = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second])
        let library = library(runner, starter, MemoryStore(["openChat": "c2"]))
        await library.start(inView: true)
        first.emit(Self.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        library.askToDelete(library.chats[1])
        #expect(library.pending == .delete(library.chats[1]))
        library.dismissPending()
        #expect(!runner.calls.contains { $0.hasPrefix("run") }, "nothing runs without the person's word")
        library.askToDelete(library.chats[0])
        runner.reading(["chats", "list"], #"{"object":"list","data":[{"id":"c1","name":"Recipes","turns":1,"updated":"2026-10-12T09:00:00Z"}]}"#)
        await library.confirm()
        #expect(first.calls == ["stop"])
        #expect(runner.calls.contains("run chats delete c2"))
        #expect(library.chats.map(\.id) == ["c1"])
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.last?.resume == nil)
    }

    @Test("the list groups by date, newest first, and filters by name in any case")
    func groupsAndSearch() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], #"{"object":"list","data":[{"id":"a","name":"Today's plan","turns":1,"updated":"2026-10-20T08:00:00Z"},{"id":"b","name":"Yesterday's","turns":1,"updated":"2026-10-19T12:00:00Z"},{"id":"c","name":"Last week","turns":1,"updated":"2026-10-14T12:00:00Z"},{"id":"d","name":"Long ago PLAN","turns":1,"updated":"2026-09-01T12:00:00Z"},{"id":"e","name":"Undated","turns":1}]}"#)
        let library = library(runner, ScriptedStarter(sessions: [ScriptedSession()]))
        await library.start(inView: false)
        var utc = Calendar(identifier: .gregorian)
        utc.timeZone = TimeZone(identifier: "UTC")!
        let groups = ChatGroup.grouping(library.chats, now: Self.now, calendar: utc)
        #expect(groups.map(\.title) == ["Today", "Yesterday", "Previous 7 days", "Earlier"])
        #expect(groups.map { $0.chats.map(\.id) } == [["a"], ["b"], ["c"], ["d", "e"]])
        var en = utc
        en.locale = Locale(identifier: "en_US")
        // (The formatter puts a narrow no-break space before AM.)
        let when = library.chats.map { library.when($0, calendar: en).replacingOccurrences(of: "\u{202F}", with: " ") }
        #expect(when == ["8:00 AM", "12:00 PM", "Wed", "Sep 1", ""])
        library.query = "plan"
        #expect(library.groups.flatMap(\.chats).map(\.id) == ["a", "d"])
        library.query = "nothing like it"
        #expect(library.emptyText == "No sessions match “nothing like it”.")
    }

    @Test("a row's glyph is neutral until the list names its backend and that backend's kind is known")
    func glyphFallback() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.twoChats)
        let library = library(runner, ScriptedStarter(sessions: [ScriptedSession()]))
        await library.start(inView: false)
        #expect(library.chats.allSatisfy { library.kind(of: $0) == nil })
        #expect(runner.calls.filter { $0 == "read models list" }.count == 1, "read once at launch, for the meter's windows and the settings' models")
        runner.reading(["chats", "list"], #"{"object":"list","data":[{"id":"c2","name":"Plans","turns":3,"updated":"2026-10-20T15:00:00Z","backend":"claude","model":""},{"id":"c1","name":"Recipes","turns":1,"updated":"2026-10-12T09:00:00Z","backend":"actor","model":""}]}"#)
        runner.reading(["models", "list"], #"{"object":"list","data":[{"backend":"claude","backend_type":"anthropic"},{"backend":"actor","backend_type":"mock"}]}"#)
        await library.refresh()
        #expect(library.kind(of: library.chats[0]) == .api)
        #expect(library.kind(of: library.chats[1]) == nil)
    }

    @Test("the inspector: the session's row, its model, the roles from models status")
    func inspector() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.twoChats)
        runner.reading(["models", "status"], #"{"fields":[{"field":"chat","value":"actor"},{"field":"vision","value":"(unset -- the chat's own backend)"}]}"#)
        let child = ScriptedSession()
        let store = MemoryStore(["openChat": "c2"])
        let library = library(runner, ScriptedStarter(sessions: [child]), store)
        await library.start(inView: true)
        child.emit(Self.session)
        #expect(await eventually { library.current.transcript.model == "actor" })
        await library.refreshRoles()
        let inspector = library.inspector
        #expect(inspector.session.prefix(2) == [Field("Name", "Plans"), Field("Turns", "3")])
        #expect(inspector.session.last == Field("Id", "c2"))
        #expect(inspector.modelFacts == [Field("Model", "actor")])
        #expect(inspector.turnFacts == [Field("Tokens", "not reported")])
        #expect(inspector.roles == [Field("chat", "actor"), Field("vision", "(unset -- the chat's own backend)")])
        library.inspectorOpen = true
        #expect(store.string(forKey: "inspectorOpen") == "true", "the inspector's state is remembered")
    }

    @Test("no binary: the list says so, and the session window shows how to install")
    func missingBinary() async {
        let runner = ScriptedRunner()
        let starter = ScriptedStarter(outcome: .notFound(skipped: []), sessions: [])
        let library = library(runner, starter)
        await library.start(inView: true)
        #expect(library.listing == .missing)
        #expect(library.emptyText == "Apogee isn't installed, so there are no sessions to list.")
        #expect(await eventually { if case .missing = library.current.connection { true } else { false } })
        #expect(runner.calls.isEmpty)
    }

    @Test("a failed read says the CLI's words")
    func readFailure() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], .failed(exit: ChildExit(reason: .exited, status: 2), stderrTail: tail("apogee: config is unreadable")))
        let library = library(runner, ScriptedStarter(sessions: [ScriptedSession()]))
        await library.start(inView: false)
        #expect(library.listing == .failed("apogee: config is unreadable (exited with status 2)"))
        #expect(library.emptyText == "The sessions could not be read: apogee: config is unreadable (exited with status 2)")
    }
}

func tail(_ text: String) -> BoundedTail {
    var tail = BoundedTail()
    tail.append(Data((text + "\n").utf8))
    return tail
}

@Suite("SessionLibraryPrivate", .timeLimit(.minutes(1)))
@MainActor
struct SessionLibraryPrivateTests {
    static let old = #"{"type":"session","protocol_version":1,"model":"local","next_turn":1,"capabilities":{"events":[],"accepts":["user","answer","attach","hello","cancel"],"tools":true,"ask":true,"schema":"2026-10-13"}}"#
    static let new = #"{"type":"session","protocol_version":1,"model":"local","next_turn":1,"capabilities":{"events":[],"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"tools":true,"ask":true,"schema":"2026-10-20"}}"#
    static let list = #"{"object":"list","data":[{"id":"c1","name":"Recipes","turns":1,"updated":"2026-10-12T09:00:00Z"}]}"#
    static let models = #"{"object":"list","data":[{"backend":"local","backend_type":"llamacpp","model":"qwen"},{"backend":"claude","backend_type":"anthropic"}]}"#

    func library(_ runner: ScriptedRunner, _ sessions: [ScriptedSession]) -> (SessionLibraryViewModel, ScriptedStarter) {
        let starter = ScriptedStarter(sessions: sessions)
        return (SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: MemoryStore())), starter)
    }

    @Test("on a build with --private, a private session is --private: nothing to delete, never listed")
    func privateWritesNothing() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.list)
        runner.reading(["models", "list"], Self.models)
        let first = ScriptedSession()
        let second = ScriptedSession()
        let third = ScriptedSession()
        runner.reading(["models", "info", "local"], #"{"name":"local","fields":[{"field":"backend","value":"local"},{"field":"window","value":"8192 tokens (the default; trained for 32768)"}]}"#)
        let (library, starter) = library(runner, [first, second, third])
        await library.start(inView: true)
        first.emit(Self.new)
        #expect(await eventually { library.privateIsNothingWritten })
        #expect(await eventually { library.current.contextWindow == 8192 }, "the session's model's window, from models info")
        #expect(library.current.modelChoices == ["local", "claude"])
        await library.newPrivateSession()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.last == ChatLaunch(privately: true))
        await library.newSession()
        #expect(!runner.calls.contains { $0.hasPrefix("run chats delete") })
    }

    @Test("on an older build a private session is --no-recall, kept out of the list, and deleted when it closes")
    func privateFallback() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.list)
        let first = ScriptedSession()
        let second = ScriptedSession()
        let third = ScriptedSession()
        let (library, starter) = library(runner, [first, second, third])
        await library.start(inView: true)
        first.emit(Self.old)
        _ = await eventually { library.current.transcript.status == .ready }
        #expect(!library.privateIsNothingWritten)
        await library.newPrivateSession()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.last == ChatLaunch(noRecall: true))
        second.emit(Self.old)
        _ = await eventually { library.current.transcript.status == .ready }
        runner.reading(["chats", "list"], #"{"object":"list","data":[{"id":"p9","name":"secret","turns":1,"updated":"2026-10-20T17:00:00Z"},{"id":"c1","name":"Recipes","turns":1,"updated":"2026-10-12T09:00:00Z"}]}"#)
        library.current.draft = "something private"
        library.current.submit()
        second.emit(#"{"type":"result","text":"ok","turn":1}"#)
        #expect(await eventually { library.current.chatID == "p9" })
        #expect(library.groups.flatMap(\.chats).map(\.id) == ["c1"], "the private one is never listed")
        await library.newSession()
        #expect(runner.calls.contains("run chats delete p9"), "deleted when it closed, as it promised")
    }

    @Test("a model's window: 52e's typed data first, else the window field's count; none is unknown")
    func windows() throws {
        let typed = try #require(ReadDocument(text: #"{"name":"q","fields":[{"field":"window","value":"4096 tokens (context_size)"}],"data":{"window":{"tokens":32768,"source":"default"}}}"#))
        #expect(SessionLibraryViewModel.window(in: typed) == 32768)
        let fields = try #require(ReadDocument(text: #"{"name":"q","fields":[{"field":"window","value":"8192 tokens (the default)"}]}"#))
        #expect(SessionLibraryViewModel.window(in: fields) == 8192)
        let api = try #require(ReadDocument(text: #"{"name":"claude","fields":[{"field":"backend","value":"claude"},{"field":"type","value":"anthropic"}]}"#))
        #expect(SessionLibraryViewModel.window(in: api) == nil)
        let unknown = try #require(ReadDocument(text: #"{"name":"m","fields":[{"field":"window","value":"window unknown -- config.json could not be read"}]}"#))
        #expect(SessionLibraryViewModel.window(in: unknown) == nil)
    }

    @Test("the window's acts reach the library: rename, delete, the inspector, a recalled chat")
    func actsFromTheWindow() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.list)
        let first = ScriptedSession()
        let second = ScriptedSession()
        let store = MemoryStore(["openChat": "c1"])
        let starter = ScriptedStarter(sessions: [first, second])
        let library = SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: store))
        await library.start(inView: true)
        first.emit(Self.new)
        _ = await eventually { library.current.transcript.status == .ready }
        library.current.onAction?(.rename)
        #expect(library.renaming?.id == "c1")
        library.current.onAction?(.delete)
        #expect(library.pending == .delete(library.chats[0]))
        library.dismissPending()
        library.current.onAction?(.toggleInspector)
        #expect(library.inspectorOpen)
        runner.reading(["chats", "list"], #"{"object":"list","data":[{"id":"c7","name":"Older","turns":1,"updated":"2026-10-01T09:00:00Z"},{"id":"c1","name":"Recipes","turns":1,"updated":"2026-10-12T09:00:00Z"}]}"#)
        library.current.onAction?(.openChat("c7"))
        #expect(await eventually { starter.launches.last?.resume == "c7" })
    }
}
