import BusinessSession
import DataProtocol

/// The conversation as the window shows it, folded from the session's events
/// and nothing else.
///
/// A pure reducer: `apply` takes each event as it arrives, and the user's own
/// acts (`userSent`, `answered`, `cancelling`) are applied the moment they
/// are made, so the window's whole behavior is a function of what was said
/// and done -- testable with no window and no binary. The answer shown is
/// exactly its deltas joined; states come from events, never timers; an event
/// this client does not know is a quiet row, never dropped.
public struct Transcript: Equatable {
    public enum Status: Equatable {
        /// Spawned; the session has not spoken.
        case connecting
        /// No turn open: the input is the user's.
        case ready
        /// The model is reasoning.
        case thinking(turn: Int)
        /// The turn is at work: a tool, another model, a pause between parts.
        case working(turn: Int)
        /// The answer is streaming.
        case answering(turn: Int)
        /// A question stands: the turn waits on the user.
        case waiting(turn: Int)
        /// A `cancel` was sent; the turn's result has not arrived.
        case stopping(turn: Int)
        /// Its child was ended to free it, while out of view; it resumes when
        /// it is back.
        case paused
        /// The session is over.
        case ended
    }

    public private(set) var items: [TranscriptItem] = []
    public private(set) var status: Status = .connecting
    /// The model the session named.
    public private(set) var model: String?
    /// The line types the session reads, as its `session` event announced
    /// them: what decides whether `detach`, `set` and `compact` are offered.
    public private(set) var accepts: [String] = []
    /// The session said it writes nothing (`"private": true`, the CLI's 52d).
    public private(set) var isPrivate = false
    /// Moves with every change: what a view watches to follow the end.
    public private(set) var revision = 0
    private var nextID = 0
    private var nextTurn = 1

    public init() {}

    /// Whether a turn may be sent now.
    public var acceptsInput: Bool { status == .ready }

    /// Whether there is a turn in flight that `cancel` would stop.
    public var turnInFlight: Bool {
        switch status {
        case .thinking, .working, .answering, .waiting: true
        case .connecting, .ready, .stopping, .paused, .ended: false
        }
    }

    // MARK: - The user's acts

    /// A turn was sent; returns its number.
    @discardableResult
    public mutating func userSent(_ text: String) -> Int {
        let turn = nextTurn
        nextTurn += 1
        append(.user(text), turn: turn)
        status = .working(turn: turn)
        return turn
    }

    /// The question or permission card `id` was answered.
    public mutating func answered(_ id: Int, with answers: [String]) {
        guard let index = items.firstIndex(where: { $0.id == id }) else { return }
        switch items[index].content {
        case .question(var card) where card.answers == nil:
            card.answers = answers
            items[index].content = .question(card)
        case .permission(var card) where card.answer == nil:
            card.answer = answers.first
            items[index].content = .permission(card)
        default:
            return
        }
        if case .waiting(let turn) = status {
            status = .working(turn: turn)
        }
        revision += 1
    }

    /// A `cancel` was sent for the turn in flight.
    public mutating func cancelling() {
        switch status {
        case .thinking(let turn), .working(let turn), .answering(let turn), .waiting(let turn):
            status = .stopping(turn: turn)
            revision += 1
        default:
            break
        }
    }

    /// The child was ended to free it: the conversation stays, quiet.
    public mutating func paused() {
        closeActivity()
        status = .paused
        revision += 1
    }

    /// A new child resumes the conversation: it waits for its `session` event.
    public mutating func reopening() {
        status = .connecting
        revision += 1
    }

    /// A line the app says of what it did -- an attach, a restart -- quiet,
    /// kept, as the session's own notices are.
    public mutating func note(_ text: String) {
        append(.notice(text), turn: nil)
    }

    /// A line could not be sent: the session had ended.
    public mutating func notSent(_ reason: String) {
        append(.error(reason), turn: nil)
    }

    // MARK: - The session's events

    public mutating func apply(_ event: SessionEvent) {
        switch event {
        case .event(let event):
            apply(event)
        case .ended(let end):
            closeActivity()
            append(.ended(Ending(reason: end.reason, message: end.message, details: end.stderrText)), turn: nil)
            status = .ended
        }
    }

