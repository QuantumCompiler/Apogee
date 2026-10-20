import DataProtocol
import Foundation
import InfrastructureProcess

/// One driven `apogee` session: the child, its event stream, and the lines
/// written back to it.
///
/// Started, it spawns the child, writes the optional `hello` first, and reads
/// every stdout line into a `ProtocolEvent` delivered on `events`, in order,
/// with `.ended` last -- why it ended, and the child's stderr tail when that
/// was a failure. Stopping a turn is the protocol's `cancel` line; stopping
/// the session closes the child's input first and terminates it only when it
/// will not go (`stop(grace:)`).
public actor MachineSession {
    /// Everything the session says, in order, `.ended` last.
    public nonisolated let events: AsyncStream<SessionEvent>
    private let continuation: AsyncStream<SessionEvent>.Continuation
    private let child: (any RunningChild)?

    /// Where the session stands, from its events.
    public private(set) var tracker = TurnTracker()
    /// The `session` event's facts, once it has arrived.
    public private(set) var info: SessionInfo?

    private var inputClosed = false
    private var stopping = false
    private var refusedVersion: Int?
    private var ended = false

    /// Spawns `command` and begins reading it. A child that will not start is
    /// a session that ends at once, `couldNotStart`, with the reason.
    public static func start(spawner: any ChildSpawner, command: ChildCommand, hello: DriverLine?) -> MachineSession {
        let session: MachineSession
        do {
            let child = try spawner.spawn(command)
            if let hello {
                try? child.write(Data((hello.encoded + "\n").utf8))
            }
            session = MachineSession(child: child)
        } catch {
            session = MachineSession(child: nil)
            session.continuation.yield(
                .ended(SessionEnd(reason: .couldNotStart, exit: nil, stderrTail: BoundedTail(), detail: "\(error)")))
            session.continuation.finish()
            return session
        }
        Task { await session.pump() }
        return session
    }

    private init(child: (any RunningChild)?) {
        (events, continuation) = AsyncStream.makeStream(of: SessionEvent.self)
        self.child = child
    }

    // MARK: - Reading

    private func pump() async {
        guard let child else { return }
        for await line in child.lines {
            receive(EventDecoder.decode(line: line))
        }
        let exit = await child.waitForExit()
        finish(exit: exit, tail: child.stderrTail)
    }

    private func receive(_ event: ProtocolEvent) {
        guard !ended, refusedVersion == nil else { return }
        if case .session(let session) = event.kind {
            guard MachineProtocol.understands(protocolVersion: session.protocolVersion) else {
                // An existing event's meaning has changed: refuse the stream
                // rather than misread it.
                refusedVersion = session.protocolVersion
                stopping = true
                child?.closeInput()
                child?.terminate()
                return
            }
            info = session
        }
        tracker.apply(event)
        continuation.yield(.event(event))
    }

    private func finish(exit: ChildExit, tail: BoundedTail) {
        guard !ended else { return }
        ended = true
        tracker.end()
        let reason: SessionEnd.Reason =
            if let refusedVersion { .unsupportedProtocol(refusedVersion) }
            else if stopping { .stopped }
            else if inputClosed && exit.succeeded { .finished }
            else { .failed }
        continuation.yield(.ended(SessionEnd(reason: reason, exit: exit, stderrTail: tail)))
        continuation.finish()
    }

    // MARK: - Writing

    /// Sends a turn. Returns its number -- provisional when sent before the
    /// `session` event, which may renumber it.
    @discardableResult
    public func send(user text: String) throws -> Int {
        try write(.user(text))
        return tracker.sent()
    }

    /// Answers the standing question's next item; a permission question takes
    /// `yes`, `no`, `session` or `always`.
    public func answer(_ text: String) throws {
        try write(.answer(text))
        tracker.answered()
    }

    /// Attaches a file, folder or glob, relative to the child's working directory.
    public func attach(path: String, graph: String? = nil) throws {
        try write(.attach(path: path, graph: graph))
    }

    /// Stops the turn in flight the protocol's way: it still ends in its one
    /// `result`, `finish_reason: "cancelled"`, and the session goes on. With
    /// no turn in flight the child ignores it.
    public func cancel() throws {
        try write(.cancel)
    }

    /// Ends the session the protocol's way: the child's input closes, and it
    /// saves the conversation and exits.
    public func close() {
        guard !inputClosed else { return }
        inputClosed = true
        child?.closeInput()
    }

    /// Ends the session, escalating only as far as it must: a turn in flight
    /// is cancelled and the input closed; a child still running after `grace`
    /// is terminated, and two seconds later killed.
    public func stop(grace: Duration = .seconds(5)) async {
        guard let child, !ended else { return }
        if tracker.turnInFlight {
            try? write(.cancel)
        }
        close()
        if await ends(within: grace) { return }
        stopping = true
        child.terminate()
        if await ends(within: .seconds(2)) { return }
        child.kill()
    }

    private func ends(within duration: Duration) async -> Bool {
        let deadline = ContinuousClock.now + duration
        while !ended && ContinuousClock.now < deadline {
            try? await Task.sleep(for: .milliseconds(20))
        }
        return ended
    }

    private func write(_ line: DriverLine) throws {
        guard let child, !ended, !inputClosed else { throw SessionError.notRunning }
        do {
            try child.write(Data((line.encoded + "\n").utf8))
        } catch {
            throw SessionError.notRunning
        }
    }
}

/// What a session says: an event of the protocol, or its end.
public enum SessionEvent: Sendable, Equatable {
    case event(ProtocolEvent)
    /// Always the last.
    case ended(SessionEnd)
}

/// Why a session ended, with the child's stderr tail.
public struct SessionEnd: Sendable, Equatable {
    public enum Reason: Sendable, Equatable {
        /// Its input closed, the child saved and exited cleanly.
        case finished
        /// The child ended on its own, or exited badly: the stderr tail says why.
        case failed
        /// It would not go when asked, and was terminated.
        case stopped
        /// The binary would not start; `detail` says why.
        case couldNotStart
        /// The child spoke a `protocol_version` this client does not read.
        case unsupportedProtocol(Int)
    }

    public let reason: Reason
    /// How the child exited; `nil` when it never started.
    public let exit: ChildExit?
    public let stderrTail: BoundedTail
    /// For `couldNotStart`: the system's reason.
    public let detail: String?

    public init(reason: Reason, exit: ChildExit?, stderrTail: BoundedTail, detail: String? = nil) {
        self.reason = reason
        self.exit = exit
        self.stderrTail = stderrTail
        self.detail = detail
    }

    /// One line saying why, the child's own last words included where there are some.
    public var message: String {
        let said = stderrTail.lastLine.map { ": \($0)" } ?? ""
        return switch reason {
        case .finished: "the session ended"
        case .failed: "apogee \(exit.map(\.description) ?? "stopped")\(said)"
        case .stopped: "apogee did not end when asked and was stopped\(said)"
        case .couldNotStart: "apogee could not be started: \(detail ?? "unknown reason")"
        case .unsupportedProtocol(let version):
            "apogee speaks machine-mode protocol \(version); this app reads protocol \(MachineProtocol.version) -- update the app or the CLI"
        }
    }
}

public enum SessionError: Error, Equatable {
    /// The session has ended, or its input is closed.
    case notRunning
}
