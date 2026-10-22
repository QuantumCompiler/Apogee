import Foundation

/// A child process to start: the executable, its arguments, and what its
/// environment changes from the parent's.
///
/// The value the process package spawns from. It describes, never runs: who
/// builds one (the session, the reads) decides what the CLI is asked to do,
/// and this layer only knows how to start a program and hold its pipes.
public struct ChildCommand: Sendable, Equatable {
    /// The program, as an absolute file URL -- never a name looked up on a
    /// `PATH`, which a `.app` does not inherit from any shell.
    public var executable: URL

    /// The arguments after the program's own name.
    public var arguments: [String]

    /// Variables set (or replaced) over the parent's environment.
    public var environment: [String: String]

    /// The child's working directory; the parent's when `nil`.
    public var workingDirectory: URL?

    public init(
        executable: URL,
        arguments: [String] = [],
        environment: [String: String] = [:],
        workingDirectory: URL? = nil
    ) {
        self.executable = executable
        self.arguments = arguments
        self.environment = environment
        self.workingDirectory = workingDirectory
    }

    /// The child's whole environment: the parent's, with this command's
    /// variables laid over it.
    public func environment(over parent: [String: String]) -> [String: String] {
        parent.merging(environment) { _, mine in mine }
    }

    /// The child's whole environment with `path` laid under this command's
    /// variables: the parent's, its `PATH` replaced by `path` (the person's
    /// login shell's, `ShellPath`), and this command's variables over both --
    /// so a command that sets its own `PATH` keeps it.
    public func environment(over parent: [String: String], path: String) -> [String: String] {
        var under = parent
        under["PATH"] = path
        return environment(over: under)
    }
}
