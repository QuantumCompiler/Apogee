import BusinessSession
import DataDiscovery
import Foundation
import Observation

/// One chat session in the window: the binary found, the session started,
/// its events folded into the transcript, and the user's acts sent back as
/// the protocol's lines -- in the order they were made.
///
/// SwiftUI-free (ADR 0002): everything the window shows is `connection`,
/// `transcript` and `draft`, and everything it does is a method here.
@MainActor
@Observable
public final class SessionViewModel {
    public enum Connection: Equatable {
        /// Not started.
        case idle
        /// Looking for the apogee binary.
        case locating
        /// None found: how to install, and every place looked.
        case missing(hint: String, places: [String])
        /// A session runs on this binary.
        case connected(binary: String, version: String)
    }

    public private(set) var connection: Connection = .idle
    public private(set) var transcript = Transcript()
    /// What the user is typing.
    public var draft = ""

    private let starter: any ChatStarting
    private var session: (any ChatSession)?
    private var outbox: AsyncStream<Outbound>.Continuation?

    /// A line to write, queued so lines leave in the order the user made them.
    private enum Outbound {
        case user(String)
        case answer(String)
        case cancel
    }

    public init(starter: any ChatStarting) {
        self.starter = starter
    }

    // MARK: - The session

    /// Finds the binary and starts a session on it; again after one has ended
    /// or none was found.
    public func start() {
        switch connection {
        case .locating: return
        case .connected where transcript.status != .ended: return
        default: break
        }
        connection = .locating
        transcript = Transcript()
        Task { await connect() }
    }

    private func connect() async {
        switch await starter.locateBinary() {
        case .notFound(let skipped):
            connection = .missing(
                hint: DiscoveryOutcome.installHint,
                places: skipped.map { "\($0.candidate.url.path): \($0.reason)" })
        case .found(let found, _):
            connection = .connected(binary: found.url.path, version: found.version)
            let session = starter.openChat(binary: found.url)
            self.session = session
            let (lines, outbox) = AsyncStream.makeStream(of: Outbound.self)
            self.outbox = outbox
            Task { await Self.write(lines, to: session) }
            for await event in session.events {
                transcript.apply(event)
            }
            outbox.finish()
            self.outbox = nil
            self.session = nil
        }
    }

    /// Writes the queued lines one at a time, in order.
    private static func write(_ lines: AsyncStream<Outbound>, to session: any ChatSession) async {
        for await line in lines {
            switch line {
            case .user(let text): _ = try? await session.send(user: text)
            case .answer(let text): try? await session.answer(text)
            case .cancel: try? await session.cancel()
            }
        }
    }

    /// Ends the session: a turn in flight is cancelled and the child asked
    /// to go, terminated only if it will not.
    public func shutDown() async {
        await session?.stop(grace: .seconds(5))
    }

    // MARK: - The user's acts

    /// Whether the draft can be sent now.
    public var canSend: Bool {
        transcript.acceptsInput && outbox != nil
            && !draft.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    /// Whether there is a turn to stop.
    public var canCancel: Bool { transcript.turnInFlight && outbox != nil }

    /// Sends the draft as a turn.
    public func submit() {
        let text = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        guard canSend, let outbox else { return }
        draft = ""
        transcript.userSent(text)
        outbox.yield(.user(text))
    }

    /// Answers the question or permission card `id`: one answer per item, in order.
    public func answer(_ id: Int, with answers: [String]) {
        guard let outbox, !answers.isEmpty else { return }
        transcript.answered(id, with: answers)
        for answer in answers {
            outbox.yield(.answer(answer))
        }
    }

    /// Stops the turn in flight through the protocol: it ends in its own
    /// result, and the session goes on.
    public func cancel() {
        guard canCancel, let outbox else { return }
        transcript.cancelling()
        outbox.yield(.cancel)
    }

    // MARK: - What the window says

    /// The session's state, in a few words.
    public var statusText: String {
        switch connection {
        case .idle, .locating: return "Starting"
        case .missing: return "Apogee not found"
        case .connected: break
        }
        return switch transcript.status {
        case .connecting: "Starting apogee"
        case .ready: "Ready"
        case .thinking: "Thinking"
        case .working: "Working"
        case .answering: "Answering"
        case .waiting: "Waiting for you"
        case .stopping: "Stopping"
        case .ended: "Session ended"
        }
    }

    /// What the input says while it is not the user's.
    public var inputPrompt: String {
        switch transcript.status {
        case .waiting: "Answer the question above"
        case .ended: "The session has ended"
        case .connecting: "Starting apogee"
        default: "Message Apogee"
        }
    }
}
