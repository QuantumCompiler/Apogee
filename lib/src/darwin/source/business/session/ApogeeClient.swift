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
    /// The CLI's install channel this client drives (GUI M18): the build's,
    /// handed down by the composition root. A client named none is the
    /// release channel's.
    public let channel: InstallChannel
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
        self.init(identity: identity, children: children, channel: .release)
    }

    /// The app's client on `channel` -- the build's, read once from its bundle
    /// by the composition root -- its sessions listed in `children`. Its
    /// preferences are the app's own `UserDefaults`, whose domain is the
    /// bundle's identifier: a dev build's are its own, never the installed
    /// app's.
    public init(identity: ClientIdentity, children: ChildRegistry, channel: InstallChannel) {
        self.init(
            identity: identity, spawner: ProcessSpawner(), preferences: .standard(),
            home: FileManager.default.homeDirectoryForCurrentUser, children: children, channel: channel)
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
        self.init(
            identity: identity, spawner: spawner, preferences: preferences, home: home, children: children,
            channel: .release)
    }

    /// A client over `spawner` on `channel`, its sessions listed in
    /// `children` while they run.
    public init(
        identity: ClientIdentity, spawner: any ChildSpawner, preferences: Preferences, home: URL,
        children: ChildRegistry?, channel: InstallChannel
    ) {
        self.identity = identity
        self.spawner = spawner
        self.preferences = preferences
        self.home = home
        self.children = children
        self.channel = channel
    }

    /// The binary to drive: the user's override first, then the client's
    /// channel's places -- or the typed condition that none answered, with
    /// that channel's install line.
    public func locateBinary() async -> DiscoveryOutcome {
        await BinaryDiscovery(spawner: spawner, home: home).locate(
            override: preferences.binaryOverride, channel: channel)
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
