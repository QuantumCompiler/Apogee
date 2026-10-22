import Foundation
import Synchronization

/// The `PATH` every child the app starts runs with: the one the person's own
/// terminal has, asked of their login shell once a launch (GUI M6).
///
/// An app opened from the Dock or the Finder inherits launchd's `PATH`,
/// `/usr/bin:/bin:/usr/sbin:/sbin`, and the tools a backend runs are not on
/// it: a vendor's CLI in `~/.local/bin`, Homebrew's in `/opt/homebrew/bin`.
/// The app never looks for those tools itself, and never decides which
/// backend runs -- it is a machine-mode client (the GUI's CLAUDE.md). It gives
/// the CLI the `PATH` the person's terminal would, and the CLI's own lookups
/// do the rest. `ProcessSpawner` lays it under every child's own variables.
///
/// - **Asked once**, by `begin()`, on a queue of its own: nothing waits for
///   it. A child spawned before the answer gets the fallback.
/// - **As a terminal starts it:** `$SHELL` (`/bin/zsh` when unset) as an
///   interactive login shell, `-ilc`, so its login and its interactive
///   profiles are both read -- the way editors such as VS Code ask it.
/// - **Bounded:** its input is `/dev/null`, its errors are dropped, and once
///   it runs past the deadline its whole process group is killed
///   (`LoginShell`).
/// - **Only `PATH`:** the shell runs `/usr/bin/printenv PATH` between two
///   printings of a marker made fresh for the ask, and only what lies between
///   them is read -- so a profile that prints is harmless, and no other
///   variable of the shell's crosses.
/// - **The fallback**, for a shell that fails, hangs or is not there: the
///   app's own `PATH` (launchd's, from the Dock) widened by
///   `wellKnownDirectories`.
public final class ShellPath: Sendable {
    /// A `PATH`, and where it came from.
    public struct Resolution: Sendable, Equatable {
        /// Where a `PATH` came from.
        public enum Source: Sendable, Equatable {
            /// The person's login shell answered it.
            case shell
            /// The shell has not answered, or could not: the fallback.
            case fallback
        }

        /// The `PATH` itself, its directories joined by `:`.
        public let path: String
        public let source: Source
        /// The login shell asked, as a path.
        public let shell: String
        /// Why the fallback, as the words that follow the shell's path ("didn't
        /// answer within 5 seconds"); `nil` when the shell answered.
        public let reason: String?

        public init(path: String, source: Source, shell: String, reason: String? = nil) {
            self.path = path
            self.source = source
            self.shell = shell
            self.reason = reason
        }

        /// The `PATH`'s directories, in order.
        public var directories: [String] {
            path.split(separator: ":").map(String.init)
        }
    }

    /// launchd's `PATH`: what an app opened from the Dock or the Finder has.
    public static let launchdPath = "/usr/bin:/bin:/usr/sbin:/sbin"

    /// What the fallback puts ahead of the app's own `PATH`, in this order --
    /// the person's own tools, Homebrew's, the classic local one -- as a
    /// terminal's `PATH` has them ahead of the system's. `~` is the home
    /// directory.
    public static let wellKnownDirectories = ["~/.local/bin", "/opt/homebrew/bin", "/usr/local/bin"]

    /// The shell asked when `SHELL` is unset: macOS's default.
    public static let defaultShell = "/bin/zsh"

    /// The variable the marker reaches the shell in.
    static let markerVariable = "APOGEE_PATH_MARKER"

    /// What the shell is asked to run: `PATH` between two printings of the
    /// marker, each by its absolute program, so nothing depends on the
    /// shell's own lookups or syntax beyond `;`. The marker's value is never in
    /// the command, so a shell that echoes its commands cannot print it early.
    static let command =
        "/usr/bin/printenv \(markerVariable); /usr/bin/printenv PATH; /usr/bin/printenv \(markerVariable)"

    /// The app's own, which `ProcessSpawner()` lays under its children; the
    /// application begins it at launch.
    public static let app = ShellPath(environment: ProcessInfo.processInfo.environment)

    /// The login shell asked: `SHELL`, or `/bin/zsh`.
    public let shell: String
    /// How long the shell has to answer before its process group is killed.
    public let deadline: Duration
    /// What children get until the shell answers, and from then on if it
    /// could not.
    public let fallback: Resolution

    private let environment: [String: String]
    private let home: String
    private let state = Mutex(State())

    private struct State {
        var asked = false
        var answer: Resolution?
        var waiters: [CheckedContinuation<Resolution, Never>] = []
    }

    /// A resolver over `environment` -- the app's own, or a test's: `SHELL`
    /// names the shell, `HOME` the home directory, `PATH` the fallback's base;
    /// the shell starts with all of it, and the marker.
    public init(environment: [String: String], deadline: Duration = .seconds(5)) {
        let shell = environment["SHELL"].flatMap { $0.isEmpty ? nil : $0 } ?? Self.defaultShell
        let home = environment["HOME"].flatMap { $0.isEmpty ? nil : $0 } ?? NSHomeDirectory()
        self.shell = shell
        self.deadline = deadline
        self.environment = environment
        self.home = home
        fallback = Resolution(
            path: Self.widened(environment["PATH"], home: home), source: .fallback, shell: shell,
            reason: "hasn't answered yet")
    }

