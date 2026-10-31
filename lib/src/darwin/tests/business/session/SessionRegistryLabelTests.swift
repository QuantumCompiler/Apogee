import BusinessJobs
import BusinessSession
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

/// GUI M10, at the monitor: the session's place in the children registry is
/// `Session: <model>` for the model the CLI says answered -- each `result`
/// that names another one renames it, as the `session` event named it first.
/// Over the branch binary's recording (`switched-to-local.jsonl`): a session
/// on `claude`, the settings sheet's `set` to the local model, its notice,
/// then a turn the local model answered.
@Suite("The monitor names the model that answered (M10)", .timeLimit(.minutes(1)))
struct SessionRegistryLabelTests {
    static let switched: [String] = {
        let fixture = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().appendingPathComponent("data/protocol/Fixtures/switched-to-local.jsonl")
        return ((try? String(contentsOf: fixture, encoding: .utf8)) ?? "").split(separator: "\n").map(String.init)
    }()

    static func type(_ line: String) -> String? { JSONValue.parse(line)?["type"]?.stringValue }

    @Test("Session: claude from the session event and its answer; through the switch's notice still claude; Session: Gemma4-26B-Instruct once it answered")
    func relabelledByTheAnswer() async throws {
        let child = FedChild()
        let registry = ChildRegistry()
        let session = MachineSession.start(
            spawner: FedSpawner(child: child), command: ChildCommand(executable: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"), arguments: []),
            hello: hello, registry: registry)
        var events = session.events.makeAsyncIterator()
        var labels: [(type: String, label: String?)] = []
        for line in Self.switched {
            child.feed(line)
            _ = await events.next()
            labels.append((Self.type(line) ?? "", registry.children.first?.label))
        }
        let results = labels.indices.filter { labels[$0].type == "result" }
        try #require(results.count == 2)
        #expect(labels[...results[1]].dropLast().allSatisfy { $0.label == "Session: claude" })
        #expect(labels[results[1]].label == "Session: Gemma4-26B-Instruct")
        child.finish()
        while await events.next() != nil {}
        #expect(registry.children.isEmpty, "gone at its end")
    }

    @Test("a result that names no model, or none at all, leaves the label")
    func unnamedResultKeepsTheLabel() async {
        let child = FedChild()
        let registry = ChildRegistry()
        let session = MachineSession.start(
            spawner: FedSpawner(child: child), command: ChildCommand(executable: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"), arguments: []),
            hello: hello, registry: registry)
        var events = session.events.makeAsyncIterator()
        for line in [
            Self.switched[0], #"{"type":"result","text":"ok","turn":1}"#, #"{"type":"result","text":"ok","model":"","turn":2}"#,
        ] {
            child.feed(line)
            _ = await events.next()
        }
        #expect(registry.children.map(\.label) == ["Session: claude"])
        child.finish()
    }
}

/// A child whose stdout is fed by the test, a line at a time; what is written
/// to it goes nowhere.
final class FedChild: RunningChild, @unchecked Sendable {
    let lines: AsyncStream<String>
    private let continuation: AsyncStream<String>.Continuation

    init() {
        (lines, continuation) = AsyncStream.makeStream(of: String.self)
    }

    func feed(_ line: String) { continuation.yield(line) }
    func finish() { continuation.finish() }

    var stderrTail: BoundedTail { BoundedTail() }
    var processIdentifier: Int32 { 4242 }
    func write(_ data: Data) throws {}
    func closeInput() { finish() }
    func interrupt() { finish() }
    func terminate() { finish() }
    func kill() { finish() }
    func waitForExit() async -> ChildExit { ChildExit(reason: .exited, status: 0) }
}

/// Hands out one fed child.
struct FedSpawner: ChildSpawner {
    let child: FedChild
    func spawn(_ command: ChildCommand) throws -> any RunningChild { child }
}
