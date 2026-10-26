import BusinessReads
import Foundation

/// A backend's adapters as the Models place shows them (the CLI's 53a): each
/// one `config get backends.<name>.adapters` lists -- its path and scale --
/// beside what `check`'s Adapters row says of it: whether it is pinned to the
/// weights the backend runs now, and the way to mend it when not (53c).
public struct AdaptersView: Equatable, Sendable {
    /// One adapter and the doctor's word on it.
    public struct Row: Equatable, Sendable, Identifiable {
        public let adapter: BackendAdapter
        /// `check`'s row for it; `nil` when the doctor named none, or could
        /// not be read.
        public let check: AdapterCheck?
        public var id: String { adapter.path }

        /// `scale 0.5`, or `scale 1`.
        public var scaleWords: String { "scale \(BackendAdapter.number(adapter.scale))" }
    }

    public let backend: String
    public let rows: [Row]
    /// Why the doctor's word is missing: `check` failed to answer -- in its
    /// words -- or answered with no row for them.
    public let checkFailure: String?

    public init(backend: String, rows: [Row], checkFailure: String?) {
        self.backend = backend
        self.rows = rows
        self.checkFailure = checkFailure
    }
}

/// `config set-backend <name> --adapters <path>[:<scale>],...`: the whole
/// list as the person leaves it, replacing the entry's -- empty clears it
/// (`--adapters ""`). The CLI pins each to the base's weights as they are
/// now, and refuses by name what it will not take (a scale outside (0, 2],
/// a second adapter on an mlx entry, a file of the wrong kind).
public struct AdaptersForm: Equatable, Sendable, Identifiable {
    /// One adapter as edited: its path, and its scale as typed (empty is 1).
    public struct Entry: Equatable, Sendable, Identifiable {
        public let id: UUID
        public var path: String
        public var scale: String

        public init(path: String, scale: String = "") {
            id = UUID()
            self.path = path
            self.scale = scale
        }
    }

    public let backend: String
    /// `llamacpp` or `mlx`: an mlx entry takes one adapter directory at its
    /// trained scale, so its sheet offers no scale.
    public let backendType: String
    public var entries: [Entry]
    public var id: String { backend }

    /// The form over what the backend applies now.
    public init(backend: String, backendType: String, adapters: [BackendAdapter]) {
        self.backend = backend
        self.backendType = backendType
        entries = adapters.map { Entry(path: $0.path, scale: $0.scale == 1 ? "" : BackendAdapter.number($0.scale)) }
    }

    /// Whether the entry is an mlx one: one adapter, its scale the trained one.
    public var isMLX: Bool { backendType == "mlx" }

    /// `--adapters`' value: each path, `:<scale>` after it when one is typed
    /// and is not 1, joined by commas; empty clears.
    public var value: String {
        entries.compactMap { entry -> String? in
            let path = entry.path.trimmingCharacters(in: .whitespaces)
            guard !path.isEmpty else { return nil }
            let scale = entry.scale.trimmingCharacters(in: .whitespaces)
            return scale.isEmpty || scale == "1" || isMLX ? path : "\(path):\(scale)"
        }.joined(separator: ",")
    }

    /// The command, as typed.
    public var arguments: [String] {
        ["config", "set-backend", backend, "--adapters", value]
    }

    /// Adds `path`, at scale 1.
    public mutating func add(_ path: String) {
        entries.append(Entry(path: path))
    }

    public mutating func remove(_ id: Entry.ID) {
        entries.removeAll { $0.id == id }
    }
}
