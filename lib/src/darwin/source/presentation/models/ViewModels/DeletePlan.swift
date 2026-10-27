import BusinessReads
import Foundation

/// What `models delete <name>` says it would do, asked without `--yes`: the
/// CLI's own plan -- what goes, and every backend and reference pointing into
/// it (the CLI's M14 and 55d) -- read before the person is asked (GUI 4e).
/// Run so, the verb removes nothing and asks nothing; the app never works out
/// what points into a set itself. Stored weights' delete and the Backends
/// tab's Delete with its weights both ask through it.
public struct DeletePlan: Equatable, Sendable {
    /// The plan as the CLI printed it, its closing "re-run with --yes" left
    /// out: the confirm's buttons are the yes.
    public let lines: [String]
    /// The backends the plan says point into what goes, as it names them
    /// (`backend 'X' points into this and will stop working …`).
    public let backends: [String]

    /// The plan `models delete <name>` printed without `--yes`.
    public init(said: String) {
        var lines = said.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
        func trim() {
            while lines.last?.trimmingCharacters(in: .whitespaces).isEmpty == true { lines.removeLast() }
        }
        trim()
        if lines.last?.hasPrefix("re-run with --yes") == true { lines.removeLast() }
        trim()
        self.lines = lines
        backends = lines.compactMap(Self.backend(pointingIn:))
    }

    /// The backend a plan's line says points into what goes.
    static func backend(pointingIn line: String) -> String? {
        let opening = "backend '"
        guard line.hasPrefix(opening), let end = line.range(of: "' points into this") else { return nil }
        return String(line[line.index(line.startIndex, offsetBy: opening.count)..<end.lowerBound])
    }

    /// What reading a plan came to.
    public enum Reading: Equatable, Sendable {
        case plan(DeletePlan)
        /// The CLI refused it -- nothing by that name -- in its words.
        case refused(String)
    }

    /// What a screen says while the plan is read.
    public static let reading = "Reading what the delete removes"

    /// `models delete <name>` without `--yes`, as typed: its plan, or why
    /// there is none.
    public static func read(_ name: String, runner: any CommandRunning, binary: URL) async -> Reading {
        let outcome = await runner.command(binary: binary, ["models", "delete", name])
        guard case .done(let said) = outcome else { return .refused(outcome.failure ?? "apogee said nothing") }
        return .plan(DeletePlan(said: said))
    }

    /// The question a confirm asks: the plan as the CLI printed it -- what
    /// goes, and each backend that points into it -- then the command the yes
    /// runs.
    public func question(running command: String) -> String {
        (lines + ["", command + " -- this can't be undone."]).joined(separator: "\n")
    }
}

/// What a delete said beyond what it removed -- the plan's warnings, a kept
/// backend and its two doors, the backends removed with it, the roles it
/// cleared -- kept, every line as the CLI printed it, until the person
/// dismisses it (GUI 4e). Since GUI M16 the same card keeps what any other
/// change said of a role the CLI moved on the person's behalf (the CLI's
/// M33): a removed backend's cleared roles, a registration's default.
public struct DeleteNarration: Equatable, Sendable {
    /// The app's line for what was done: `Deleted <name>`, `Registered <name>`.
    public let title: String
    /// Every line the delete printed, as printed.
    public let lines: [String]

    /// `said`, unless it says no more than the folders it removed (`will
    /// remove:`, each folder, `removed.`).
    public init?(title: String, said: String) {
        var lines = said.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
        while lines.last?.trimmingCharacters(in: .whitespaces).isEmpty == true { lines.removeLast() }
        var listing = false
        let more = lines.contains { line in
            if line == "will remove:" {
                listing = true
                return false
            }
            if listing && line.hasPrefix("  ") { return false }
            listing = false
            return !line.trimmingCharacters(in: .whitespaces).isEmpty && line != "removed."
        }
        guard more else { return nil }
        self.title = title
        self.lines = lines
    }

    /// `said`, every line as printed, where it says a role the CLI moved on
    /// the person's behalf (`RoleChange`, the CLI's M33): `models.default set
    /// to '<name>' (none was set)`, `models.<field> cleared: it named
    /// '<name>'`. `nil` where it says none -- a change that moved no role, or
    /// a binary before M33 -- whose toast says it, as before.
    public init?(title: String, movingRoles said: String) {
        guard !RoleChange.said(in: said).isEmpty else { return nil }
        var lines = said.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
        while lines.last?.trimmingCharacters(in: .whitespaces).isEmpty == true { lines.removeLast() }
        self.title = title
        self.lines = lines
    }
}
