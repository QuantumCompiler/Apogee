import BusinessJobs
import DataDiscovery
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess

/// Who is driving: written in the `hello` line, recorded in the CLI's log.
public struct ClientIdentity: Sendable, Equatable {
    public let name: String
    public let version: String

    public init(name: String = "Apogee", version: String) {
        self.name = name
        self.version = version
    }
}

/// The client core's front door: find the binary, start a session on it.
public struct ApogeeClient: Sendable {
    public let identity: ClientIdentity
    private let spawner: any ChildSpawner
    private let preferences: Preferences
    private let home: URL
    /// Where each session's child is listed while it runs; none for a client
    /// no monitor reads.
    private let children: ChildRegistry?

    /// The app's client: real processes, the app's own preferences, the
    /// user's home directory, each session listed in the app's registry.
    ///
    /// Spelled out here rather than as default arguments: Swift compiles a
    /// public initializer's default arguments into its caller, so defaults
    /// naming `ProcessSpawner` and `Preferences` would make every caller link
    /// InfrastructureProcess and DataPreferences -- which `PresentationApp`
    /// does not, and its link failed for it (2026-10-20).
    public init(identity: ClientIdentity) {
        self.init(identity: identity, children: .app)
    }

    /// The app's client, its sessions listed in `children`.
    public init(identity: ClientIdentity, children: ChildRegistry) {
        self.init(
            identity: identity, spawner: ProcessSpawner(), preferences: .standard(),
            home: FileManager.default.homeDirectoryForCurrentUser, children: children)
    }

    /// A client over `spawner`, its sessions listed nowhere.
    public init(identity: ClientIdentity, spawner: any ChildSpawner, preferences: Preferences, home: URL) {
        self.init(identity: identity, spawner: spawner, preferences: preferences, home: home, children: nil)
    }

    /// A client over `spawner`, its sessions listed in `children` while they run.
    public init(
        identity: ClientIdentity, spawner: any ChildSpawner, preferences: Preferences, home: URL,
        children: ChildRegistry?
    ) {
        self.identity = identity
        self.spawner = spawner
        self.preferences = preferences
        self.home = home
        self.children = children
    }

    /// The binary to drive: the user's override first, then the install
    /// channels' places -- or the typed condition that none answered.
    public func locateBinary() async -> DiscoveryOutcome {
        await BinaryDiscovery(spawner: spawner, home: home).locate(override: preferences.binaryOverride)
    }

    /// A chat session on `binary`, working in `folder` -- the user's home
    /// directory unless one is given: a `.app` starts in `/`, which is no
    /// place for a chat's tools to work.
    public func startChat(
        binary: URL, launch: ChatLaunch = ChatLaunch(), folder: URL? = nil, environment: [String: String] = [:]
    ) -> MachineSession {
        var command = launch.command(binary: binary)
        command.workingDirectory = folder ?? launch.folder ?? home
        command.environment = environment
        return MachineSession.start(
            spawner: spawner, command: command,
            hello: .hello(
                clientName: identity.name, clientVersion: identity.version, wants: launch.tools ? ["tools"] : []),
            registry: children)
    }
}
