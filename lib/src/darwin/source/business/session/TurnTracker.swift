import DataProtocol

/// Where a driven session stands, decided from the protocol's own events --
/// never from a timer, never inferred.
///
/// Every accepted `user` line is a turn, numbered from the session's
/// `next_turn`, and ends in exactly one `result` or one `error`
/// (machine-mode.md -> Turns). A `question` holds its turn until each of its
/// questions has its `answer` line.
public struct TurnTracker: Sendable, Equatable {
    public enum Phase: Sendable, Equatable {
        /// Spawned; the `session` event has not arrived.
        case opening
        /// No turn open: the session waits for a `user` line.
        case idle
        /// A turn is being answered.
        case running(turn: Int)
        /// A turn waits on the user: `unanswered` answers still owed.
        case asking(turn: Int, unanswered: Int)
        /// The child has exited.
        case over
    }

    public private(set) var phase: Phase = .opening
    /// The number the next `user` line gets.
    public private(set) var nextTurn = 1
    /// Turns sent and not yet ended, oldest first: turns are serial, but a
    /// line may be sent while the one before it is still answered.
    public private(set) var open: [Int] = []

    public init() {}

    /// Whether a turn is in flight -- running, or waiting on an answer --
    /// which is what a `cancel` reaches.
    public var turnInFlight: Bool {
        switch phase {
        case .running, .asking: true
        case .opening, .idle, .over: false
        }
    }

    /// A `user` line was written: it is turn `nextTurn`.
    public mutating func sent() -> Int {
        let turn = nextTurn
        nextTurn += 1
        open.append(turn)
        if phase == .idle {
            phase = .running(turn: turn)
        }
        return turn
    }

    /// An `answer` line was written.
    public mutating func answered() {
        if case .asking(let turn, let unanswered) = phase {
            phase = unanswered > 1 ? .asking(turn: turn, unanswered: unanswered - 1) : .running(turn: turn)
        }
    }

    /// What `event` says about the session.
    public mutating func apply(_ event: ProtocolEvent) {
        guard phase != .over else { return }
        switch event.kind {
        case .session(let info):
            // Numbered past the turns a resumed conversation already holds;
            // a line sent before the session spoke takes the first of them.
            let early = open.count
            nextTurn = info.nextTurn + early
            open = (0..<early).map { info.nextTurn + $0 }
            phase = open.first.map { .running(turn: $0) } ?? .idle
        case .question(let question):
            let turn = event.turn ?? open.first ?? nextTurn - 1
            phase = .asking(turn: turn, unanswered: max(1, question.items.count))
        case .result, .error:
            let turn = event.turn ?? open.first
            if let turn, let index = open.firstIndex(of: turn) {
                open.remove(at: index)
            } else if !open.isEmpty {
                open.removeFirst()
            }
            phase = open.first.map { .running(turn: $0) } ?? .idle
        default:
            if phase == .idle, let turn = event.turn, open.contains(turn) {
                phase = .running(turn: turn)
            }
        }
    }

    /// The child exited.
    public mutating func end() {
        phase = .over
        open = []
    }
}