    /// The fallback's `PATH`: the well-known directories, `~` expanded, then
    /// `inherited` (launchd's when there is none), each directory once, in
    /// order.
    static func widened(_ inherited: String?, home: String) -> String {
        let base = inherited.flatMap { $0.isEmpty ? nil : $0 } ?? launchdPath
        let wellKnown = wellKnownDirectories.map { directory in
            directory.hasPrefix("~/") ? (home as NSString).appendingPathComponent(String(directory.dropFirst(2))) : directory
        }
        var seen = Set<String>()
        return (wellKnown + base.split(separator: ":").map(String.init))
            .filter { !$0.isEmpty && seen.insert($0).inserted }
            .joined(separator: ":")
    }

    /// Asks the shell, the first time; every later call does nothing. Returns
    /// at once: the ask runs on a global queue -- never the main actor, and
    /// never Swift's cooperative pool, which a blocking wait must not hold --
    /// and is over by the deadline.
    public func begin() {
        let first = state.withLock { state -> Bool in
            defer { state.asked = true }
            return !state.asked
        }
        guard first else { return }
        DispatchQueue.global(qos: .userInitiated).async { [self] in
            let answer = ask()
            let waiters = state.withLock { state -> [CheckedContinuation<Resolution, Never>] in
                state.answer = answer
                defer { state.waiters = [] }
                return state.waiters
            }
            waiters.forEach { $0.resume(returning: answer) }
        }
    }

    /// The `PATH` a child gets now, never waiting: the shell's once it has
    /// answered, the fallback until then -- and from then on, if it could not.
    public var current: Resolution {
        state.withLock { $0.answer } ?? fallback
    }

    /// The answer, waited for while the shell is being asked (never past its
    /// deadline); the fallback when it was never asked. It never asks.
    public func answer() async -> Resolution {
        await withCheckedContinuation { continuation in
            let ready = state.withLock { state -> Resolution? in
                if let answer = state.answer { return answer }
                if !state.asked { return fallback }
                state.waiters.append(continuation)
                return nil
            }
            if let ready {
                continuation.resume(returning: ready)
            }
        }
    }

    // MARK: - The ask

    private func ask() -> Resolution {
        let marker = Data("apogee-path-\(UUID().uuidString)".utf8)
        var environment = self.environment
        environment[Self.markerVariable] = String(decoding: marker, as: UTF8.self)
        let run = LoginShell(
            executable: shell, arguments: ["-ilc", Self.command], environment: environment, directory: home,
            deadline: deadline
        ).run { Self.between(marker, in: $0) != nil }
        return resolution(of: run, marker: marker)
    }

    /// What a run of the shell says: the `PATH` between the markers from a
    /// shell that exited cleanly, or the fallback and why.
    private func resolution(of run: LoginShell.Run, marker: Data) -> Resolution {
        func fallingBack(_ reason: String) -> Resolution {
            Resolution(path: fallback.path, source: .fallback, shell: shell, reason: reason)
        }
        switch run.end {
        case .couldNotStart(let error):
            return fallingBack(error == ENOENT ? "isn't there" : "couldn't be started (\(String(cString: strerror(error))))")
        case .timedOut:
            return fallingBack("didn't answer within \(Self.said(deadline))")
        case .signalled(let signal):
            return fallingBack("was ended by signal \(signal)")
        case .exited(let status) where status != 0:
            return fallingBack("exited with status \(status)")
        case .exited:
            break
        }
        guard let answered = Self.between(marker, in: run.output) else {
            return fallingBack(run.overflowed ? "printed more than 1 MiB before its PATH" : "didn't print its PATH")
        }
        let path = String(decoding: answered, as: UTF8.self).trimmingCharacters(in: .newlines)
        guard !path.isEmpty else { return fallingBack("answered an empty PATH") }
        return Resolution(path: path, source: .shell, shell: shell)
    }

    /// What lies between the first two printings of `marker`, or `nil` when
    /// there are not two.
    static func between(_ marker: Data, in output: Data) -> Data? {
        guard let first = output.range(of: marker),
            let second = output.range(of: marker, in: first.upperBound..<output.endIndex)
        else { return nil }
        return output.subdata(in: first.upperBound..<second.lowerBound)
    }

    /// A deadline in words: "5 seconds", "1 second", "0.5 seconds".
    static func said(_ duration: Duration) -> String {
        let seconds = Double(duration.components.seconds) + Double(duration.components.attoseconds) / 1e18
        if seconds == 1 { return "1 second" }
        return seconds == seconds.rounded() ? "\(Int(seconds)) seconds" : "\(seconds) seconds"
    }
}
