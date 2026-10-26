import BusinessReads
import BusinessSession
import DataPreferences
import DataProtocol
import Foundation
import PresentationSession
import Synchronization
import Testing

@testable import PresentationSessions

/// GUI M10, at the context meter: its window is the window of the model the
/// CLI says answered. Neither `result` nor its `usage` carries a window, so
/// it is read as the opening model's is -- `models info <model>` -- once a
/// `result` names another model; between the switch and the answer the meter
/// keeps the last model's, as the header does. Over the branch binary's
/// recording (`switched-to-local.jsonl`) and its `models info` of the same
/// two names in a sibling sandbox: `claude` an API entry, which states no
/// window, and `Gemma4-26B-Instruct` a local GGUF entry, 8192 tokens.
@Suite("The meter follows the model that answered (M10)", .timeLimit(.minutes(1)))
@MainActor
struct SessionMeterTests {
    static func stream(_ name: String) -> [String] {
        let tests = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let text = (try? String(contentsOf: tests.appendingPathComponent("data/protocol/Fixtures/\(name).jsonl"), encoding: .utf8)) ?? ""
        return text.split(separator: "\n").map(String.init)
    }

    static let switched = stream("switched-to-local")
    static let turns = ["Which port does the dev server use?", "And on the local model?"]
    static let local = "Gemma4-26B-Instruct"

    static func type(_ line: String) -> String? { JSONValue.parse(line)?["type"]?.stringValue }

    func reads(_ runner: ScriptedRunner, of model: String) -> Int {
        runner.calls.filter { $0 == "read models info \(model)" }.count
    }

    @Test("the recorded switch: claude's window unknown, kept through the notice, then the local model's 8192 once it answered")
    func meterFollowsTheAnswer() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], printed("chats-list-unsent.json"))
        runner.reading(["models", "info", "claude"], printed("models-info-switched-claude.json"))
        runner.reading(["models", "info", Self.local], printed("models-info-switched-local.json"))
        let child = ScriptedSession()
        let library = SessionLibraryViewModel(
            starter: ScriptedStarter(sessions: [child]), runner: runner, preferences: Preferences(store: MemoryStore()))
        await library.start(inView: true)
        child.emit(Self.switched[0])
        #expect(await eventually { library.current.transcript.status == .ready && self.reads(runner, of: "claude") == 1 })
        #expect(library.current.contextWindow == nil, "an API entry states no window: unknown, never guessed")
        library.current.draft = Self.turns[0]
        library.current.submit()
        let notice = Self.switched.firstIndex { Self.type($0) == "notice" }!
        Self.switched[1..<notice].forEach(child.emit)
        #expect(await eventually { library.current.transcript.acceptsInput })
        await library.current.apply(RunSettings(model: Self.local))
        child.emit(Self.switched[notice])
        #expect(await eventually { library.current.transcript.items.last?.content == .notice("switched to Gemma4-26B-Instruct -- from the next turn") })
        try? await Task.sleep(for: .milliseconds(50))
        #expect(reads(runner, of: Self.local) == 0, "said from the next turn: nothing has answered on it yet")
        #expect(reads(runner, of: "claude") == 1, "the same model answering again reads nothing again")
        library.current.draft = Self.turns[1]
        library.current.submit()
        Self.switched[(notice + 1)...].forEach(child.emit)
        #expect(await eventually { library.current.contextWindow == 8192 })
        #expect(reads(runner, of: Self.local) == 1)
        #expect(library.current.meter.window == 8192)
    }

    @Test("a window read for the model that answered that fails leaves the meter unknown -- never the last model's")
    func failedReadIsUnknown() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], printed("chats-list-unsent.json"))
        runner.reading(["models", "info", Self.local], printed("models-info-switched-local.json"))
        let child = ScriptedSession()
        let library = SessionLibraryViewModel(
            starter: ScriptedStarter(sessions: [child]), runner: runner, preferences: Preferences(store: MemoryStore()))
        await library.start(inView: true)
        // A session opened on the local model, then answered by claude --
        // whose read is refused here.
        child.emit(#"{"type":"session","protocol_version":1,"model":"Gemma4-26B-Instruct","next_turn":1}"#)
        #expect(await eventually { library.current.contextWindow == 8192 })
        library.current.draft = "hi"
        library.current.submit()
        child.emit(#"{"type":"result","text":"hello","model":"claude","finish_reason":"stop","turn":1}"#)
        #expect(await eventually { self.reads(runner, of: "claude") == 1 })
        #expect(await eventually { library.current.contextWindow == nil })
    }

    @Test("the opening model's window read, landing after the local model answered, is let go: the meter is the answering model's")
    func lateReadLetGo() async {
        let base = ScriptedRunner()
        base.reading(["chats", "list"], printed("chats-list-unsent.json"))
        base.reading(["models", "info", "claude"], printed("models-info-switched-claude.json"))
        base.reading(["models", "info", Self.local], printed("models-info-switched-local.json"))
        let runner = HoldingRunner(base: base, holding: ["models", "info", "claude"])
        let child = ScriptedSession()
        let library = SessionLibraryViewModel(
            starter: ScriptedStarter(sessions: [child]), runner: runner, preferences: Preferences(store: MemoryStore()))
        await library.start(inView: true)
        child.emit(Self.switched[0])
        #expect(await eventually { library.current.transcript.status == .ready && runner.asked == 1 })
        library.current.draft = Self.turns[0]
        library.current.submit()
        let notice = Self.switched.firstIndex { Self.type($0) == "notice" }!
        Self.switched[1..<notice].forEach(child.emit)
        #expect(await eventually { library.current.transcript.acceptsInput })
        await library.current.apply(RunSettings(model: Self.local))
        child.emit(Self.switched[notice])
        library.current.draft = Self.turns[1]
        library.current.submit()
        Self.switched[(notice + 1)...].forEach(child.emit)
        #expect(await eventually { library.current.contextWindow == 8192 })
        runner.release()
        try? await Task.sleep(for: .milliseconds(100))
        #expect(library.current.contextWindow == 8192, "claude's window, read late, is not the answering model's")
    }
}

/// A scripted runner whose one read, `holding`, answers only once released --
/// a slow `models info` -- every other call answered at once.
final class HoldingRunner: CommandRunning, @unchecked Sendable {
    private let base: ScriptedRunner
    private let holding: [String]
    private let gate = Mutex<(open: Bool, asked: Int, waiting: [CheckedContinuation<Void, Never>])>((false, 0, []))

    /// How many times the held read was asked for.
    var asked: Int { gate.withLock { $0.asked } }

    init(base: ScriptedRunner, holding: [String]) {
        self.base = base
        self.holding = holding
    }

    func release() {
        let waiting = gate.withLock { gate in
            gate.open = true
            defer { gate.waiting = [] }
            return gate.waiting
        }
        waiting.forEach { $0.resume() }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        if arguments == holding {
            await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
                let now = gate.withLock { gate in
                    gate.asked += 1
                    if gate.open { return true }
                    gate.waiting.append(continuation)
                    return false
                }
                if now { continuation.resume() }
            }
        }
        return await base.read(binary: binary, arguments)
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        await base.command(binary: binary, arguments)
    }
}
