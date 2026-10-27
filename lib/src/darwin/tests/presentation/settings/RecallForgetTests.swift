import BusinessJobs
import BusinessReads
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization
import Testing

@testable import PresentationSettings

/// A recorded document of `tests/business/reads/Fixtures/`, as the binary
/// wrote it over a sandbox home, the home written `/Users/someone`.
func recordedText(_ name: String) -> String {
    let tests = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
    return (try? String(contentsOf: tests.appendingPathComponent("business/reads/Fixtures/\(name)"), encoding: .utf8)) ?? ""
}

/// The binary's refusal: exit 1, the recorded stderr its tail.
func refused(_ name: String) -> CommandOutcome {
    var tail = BoundedTail()
    tail.append(Data(recordedText(name).utf8))
    return .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail)
}

/// A runner answering each command by the first rule its arguments equal or
/// start with, every call recorded; reads by prefix.
final class RuleRunner: CommandRunning, @unchecked Sendable {
    private let exact = Mutex<[([String], CommandOutcome)]>([])
    private let prefixed = Mutex<[([String], CommandOutcome)]>([])
    private let reads = Mutex<[([String], String)]>([])
    private let readFailures = Mutex<[([String], ReadOutcome)]>([])
    private let log = Mutex<[String]>([])

    var calls: [String] { log.withLock { $0 } }

    func when(_ arguments: [String], _ outcome: CommandOutcome) { exact.withLock { $0.insert((arguments, outcome), at: 0) } }
    func whenStarting(_ prefix: [String], _ outcome: CommandOutcome) { prefixed.withLock { $0.insert((prefix, outcome), at: 0) } }
    func reading(_ prefix: [String], _ json: String) { reads.withLock { $0.insert((prefix, json), at: 0) } }
    /// Answers every later read starting with `prefix` with `outcome`: a refusal.
    func failingRead(_ prefix: [String], _ outcome: ReadOutcome) { readFailures.withLock { $0.insert((prefix, outcome), at: 0) } }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        log.withLock { $0.append("read " + arguments.joined(separator: " ")) }
        if let failure = readFailures.withLock({ rules in rules.first { arguments.starts(with: $0.0) }?.1 }) { return failure }
        return reads.withLock { rules in rules.first { arguments.starts(with: $0.0) } }.flatMap { ReadDocument(text: $0.1) }.map(ReadOutcome.document)
            ?? .couldNotStart("no scripted read")
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        log.withLock { $0.append("run " + arguments.joined(separator: " ")) }
        if let outcome = exact.withLock({ rules in rules.first { $0.0 == arguments }?.1 }) { return outcome }
        if let outcome = prefixed.withLock({ rules in rules.first { arguments.starts(with: $0.0) }?.1 }) { return outcome }
        if arguments.starts(with: ["config", "get"]) {
            return .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: BoundedTail())
        }
        return .done(stdout: "")
    }
}

/// GUI 4k, the first gap's Settings side: recall's summaries are forgotten
/// only once seen and confirmed -- and the CLI takes the forget from a pipe
/// only with `--yes`.
@Suite("Settings: recall forgotten (GUI 4k)", .timeLimit(.minutes(1)))
@MainActor
struct RecallForgetTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func runner() -> RuleRunner {
        let runner = RuleRunner()
        runner.when(["chats", "recall"], .done(stdout: recordedText("chats-recall.txt")))
        runner.when(["chats", "recall", "--forget"], refused("chats-recall-forget-refused.err"))
        runner.when(["chats", "recall", "--forget", "--yes"], .done(stdout: recordedText("chats-recall-forget.txt")))
        return runner
    }

    @Test("Forget, once the person said it, is chats recall --forget --yes: the CLI refuses the bare --forget on a pipe")
    func forgetSendsYes() async {
        let runner = runner()
        var said: [String] = []
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        model.say = { said.append($0) }
        model.hasTrack52 = { true }
        await model.connect(binary: Self.binary, version: "0.2.0")
        await model.forgetRecall()
        #expect(runner.calls.contains("run chats recall --forget --yes"))
        #expect(!runner.calls.contains("run chats recall --forget"), "never the form the CLI refuses on a pipe")
        #expect(model.failure == nil)
        #expect(said.last?.hasPrefix("forgot 2 chat summaries") == true, "the CLI's own words")
    }
}

/// GUI 4k, the first gap's other half: what recall holds is read and shown
/// before anything is forgotten.
@Suite("Settings: recall seen before it is forgotten (GUI 4k)", .timeLimit(.minutes(1)))
@MainActor
struct RecallSeenTests {
    @Test("Forget asks first, with what recall holds read and shown; nothing runs until the person says yes")
    func forgetAsksFirst() async {
        let runner = RecallForgetTests().runner()
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        model.hasTrack52 = { true }
        await model.connect(binary: RecallForgetTests.binary, version: "0.2.0")
        #expect(model.recallHolds == "recall holds 2 chat summaries in /Users/someone/.apogee/memory/chats.db", "read with the pane")
        await model.askToForget()
        #expect(model.forgetting)
        #expect(!runner.calls.contains { $0.hasPrefix("run chats recall --forget") })
        await model.forgetRecall()
        #expect(!model.forgetting)
        #expect(runner.calls.contains("run chats recall --forget --yes"))
    }

    @Test("a build before 52e reads nothing of recall and asks nothing")
    func olderBuild() async {
        let runner = RecallForgetTests().runner()
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        await model.connect(binary: RecallForgetTests.binary, version: "0.1.9")
        await model.askToForget()
        #expect(model.recallHolds == nil && !model.forgetting)
        #expect(!runner.calls.contains { $0.hasPrefix("run chats recall") })
    }
}
