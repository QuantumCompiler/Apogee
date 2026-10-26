import Foundation
import Synchronization

/// Every child the app runs and supervises, in one place: each supervised
/// job's and the session's -- what the system monitor lists, and what a quit
/// stops (GUI 4c).
///
/// A child is registered by the supervisor that holds it, never by a screen:
/// `SupervisedJob` when it has spawned its run, `MachineSession` its chat. It
/// leaves when its end is said. So a run any screen starts is seen, whether
/// the screen keeps the job or only a local -- a media install, a preview's
/// dry run -- and a screen that starts runs needs no property of its own for
/// the monitor.
public final class ChildRegistry: Sendable {
    /// A child, as the monitor shows it.
    public struct Child: Sendable, Equatable, Identifiable {
        /// What holds it.
        public enum Kind: Sendable, Equatable {
            /// The session's chat (or execute) child.
            case session
            /// A supervised job's run.
            case job
        }

        /// Its place in the registry, in the order children registered.
        public let id: Int
        public let kind: Kind
        /// What it is, in a few words: `apogee video generate`, `Session: actor`.
        public let label: String
        public let processIdentifier: Int32
    }

    /// One child's place, held by its supervisor: renamed as it learns more,
    /// left at its end.
    public struct Registration: Sendable {
        public let id: Int
        private let registry: ChildRegistry

        fileprivate init(id: Int, registry: ChildRegistry) {
            self.id = id
            self.registry = registry
        }

        /// Shows it as `label` from now on.
        public func relabel(_ label: String) {
            registry.state.withLock { state in
                guard let index = state.entries.firstIndex(where: { $0.child.id == id }) else { return }
                let child = state.entries[index].child
                state.entries[index].child = Child(
                    id: child.id, kind: child.kind, label: label, processIdentifier: child.processIdentifier)
            }
        }

        /// It ended: it is no longer listed. Leaving twice is leaving once.
        public func leave() {
            registry.state.withLock { $0.entries.removeAll { $0.child.id == id } }
        }
    }

    /// The app's own: what `JobStarter()` and `ApogeeClient(identity:)`
    /// register with, and the composition reads.
    public static let app = ChildRegistry()

    private struct Entry: Sendable {
        var child: Child
        /// Its supervisor's Stop, with the grace it is given.
        let stop: @Sendable (Duration) async -> Void
    }

    private struct State {
        var next = 1
        var entries: [Entry] = []
    }

    private let state = Mutex(State())

    public init() {}

    /// Lists a child that has just started; `stop` is its supervisor's Stop,
    /// which a quit asks with its grace.
    public func register(
        kind: Child.Kind, label: String, processIdentifier: Int32,
        stop: @escaping @Sendable (Duration) async -> Void
    ) -> Registration {
        let id = state.withLock { state in
            defer { state.next += 1 }
            state.entries.append(
                Entry(child: Child(id: state.next, kind: kind, label: label, processIdentifier: processIdentifier), stop: stop))
            return state.next
        }
        return Registration(id: id, registry: self)
    }

    /// Every child running now, in the order they started.
    public var children: [Child] {
        state.withLock { $0.entries.map(\.child) }
    }

    /// The children of one kind running now, in the order they started.
    public func children(_ kind: Child.Kind) -> [Child] {
        children.filter { $0.kind == kind }
    }

    /// Stops every child of `kind` running now, all at once, each by its own
    /// supervisor's Stop with `grace` -- a job's: interrupted as Ctrl-C does,
    /// terminated after the grace, killed two seconds later -- and returns
    /// once every Stop has, saying whether all of them have left.
    public func stop(_ kind: Child.Kind, grace: Duration) async -> Bool {
        let stopping = state.withLock { state in state.entries.filter { $0.child.kind == kind } }
        await withTaskGroup(of: Void.self) { group in
            for entry in stopping {
                group.addTask { await entry.stop(grace) }
            }
        }
        let ids = Set(stopping.map(\.child.id))
        return state.withLock { state in !state.entries.contains { ids.contains($0.child.id) } }
    }

    /// A job's label: the command the app ran as a person would type it --
    /// `apogee` and the words before its first option -- the first three
    /// arguments when it opens with an option, and no more than
    /// `labelLimit` characters (a prompt can be a paragraph); the monitor
    /// fits what it shows to its row.
    public static func label(for arguments: [String]) -> String {
        let words = arguments.prefix { !$0.hasPrefix("-") }
        let said = (["apogee"] + (words.isEmpty ? Array(arguments.prefix(3)) : Array(words))).joined(separator: " ")
        return said.count > labelLimit ? String(said.prefix(labelLimit - 1)) + "…" : said
    }

    /// The longest label a job is given.
    public static let labelLimit = 120
}
