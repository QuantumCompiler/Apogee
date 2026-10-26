import DataProtocol
import Foundation
import InfrastructureProcess

/// What a screen runs through: a read, and a command that changes something.
/// `ReadRunner` is the app's; a test's scripted runner stands in for it.
public protocol CommandRunning: Sendable {
    /// `apogee <arguments> --output-format json`, its document decoded -- the
    /// flag before a `--` the arguments hold (`Argv`).
    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome
    /// `apogee <arguments>`, exactly as a person would type it.
    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome
    /// The same, `input` written to its standard input -- a secret goes this
    /// way, never on a command line.
    func command(binary: URL, _ arguments: [String], input: Data?) async -> CommandOutcome
    /// A read whose document stands whatever its exit: `check` exits
    /// non-zero when a row fails, and its document says which.
    func report(binary: URL, _ arguments: [String]) async -> ReadOutcome
    /// A read, with the stderr it left beside its document: what an older
    /// binary says there alone (`chats show`'s warnings before the CLI's 55b).
    func readKeepingStderr(binary: URL, _ arguments: [String]) async -> ReadWithStderr
}

extension CommandRunning {
    public func command(binary: URL, _ arguments: [String], input: Data?) async -> CommandOutcome {
        await command(binary: binary, arguments)
    }

    public func report(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        await read(binary: binary, arguments)
    }

    /// A runner that keeps no stderr says none.
    public func readKeepingStderr(binary: URL, _ arguments: [String]) async -> ReadWithStderr {
        ReadWithStderr(outcome: await read(binary: binary, arguments), stderrTail: BoundedTail())
    }
}

extension ReadRunner: CommandRunning {
    public func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        await run(binary: binary, arguments)
    }

    public func readKeepingStderr(binary: URL, _ arguments: [String]) async -> ReadWithStderr {
        await runKeepingStderr(binary: binary, arguments)
    }

    public func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        await command(binary: binary, arguments, input: nil)
    }

    public func report(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        let run: CapturedRun
        do {
            run = try await spawner.capture(
                ChildCommand(executable: binary, arguments: Argv.adding(["--output-format", "json"], to: arguments)), timeout: timeout)
        } catch {
            return .couldNotStart("\(error)")
        }
        if let document = ReadDocument(text: run.stdout) {
            return .document(document)
        }
        if run.timedOut {
            return .timedOut(after: timeout, stderrTail: run.stderrTail)
        }
        return run.exit.succeeded
            ? .undecodable(stdout: String(run.stdout.prefix(4096)), stderrTail: run.stderrTail)
            : .failed(exit: run.exit, stderrTail: run.stderrTail)
    }

    public func command(binary: URL, _ arguments: [String], input: Data?) async -> CommandOutcome {
        let run: CapturedRun
        do {
            run = try await spawner.capture(
                ChildCommand(executable: binary, arguments: arguments), timeout: timeout, input: input)
        } catch {
            return .couldNotStart("\(error)")
        }
        if run.timedOut {
            return .timedOut(after: timeout, stderrTail: run.stderrTail)
        }
        guard run.exit.succeeded else {
            return .failed(exit: run.exit, stderrTail: run.stderrTail)
        }
        return .done(stdout: run.stdout)
    }
}

/// How a command that changes something ended.
public enum CommandOutcome: Sendable, Equatable {
    /// It did what it said; `stdout` is what it printed for a person.
    case done(stdout: String)
    /// It refused or failed: the stderr tail is the CLI's reason.
    case failed(exit: ChildExit, stderrTail: BoundedTail)
    case timedOut(after: Duration, stderrTail: BoundedTail)
    case couldNotStart(String)

    public var succeeded: Bool {
        if case .done = self { true } else { false }
    }

    /// Why it did not succeed, as the design's failed state says it: the
    /// stderr's last line, word for word, and how it exited. `nil` when it did.
    public var failure: String? {
        switch self {
        case .done: nil
        case .failed(let exit, let tail): "\(tail.lastLine ?? "apogee said nothing") (\(exit))"
        case .timedOut(let after, let tail): "\(tail.lastLine.map { $0 + " -- " } ?? "")it did not finish within \(after)"
        case .couldNotStart(let reason): reason
        }
    }
}

extension ReadOutcome {
    /// Why there is no document, in the CLI's words where it gave some; `nil`
    /// for a document.
    public var failure: String? {
        switch self {
        case .document: nil
        case .failed(let exit, let tail): "\(tail.lastLine ?? "apogee said nothing") (\(exit))"
        case .undecodable: "apogee printed something that is not one JSON document"
        case .timedOut(let after, let tail): "\(tail.lastLine.map { $0 + " -- " } ?? "")it did not finish within \(after)"
        case .couldNotStart(let reason): reason
        }
    }

    /// The document, when there is one.
    public var document: ReadDocument? {
        if case .document(let document) = self { document } else { nil }
    }
}
