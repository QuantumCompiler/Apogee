import BusinessReads
import BusinessSession
import DataPreferences
import DataProtocol
import DataDiscovery
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationSessions

/// A vocabulary recorded from a binary's `__machine-schema`, the home
/// `/Users/someone` (tests/data/protocol/Fixtures): the branch's (2026-10-25),
/// the installed one's (2026-10-24), and one hand-made to the CLI source's
/// shape before its track 52 (v0.1.7's `protocol.cpp`, 2026-10-13).
func schema(_ name: String) -> String {
    let tests = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
    return (try? String(contentsOf: tests.appendingPathComponent("data/protocol/Fixtures/\(name).json"), encoding: .utf8)) ?? ""
}

/// A value a test moves while the code under test reads it.
final class Held<Value: Sendable>: @unchecked Sendable {
    private let lock = NSLock()
    private var stored: Value
    init(_ value: Value) { stored = value }
    var value: Value {
        get { lock.withLock { stored } }
        set { lock.withLock { stored = newValue } }
    }
}

/// GUI 4k, the seventh gap: capabilities are known from launch. The library
/// asks the binary's vocabulary once it has found it -- `__machine-schema`,
/// which answers with no config and no backend -- before any session starts,
/// so a place shown first never runs on a fallback against a capable binary.
@Suite("Capabilities from launch (GUI 4k)", .timeLimit(.minutes(1)))
@MainActor
struct CapabilitiesTests {
    func library(_ runner: ScriptedRunner, _ starter: ScriptedStarter) -> SessionLibraryViewModel {
        SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: MemoryStore()))
    }

    func runner(schema text: String) -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], #"{"object":"list","data":[]}"#)
        runner.commanding(["__machine-schema"], .done(stdout: text))
        return runner
    }

    @Test("launched out of view on the branch's binary: every track-52 flag is known before any session starts")
    func knownBeforeAnySession() async {
        let runner = runner(schema: schema("machine-schema"))
        let starter = ScriptedStarter(sessions: [ScriptedSession()])
        let library = library(runner, starter)
        await library.start(inView: false)
        #expect(starter.openedCount == 0, "no session: another place is in view")
        #expect(runner.calls.contains("run __machine-schema"))
        #expect(library.streamsJobs, "the build writes `job`: pulls stream")
        #expect(library.hasTrack52, "2026-10-25: compose plans alone, config set, recall forget")
        #expect(library.privateIsNothingWritten, "the build reads `set`: a private session writes nothing")
        #expect(library.keepsOutsideRenames)
        #expect(library.capabilitiesKnown)
    }

    @Test("an older vocabulary says what it has: 2026-10-24 streams and has track 52 but puts its own title back; before 52, none of it")
    func olderShapes() async {
        let older = library(runner(schema: schema("machine-schema-older")), ScriptedStarter(sessions: []))
        await older.start(inView: false)
        #expect(older.streamsJobs && older.hasTrack52 && older.privateIsNothingWritten)
        #expect(!older.keepsOutsideRenames)
        let pre52 = library(runner(schema: schema("machine-schema-pre52-handmade")), ScriptedStarter(sessions: []))
        await pre52.start(inView: false)
        #expect(!pre52.streamsJobs && !pre52.hasTrack52 && !pre52.privateIsNothingWritten && !pre52.keepsOutsideRenames)
        #expect(pre52.capabilitiesKnown, "asked, and it said: the fallback is now the truth")
    }

    @Test("a binary that doesn't answer leaves the flags unknown -- the fallback, never asserted -- and a session's word refreshes them")
    func unknownThenRefreshed() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], #"{"object":"list","data":[]}"#)
        runner.commanding(["__machine-schema"], .failed(exit: ChildExit(reason: .exited, status: 109), stderrTail: BoundedTail()))
        let child = ScriptedSession()
        let library = library(runner, ScriptedStarter(sessions: [child]))
        await library.start(inView: true)
        #expect(!library.capabilitiesKnown && !library.streamsJobs)
        child.emit(#"{"type":"session","protocol_version":1,"model":"actor","next_turn":1,"capabilities":{"events":["session","job"],"accepts":["user","set"],"tools":true,"ask":true,"schema":"2026-10-25"}}"#)
        #expect(await eventually { library.streamsJobs })
        #expect(library.capabilitiesKnown && library.hasTrack52 && library.privateIsNothingWritten)
    }

    @Test("asked again only when the binary's file changed: another build installed where it stood says what it can do")
    func binaryChanged() async {
        let runner = runner(schema: schema("machine-schema-older"))
        let library = library(runner, ScriptedStarter(sessions: []))
        let file = Held(BinaryFingerprint(fileNumber: 7, size: 100, modified: Date(timeIntervalSince1970: 1)))
        library.fingerprint = { _ in file.value }
        await library.start(inView: false)
        #expect(!library.keepsOutsideRenames, "the installed build, 2026-10-24")
        await library.checkBinary()
        #expect(runner.calls.filter { $0 == "run __machine-schema" }.count == 1, "the same file: nothing run")
        file.value = BinaryFingerprint(fileNumber: 8, size: 120, modified: Date(timeIntervalSince1970: 2))
        runner.commanding(["__machine-schema"], .done(stdout: schema("machine-schema")))
        await library.checkBinary()
        #expect(runner.calls.filter { $0 == "run __machine-schema" }.count == 2)
        #expect(library.keepsOutsideRenames, "the branch's build, 2026-10-25")
        file.value = BinaryFingerprint(fileNumber: 9, size: 1, modified: Date(timeIntervalSince1970: 3))
        runner.commanding(["__machine-schema"], .failed(exit: ChildExit(reason: .exited, status: 109), stderrTail: BoundedTail()))
        await library.checkBinary()
        #expect(!library.capabilitiesKnown && !library.hasTrack52, "a build that doesn't answer: nothing kept of the one before")
    }
}
