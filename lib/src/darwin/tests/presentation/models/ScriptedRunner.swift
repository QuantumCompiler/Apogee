import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization

/// A runner a test scripts: each read or command answered by the first rule
/// its arguments start with, every call recorded. (The library's tests keep
/// their own copy: each bundle keeps its own support.)
final class ScriptedRunner: CommandRunning, @unchecked Sendable {
    private let reads = Mutex<[([String], ReadOutcome)]>([])
    private let commands = Mutex<[([String], CommandOutcome)]>([])
    private let log = Mutex<[String]>([])
    /// Reads held open until the test releases them: the next read each
    /// prefix starts waits on its gate.
    private let gates = Mutex<[([String], AsyncStream<Void>)]>([])
    private let released = Mutex(0)

    var calls: [String] { log.withLock { $0 } }

    /// How many held reads have answered since their release.
    var heldAnswered: Int { released.withLock { $0 } }

    /// Holds the next read `prefix` starts -- recorded in `calls` as it
    /// starts -- until the returned closure is called, then answers it by the
    /// rules, as a slow store read would (GUI 6d).
    func holding(_ prefix: [String]) -> @Sendable () -> Void {
        let (gate, open) = AsyncStream.makeStream(of: Void.self)
        gates.withLock { $0.append((prefix, gate)) }
        return { open.finish() }
    }

    func reading(_ prefix: [String], _ json: String) {
        reads.withLock { $0.insert((prefix, .document(ReadDocument(text: json)!)), at: 0) }
    }

    /// A read answered by `outcome` -- a failure, as the binary's would be.
    func reading(_ prefix: [String], outcome: ReadOutcome) {
        reads.withLock { $0.insert((prefix, outcome), at: 0) }
    }

    func commanding(_ prefix: [String], _ outcome: CommandOutcome) {
        commands.withLock { $0.insert((prefix, outcome), at: 0) }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        log.withLock { $0.append("read " + arguments.joined(separator: " ")) }
        let gate = gates.withLock { held -> AsyncStream<Void>? in
            guard let index = held.firstIndex(where: { arguments.starts(with: $0.0) }) else { return nil }
            return held.remove(at: index).1
        }
        if let gate {
            for await _ in gate {}
        }
        let outcome = reads.withLock { rules in rules.first { arguments.starts(with: $0.0) }?.1 }
            ?? .couldNotStart("no scripted read for \(arguments)")
        if gate != nil { released.withLock { $0 += 1 } }
        return outcome
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        log.withLock { $0.append("run " + arguments.joined(separator: " ")) }
        return commands.withLock { rules in rules.first { arguments.starts(with: $0.0) }?.1 } ?? .done(stdout: "")
    }
}

func tail(_ text: String) -> BoundedTail {
    var tail = BoundedTail()
    tail.append(Data((text + "\n").utf8))
    return tail
}
