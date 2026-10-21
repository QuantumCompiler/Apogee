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

    var calls: [String] { log.withLock { $0 } }

    func reading(_ prefix: [String], _ json: String) {
        reads.withLock { $0.insert((prefix, .document(ReadDocument(text: json)!)), at: 0) }
    }

    func commanding(_ prefix: [String], _ outcome: CommandOutcome) {
        commands.withLock { $0.insert((prefix, outcome), at: 0) }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        log.withLock { $0.append("read " + arguments.joined(separator: " ")) }
        return reads.withLock { rules in rules.first { arguments.starts(with: $0.0) }?.1 }
            ?? .couldNotStart("no scripted read for \(arguments)")
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
