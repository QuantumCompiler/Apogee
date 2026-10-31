import DataDiscovery
import DataProtocol
import Foundation

/// A driven chat session, as whoever renders one sees it: the events it says,
/// and the lines written back. `MachineSession` is the app's; a test's
/// scripted session stands in for it.
public protocol ChatSession: AnyObject, Sendable {
    /// Everything the session says, in order, `.ended` last.
    var events: AsyncStream<SessionEvent> { get }
    /// Sends a turn; returns its number.
    func send(user text: String) async throws -> Int
    /// Answers the standing question's next item.
    func answer(_ text: String) async throws
    /// Attaches a file, folder or glob.
    func attach(path: String, graph: String?) async throws
    /// Detaches it again (52d).
    func detach(path: String) async throws
    /// Moves the run settings from the next turn (52d).
    func set(_ settings: RunSettings) async throws
    /// Compacts the conversation now (52d).
    func compact() async throws
    /// Stops the turn in flight, the protocol's way.
    func cancel() async throws
    /// Ends the session the protocol's way: its input closes.
    func close() async
    /// Ends the session, escalating only as far as it must.
    func stop(grace: Duration) async
    /// Its child's process id, when it has one.
    var processIdentifier: Int32? { get }
}

extension ChatSession {
    public var processIdentifier: Int32? { nil }
}

extension MachineSession: ChatSession {}

/// Where sessions come from: the binary located, a chat opened on it.
/// `ApogeeClient` is the app's.
public protocol ChatStarting: Sendable {
    func locateBinary() async -> DiscoveryOutcome
    /// A chat on `binary`, started as `launch` says: new, or resuming one.
    func openChat(binary: URL, launch: ChatLaunch) -> any ChatSession
}

extension ChatStarting {
    /// A new chat.
    public func openChat(binary: URL) -> any ChatSession {
        openChat(binary: binary, launch: ChatLaunch())
    }
}

extension ApogeeClient: ChatStarting {
    public func openChat(binary: URL, launch: ChatLaunch) -> any ChatSession {
        startChat(binary: binary, launch: launch)
    }
}

/// A starter whose binary was already found: discovery runs once, and each
/// session after opens on what it found.
public struct LocatedStarter: ChatStarting {
    public let found: FoundBinary
    private let base: any ChatStarting

    public init(base: any ChatStarting, found: FoundBinary) {
        self.base = base
        self.found = found
    }

    public func locateBinary() async -> DiscoveryOutcome { .found(found, skipped: []) }

    public func openChat(binary: URL, launch: ChatLaunch) -> any ChatSession {
        base.openChat(binary: binary, launch: launch)
    }
}

extension SessionEnd {
    /// The child's stderr tail, as text: the details behind `message`.
    public var stderrText: String { stderrTail.text }
}
