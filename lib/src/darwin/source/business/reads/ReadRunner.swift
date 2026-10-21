import DataProtocol
import Foundation
import InfrastructureProcess

/// Runs one read -- `apogee <command> --output-format json` -- and hands back
/// its document, or why there is none, in the CLI's own words.
///
/// A read is one bounded, captured child: its input closed, its stderr kept
/// as a tail, stopped if it runs past its time. Its stdout carries one JSON
/// document and nothing else (machine-mode.md -> Everything else is a CLI
/// command).
public struct ReadRunner: Sendable {
    let spawner: any ChildSpawner
    let timeout: Duration

    /// Over real processes. (Not a default argument: Swift compiles those
    /// into the caller, which would then have to link InfrastructureProcess.)
    public init(timeout: Duration = .seconds(60)) {
        self.init(spawner: ProcessSpawner(), timeout: timeout)
    }

    public init(spawner: any ChildSpawner, timeout: Duration = .seconds(60)) {
        self.spawner = spawner
        self.timeout = timeout
    }

    /// `arguments` is the read after `apogee` -- `["chats", "list"]` --
    /// without `--output-format`, which is added here.
    public func run(
        binary: URL, _ arguments: [String], environment: [String: String] = [:], workingDirectory: URL? = nil
    ) async -> ReadOutcome {
        let command = ChildCommand(
            executable: binary, arguments: arguments + ["--output-format", "json"], environment: environment,
            workingDirectory: workingDirectory)
        let run: CapturedRun
        do {
            run = try await spawner.capture(command, timeout: timeout)
        } catch {
            return .couldNotStart("\(error)")
        }
        if run.timedOut {
            return .timedOut(after: timeout, stderrTail: run.stderrTail)
        }
        guard run.exit.succeeded else {
            return .failed(exit: run.exit, stderrTail: run.stderrTail)
        }
        guard !run.stdoutTruncated, let document = ReadDocument(text: run.stdout) else {
            return .undecodable(stdout: String(run.stdout.prefix(4096)), stderrTail: run.stderrTail)
        }
        return .document(document)
    }
}

public enum ReadOutcome: Sendable, Equatable {
    case document(ReadDocument)
    /// The read exited badly: the stderr tail is the CLI's reason.
    case failed(exit: ChildExit, stderrTail: BoundedTail)
    /// It printed something that is not one JSON document (the first 4 KiB of it).
    case undecodable(stdout: String, stderrTail: BoundedTail)
    case timedOut(after: Duration, stderrTail: BoundedTail)
    case couldNotStart(String)
}
