import DataDiscovery
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
    /// Stops the turn in flight, the protocol's way.
    func cancel() async throws
    /// Ends the session the protocol's way: its input closes.
    func close() async
    /// Ends the session, escalating only as far as it must.
    func stop(grace: Duration) async
}

extension MachineSession: ChatSession {}

/// Where sessions come from: the binary located, a chat opened on it.
/// `ApogeeClient` is the app's.
public protocol ChatStarting: Sendable {
    func locateBinary() async -> DiscoveryOutcome
    func openChat(binary: URL) -> any ChatSession
}

extension ApogeeClient: ChatStarting {
    public func openChat(binary: URL) -> any ChatSession {
        startChat(binary: binary)
    }
}

extension SessionEnd {
    /// The child's stderr tail, as text: the details behind `message`.
    public var stderrText: String { stderrTail.text }
}