    private mutating func apply(_ event: ProtocolEvent) {
        let turn = event.turn
        switch event.kind {
        case .session(let info):
            model = info.model
            nextTurn = info.nextTurn
            accepts = info.capabilities?.accepts ?? []
            isPrivate = info.isPrivate
            if status == .connecting {
                status = .ready
            }
            if items.isEmpty && info.nextTurn > 1 {
                // A resumed conversation: the CLI hands no read of its saved
                // turns to a driver, so say they are there rather than show
                // a conversation that seems to start here.
                let earlier = info.nextTurn - 1
                append(.notice(Self.resumed(earlier: earlier)), turn: nil)
            }
            revision += 1
        case .thinking(let budgetReached):
            updateActivity(turn) { activity in
                activity.live = true
                activity.budgetReached = activity.budgetReached || budgetReached
            }
            setStatus(.thinking(turn: turn ?? 0))
        case .thinkingDelta(let text):
            updateActivity(turn) { $0.reasoning += text }
        case .toolStatus(let text):
            updateActivity(turn) { $0.lines.append(text) }
            setStatus(.working(turn: turn ?? 0))
        case .memory(let chats, let decisions):
            let named = event.recalled
            if named.isEmpty {
                // A build before the CLI's 52a counts the chats, and names none.
                append(.notice(Self.recalled(chats: chats, decisions: decisions)), turn: turn)
            } else {
                append(.recalled(Recall(chats: named, decisions: decisions)), turn: turn)
            }
        case .sources(let sources):
            append(.sources(sources), turn: turn)
        case .notice(let text):
            append(.notice(text), turn: turn)
        case .todo(let todo):
            // Drawn in place of the last: it is the plan, not a log.
            if let index = items.lastIndex(where: { if case .todo = $0.content { true } else { false } }) {
                items[index].content = .todo(todo)
                revision += 1
            } else {
                append(.todo(todo), turn: turn)
            }
        case .answerStart:
            closeActivity()
            append(.answer(Answer(text: "", finished: false, usage: nil)), turn: turn)
            setStatus(.answering(turn: turn ?? 0))
        case .answerDelta(let text):
            if let index = openAnswer(turn) {
                if case .answer(var answer) = items[index].content {
                    answer.text += text
                    items[index].content = .answer(answer)
                    revision += 1
                }
            } else {
                closeActivity()
                append(.answer(Answer(text: text, finished: false, usage: nil)), turn: turn)
            }
            setStatus(.answering(turn: turn ?? 0))
        case .answerEnd:
            finishAnswers(turn, usage: nil)
            setStatus(.working(turn: turn ?? 0))
        case .result(let result):
            closeActivity()
            let answered = items.contains { item in
                if case .answer = item.content, item.turn == turn { true } else { false }
            }
            if !answered && !result.text.isEmpty {
                // A driver that dropped every delta still has the answer.
                append(.answer(Answer(text: result.text, finished: true, usage: nil)), turn: turn)
            }
            finishAnswers(turn, usage: result.usage)
            if result.wasCancelled {
                append(.stopped, turn: turn)
            }
            status = .ready
            revision += 1
        case .question(let question):
            closeActivity()
            if question.isPermission {
                append(.permission(PermissionCard(question: question, answer: nil)), turn: turn)
            } else {
                append(.question(QuestionCard(question: question, answers: nil)), turn: turn)
            }
            if case .stopping = status {} else { status = .waiting(turn: turn ?? 0) }
        case .error(let message):
            closeActivity()
            finishAnswers(turn, usage: nil)
            append(.error(message), turn: turn)
            status = .ready
            revision += 1
        case .task(let type), .unknown(let type):
            append(.unknown(type: type), turn: turn)
        case .unreadable:
            append(.unknown(type: "a line that is not JSON"), turn: turn)
        }
    }

    // MARK: - Folding

    private mutating func append(_ content: TranscriptItem.Content, turn: Int?) {
        items.append(TranscriptItem(id: nextID, turn: turn, content: content))
        nextID += 1
        revision += 1
    }

    /// The status, unless a `cancel` is on its way: then the turn is stopping
    /// until its result says it stopped.
    private mutating func setStatus(_ next: Status) {
        if case .stopping = status { return }
        if status == .ended { return }
        status = next
    }

    /// The turn's activity section: the last item, when it is this turn's;
    /// a new one otherwise, so the transcript stays in the order things happened.
    private mutating func updateActivity(_ turn: Int?, _ change: (inout Activity) -> Void) {
        if let last = items.indices.last, case .activity(var activity) = items[last].content, items[last].turn == turn {
            change(&activity)
            items[last].content = .activity(activity)
            revision += 1
        } else {
            var activity = Activity()
            activity.live = true
            change(&activity)
            append(.activity(activity), turn: turn)
        }
    }

