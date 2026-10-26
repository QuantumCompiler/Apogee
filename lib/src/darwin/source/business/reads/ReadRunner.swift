import DataProtocol
import Foundation
import InfrastructureProcess

/// Runs one read -- `apogee <command> --output-format json` -- and hands back
/// its document, or why there is none, in the CLI's own words; asked, the
/// stderr it left beside a document too.
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
        await runKeepingStderr(binary: binary, arguments, environment: environment, workingDirectory: workingDirectory)
            .outcome
    }

    /// The same read, with the stderr tail its child left whatever the
    /// outcome -- beside a document too, where `run` keeps it only to say why
    /// a read failed.
    public func runKeepingStderr(
        binary: URL, _ arguments: [String], environment: [String: String] = [:], workingDirectory: URL? = nil
    ) async -> ReadWithStderr {
        let command = ChildCommand(
            executable: binary, arguments: arguments + ["--output-format", "json"], environment: environment,
            workingDirectory: workingDirectory)
        let run: CapturedRun
        do {
            run = try await spawner.capture(command, timeout: timeout)
        } catch {
            return ReadWithStderr(outcome: .couldNotStart("\(error)"), stderrTail: BoundedTail())
        }
        let outcome: ReadOutcome
        if run.timedOut {
            outcome = .timedOut(after: timeout, stderrTail: run.stderrTail)
        } else if !run.exit.succeeded {
            outcome = .failed(exit: run.exit, stderrTail: run.stderrTail)
        } else if !run.stdoutTruncated, let document = ReadDocument(text: run.stdout) {
            outcome = .document(document)
        } else {
            outcome = .undecodable(stdout: String(run.stdout.prefix(4096)), stderrTail: run.stderrTail)
        }
        return ReadWithStderr(outcome: outcome, stderrTail: run.stderrTail)
    }
}

/// A read's outcome and what its child said on stderr, kept whatever the
/// exit: a read that succeeds may still say something there -- a binary
/// before the CLI's 55b writes the warnings of `chats show` to stderr alone,
/// where a document's reader never looks.
public struct ReadWithStderr: Sendable, Equatable {
    public let outcome: ReadOutcome
    public let stderrTail: BoundedTail

    public init(outcome: ReadOutcome, stderrTail: BoundedTail) {
        self.outcome = outcome
        self.stderrTail = stderrTail
    }

    /// What stderr said, line by line: each with anything on it, trimmed, in
    /// the order written.
    public var stderrLines: [String] {
        stderrTail.text.split(whereSeparator: \.isNewline)
            .map { $0.trimmingCharacters(in: .whitespaces) }
            .filter { !$0.isEmpty }
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
