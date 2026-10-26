import BusinessReads
import BusinessSession
import DataPreferences
import DataProtocol
import Foundation
import PresentationSession
import Testing

@testable import PresentationSessions

/// 4b: a rename of the open session survives its next turn. A binary with
/// the CLI's 55b keeps an outside rename (its schema is 2026-10-25 or later),
/// so the rename is just `chats title`, read back; an older one puts its own
/// title back on its next save, so the session is paused, renamed and
/// resumed. Over the binaries' own recordings: each one's `session` event,
/// and the list each read back after a `chats title` made while it ran and
/// a turn after it.
@Suite("Renaming the open session", .timeLimit(.minutes(1)))
@MainActor
struct SessionRenameTests {
    static func fixture(_ path: String) -> String {
        let tests = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        return (try? String(contentsOf: tests.appendingPathComponent(path), encoding: .utf8)) ?? ""
    }

    static func stream(_ name: String) -> [String] {
        fixture("data/protocol/Fixtures/\(name).jsonl").split(separator: "\n").map(String.init)
    }

    /// The branch binary (55b, schema 2026-10-25) and the installed one
    /// before it (schema 2026-10-24): each run renamed from outside after its
    /// first turn, then a second turn, then the list read.
    static let newer = stream("rename-outside")
    static let older = stream("rename-outside-older")
    static let listedNewer = fixture("business/reads/Fixtures/chats-list-renamed.json")
    static let listedOlder = fixture("business/reads/Fixtures/chats-list-renamed-older.json")

    static func chatID(_ lines: [String]) -> String { JSONValue.parse(lines[0])?["chat_id"]?.stringValue ?? "" }

    /// The library over one open saved session that has said `session`.
    func opened(_ lines: [String], listed: String, ledger: Ledger, children: Int = 2) async -> (SessionLibraryViewModel, ScriptedRunner, ScriptedStarter) {
        let runner = ScriptedRunner(ledger: ledger)
        runner.reading(["chats", "list"], listed)
        let sessions = (0..<children).map { ScriptedSession("child \($0 + 1)", ledger: ledger) }
        let starter = ScriptedStarter(sessions: sessions)
        let library = SessionLibraryViewModel(
            starter: starter, runner: runner, preferences: Preferences(store: MemoryStore(["openChat": Self.chatID(lines)])))
        await library.start(inView: true)
        sessions[0].emit(lines[0])
        _ = await eventually { library.current.transcript.status == .ready }
        return (library, runner, starter)
    }

    @Test("the recordings: the branch binary kept the outside rename through its next turn; the one before put its own title back")
    func whatEachBinaryKeeps() {
        let kept = ReadDocument(text: Self.listedNewer)?.entries?.first
        let undone = ReadDocument(text: Self.listedOlder)?.entries?.first
        #expect(kept?["name"]?.stringValue == "Renamed From Outside")
        #expect(kept?["turns"]?.intValue == 2, "after the turn that followed the rename")
        #expect(undone?["name"]?.stringValue == "Dev Server Port", "its own title, over the rename")
        #expect(JSONValue.parse(Self.newer[0])?["capabilities"]?["schema"]?.stringValue == "2026-10-25")
        #expect(JSONValue.parse(Self.older[0])?["capabilities"]?["schema"]?.stringValue == "2026-10-24")
    }

    @Test("a binary that keeps outside renames: the open session's rename is chats title alone, read back, its child left running")
    func renamedInPlace() async {
        let ledger = Ledger()
        let (library, runner, starter) = await opened(Self.newer, listed: Self.listedNewer, ledger: ledger)
        let id = Self.chatID(Self.newer)
        await library.rename(library.chats[0], to: "Port Notes")
        #expect(ledger.all == ["run chats title \(id) -- Port Notes"])
        #expect(runner.calls.drop { !$0.hasPrefix("run chats title") }.contains("read chats list"), "read back")
        #expect(starter.openedCount == 1)
        #expect(starter.live == 1, "the session runs on")
    }

    @Test("an older binary: the open session is paused, renamed, then resumed -- its own next save can't put its title back")
    func pausedRenamedResumed() async {
        let ledger = Ledger()
        let (library, _, starter) = await opened(Self.older, listed: Self.listedOlder, ledger: ledger)
        let id = Self.chatID(Self.older)
        await library.rename(library.chats[0], to: "Renamed From Outside")
        #expect(Array(ledger.all.prefix(3)) == ["child 1: stop", "child 1: ended", "run chats title \(id) -- Renamed From Outside"])
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.map(\.resume) == [id, id], "resumed by its id")
        #expect(starter.live == 1)
    }

    @Test("an older binary mid-turn: the rename waits for the turn to end, then pauses, renames and resumes")
    func renameWaitsForTheTurn() async {
        let ledger = Ledger()
        let (library, _, starter) = await opened(Self.older, listed: Self.listedOlder, ledger: ledger)
        let id = Self.chatID(Self.older)
        library.current.draft = "Anything else?"
        library.current.submit()
        await library.rename(library.chats[0], to: "Renamed From Outside")
        #expect(ledger.all.isEmpty, "the turn runs on, and nothing is renamed yet")
        starter.handed[0].emit(Self.older.first { JSONValue.parse($0)?["type"]?.stringValue == "result" }!)
        #expect(await eventually { ledger.all.contains("run chats title \(id) -- Renamed From Outside") })
        #expect(Array(ledger.all.prefix(3)) == ["child 1: stop", "child 1: ended", "run chats title \(id) -- Renamed From Outside"])
        #expect(await eventually { starter.openedCount == 2 })
    }

    @Test("an older binary whose child ends mid-turn, a rename waiting: it is made then, by chats title alone")
    func waitingRenameWhenTheChildEnds() async {
        let ledger = Ledger()
        let (library, _, starter) = await opened(Self.older, listed: Self.listedOlder, ledger: ledger)
        let id = Self.chatID(Self.older)
        library.current.draft = "Anything else?"
        library.current.submit()
        await library.rename(library.chats[0], to: "Renamed From Outside")
        #expect(ledger.all.isEmpty)
        starter.handed[0].end(.failed)
        #expect(await eventually { ledger.all == ["run chats title \(id) -- Renamed From Outside"] })
        #expect(starter.openedCount == 1, "nothing to pause or resume: its child is gone")
    }
}