    /// The live section closes: the answer started, a question stood, the turn ended.
    private mutating func closeActivity() {
        for index in items.indices {
            if case .activity(var activity) = items[index].content, activity.live {
                activity.live = false
                items[index].content = .activity(activity)
                revision += 1
            }
        }
    }

    private func openAnswer(_ turn: Int?) -> Int? {
        items.lastIndex { item in
            if case .answer(let answer) = item.content, item.turn == turn, !answer.finished { true } else { false }
        }
    }

    private mutating func finishAnswers(_ turn: Int?, usage: Usage?) {
        for index in items.indices where items[index].turn == turn {
            if case .answer(var answer) = items[index].content {
                answer.finished = true
                if let usage { answer.usage = usage }
                items[index].content = .answer(answer)
                revision += 1
            }
        }
    }

    static func resumed(earlier: Int) -> String {
        "resumed: \(earlier) earlier \(earlier == 1 ? "turn" : "turns") of this conversation \(earlier == 1 ? "isn't" : "aren't") shown here -- apogee keeps \(earlier == 1 ? "it" : "them"), and the model still has \(earlier == 1 ? "it" : "them")"
    }

    static func recalled(chats: Int, decisions: Int) -> String {
        func count(_ n: Int, _ one: String, _ many: String) -> String { "\(n) \(n == 1 ? one : many)" }
        let parts = [chats > 0 ? count(chats, "earlier chat", "earlier chats") : nil,
                     decisions > 0 ? count(decisions, "decision", "decisions") : nil].compactMap { $0 }
        return "recalled " + (parts.isEmpty ? "nothing" : parts.joined(separator: " and "))
    }
}

/// One row of the conversation.
public struct TranscriptItem: Equatable, Identifiable {
    public let id: Int
    /// The turn it belongs to; `nil` for what is said between turns.
    public let turn: Int?
    public var content: Content

    public enum Content: Equatable {
        /// What the user sent.
        case user(String)
        /// The turn's work: its reasoning and the lines of what it ran.
        case activity(Activity)
        case answer(Answer)
        /// A line the session said for the user, quiet, kept.
        case notice(String)
        /// The past chats a turn recalled, named (52a), each one openable.
        case recalled(Recall)
        /// The excerpts a turn's retrieval sent, numbered for citing (52a).
        case sources([Source])
        case question(QuestionCard)
        case permission(PermissionCard)
        /// The turn failed, in the CLI's own words.
        case error(String)
        /// The turn was cancelled.
        case stopped
        /// The session's plan, drawn in place.
        case todo([TodoItem])
        /// An event this client does not know: shown, quietly.
        case unknown(type: String)
        /// The session is over, and why.
        case ended(Ending)
    }
}

/// What a turn recalled: the chats by name, and how many decisions.
public struct Recall: Equatable {
    public let chats: [RecalledChat]
    public let decisions: Int

    /// "recalled 1 earlier chat" / "and 2 decisions".
    public var summary: String {
        let count = chats.count
        var text = "recalled \(count) earlier \(count == 1 ? "chat" : "chats")"
        if decisions > 0 { text += " and \(decisions) \(decisions == 1 ? "decision" : "decisions")" }
        return text
    }
}

/// A turn's thinking section.
public struct Activity: Equatable {
    /// The reasoning, as streamed.
    public var reasoning = ""
    /// Tool and helper lines, as the CLI wrote them.
    public var lines: [String] = []
    /// Open while the turn works; closed when the answer starts.
    public var live = false
    /// The reasoning was ended at its budget.
    public var budgetReached = false

    public init() {}
}

public struct Answer: Equatable {
    /// Exactly the deltas, joined.
    public var text: String
    public var finished: Bool
    /// The turn's tokens, when the provider reported them.
    public var usage: Usage?
}

public struct QuestionCard: Equatable {
    public let question: Question
    /// One per item, once answered; `nil` while the question stands.
    public var answers: [String]?
}

public struct PermissionCard: Equatable {
    public let question: Question
    /// `yes`, `session`, `always` or `no`, once answered.
    public var answer: String?
}

public struct Ending: Equatable {
    public let reason: SessionEnd.Reason
    /// One line, the child's own last words included.
    public let message: String
    /// The child's stderr tail.
    public let details: String
}
