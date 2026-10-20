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
    }

    public init(store: any KeyValueStore) {
        self.store = store
    }

    /// The app's own preferences, in its defaults domain.
    public static func standard() -> Preferences {
        Preferences(store: DefaultsStore(UserDefaults(suiteName: "com.quantumcompiler.apogee") ?? .standard))
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
