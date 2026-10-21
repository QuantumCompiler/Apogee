import Foundation
import Synchronization

/// Where the app keeps a value between launches, by key: `UserDefaults` in
/// the app, a dictionary in a test.
public protocol KeyValueStore: Sendable {
    func string(forKey key: String) -> String?
    func set(_ value: String?, forKey key: String)
}

/// The GUI's own settings -- never the CLI's config, which only the CLI edits.
public final class Preferences: Sendable {
    private let store: any KeyValueStore

    /// The keys, spelled once.
    enum Key {
        static let binaryOverride = "binaryOverride"
        static let route = "route"
        static let sidebarHidden = "sidebarHidden"
        static let inspectorOpen = "inspectorOpen"
        static let openChat = "openChat"
    }

    public init(store: any KeyValueStore) {
        self.store = store
    }

    /// The app's own preferences: its standard defaults, whose domain in the
    /// app is its bundle identifier, `com.quantumcompiler.apogee`. (A suite
    /// named for the app's own identifier is refused by `UserDefaults`, and
    /// would keep nothing.)
    public static func standard() -> Preferences {
        Preferences(store: DefaultsStore(.standard))
    }

    /// The `apogee` binary the user pointed the app at, ahead of every place
    /// discovery looks; `nil` when they have not.
    public var binaryOverride: URL? {
        get {
            store.string(forKey: Key.binaryOverride)
                .flatMap { $0.isEmpty ? nil : URL(fileURLWithPath: $0) }
        }
        set {
            store.set(newValue?.path, forKey: Key.binaryOverride)
        }
    }

    /// The place the window last showed, by its name; `nil` before the first.
    public var route: String? {
        get { store.string(forKey: Key.route).flatMap { $0.isEmpty ? nil : $0 } }
        set { store.set(newValue, forKey: Key.route) }
    }

    /// Whether the person hid the sidebar.
    public var sidebarHidden: Bool {
        get { bool(Key.sidebarHidden) }
        set { setBool(newValue, Key.sidebarHidden) }
    }

    /// Whether the inspector is open beside a session.
    public var inspectorOpen: Bool {
        get { bool(Key.inspectorOpen) }
        set { setBool(newValue, Key.inspectorOpen) }
    }

    /// The saved conversation open in Sessions, by its id; `nil` for a new
    /// one not yet saved.
    public var openChat: String? {
        get { store.string(forKey: Key.openChat).flatMap { $0.isEmpty ? nil : $0 } }
        set { store.set(newValue, forKey: Key.openChat) }
    }

    /// A flag, kept as `true` or absent.
    func bool(_ key: String) -> Bool {
        store.string(forKey: key) == "true"
    }

    func setBool(_ value: Bool, _ key: String) {
        store.set(value ? "true" : nil, forKey: key)
    }
}

/// `UserDefaults` as a `KeyValueStore`.
public struct DefaultsStore: KeyValueStore, @unchecked Sendable {
    private let defaults: UserDefaults

    public init(_ defaults: UserDefaults) {
        self.defaults = defaults
    }

    public func string(forKey key: String) -> String? { defaults.string(forKey: key) }

    public func set(_ value: String?, forKey key: String) {
        if let value { defaults.set(value, forKey: key) } else { defaults.removeObject(forKey: key) }
    }
}

/// A store in memory: a test's, or a session that keeps nothing.
public final class MemoryStore: KeyValueStore {
    private let values: Mutex<[String: String]>

    public init(_ values: [String: String] = [:]) {
        self.values = Mutex(values)
    }

    public func string(forKey key: String) -> String? { values.withLock { $0[key] } }

    public func set(_ value: String?, forKey key: String) { values.withLock { $0[key] = value } }
}
