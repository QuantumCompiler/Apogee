import BusinessReads
import BusinessSession
import DataProtocol
import Foundation

/// The conversation as the window shows it, folded from the session's events
/// and nothing else -- and, for a saved conversation opened again, from its
/// earlier turns as `chats show` read them back.
///
/// A pure reducer: `apply` takes each event as it arrives, and the user's own
/// acts (`userSent`, `answered`, `cancelling`) are applied the moment they
/// are made, so the window's whole behavior is a function of what was said
/// and done -- testable with no window and no binary. The answer shown is
/// exactly its deltas joined; states come from events, never timers; an event
/// this client does not know is a quiet row, never dropped.
///
/// A row is known by its range and its turn, never its turn alone: the turns
/// read back are one range, and each child that drives the conversation its
/// own, since the numbers a resumed child gives its turns can repeat ones
/// already drawn -- a binary before the CLI's 55b resumes a chat saved with
/// no count at turn 1, and a live turn that failed or was stopped leaves the
/// saved count behind the live numbers (55b's "not done").
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
    /// The range the live child's events fold into: 1 for the first child,
    /// one more for each that carries the conversation on.
    private var range = 1

    /// The range of the turns read back from the saved conversation.
    public static let earlierRange = 0

    public init() {}

    /// Whether a turn may be sent now.
    public var acceptsInput: Bool { status == .ready }

    /// The question or permission card that waits on the person now, by its
    /// id; `nil` when none does.
    public var standingCard: Int? {
        items.last { item in
            switch item.content {
            case .question(let card): card.isStanding
            case .permission(let card): card.isStanding
            default: false
            }
        }?.id
    }

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

    /// The question or permission card `id` was answered; whether it took
    /// the answer. One answered already, or closed -- its turn ended before
    /// it was answered -- takes none, so no answer line is sent for it.
    @discardableResult
    public mutating func answered(_ id: Int, with answers: [String]) -> Bool {
        guard let index = items.firstIndex(where: { $0.id == id }) else { return false }
        switch items[index].content {
        case .question(var card) where card.isStanding:
            card.answers = answers
            items[index].content = .question(card)
        case .permission(var card) where card.isStanding:
            card.answer = answers.first
            items[index].content = .permission(card)
        default:
            return false
        }
        if case .waiting(let turn) = status {
            status = .working(turn: turn)
        }
        revision += 1
        return true
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

    /// The child was ended to free it: the conversation stays, quiet -- a
    /// card it left standing closed, since its child can take no answer now.
    public mutating func paused() {
        closeActivity()
        closeStandingCards()
        status = .paused
        revision += 1
    }

    /// A new child resumes the conversation: it waits for its `session` event,
    /// and its events fold into a range of their own.
    public mutating func reopening() {
        closeStandingCards()
        range += 1
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

    // MARK: - The earlier turns

    /// A saved conversation's earlier turns, as `chats show` read them back
    /// (the CLI's M27), folded in as rows drawn as live turns are -- then the
    /// divider where the live session takes over. The session's own `next_turn`
    /// numbers the live turns on from them, and with rows drawn its `session`
    /// event says no "aren't shown here" notice.
    ///
    /// Only into a transcript with no rows yet: the window hands them over
    /// before the session's first event. One that has rows -- a conversation
    /// the app paused and carried on itself -- keeps its own, and takes none.
    /// A chat with no entries draws nothing, and the notice stands as before.
    ///
    /// What the read could not carry over heads them, in the CLI's words:
    /// the document's `warnings` (the CLI's 55b), or -- from a binary whose
    /// document has none -- what the read said on stderr, `stderr`.
    ///
    /// They are a range of their own, `earlierRange`: no live event is ever
    /// matched to them, whatever its number.
    public mutating func earlier(_ chat: SavedChat, stderr: [String] = []) {
        let warnings = chat.warnings.map { $0.map(\.message) } ?? stderr
        guard items.isEmpty, !chat.turns.isEmpty || !warnings.isEmpty else { return }
        let range = Self.earlierRange
        for warning in warnings {
            append(.warning(warning), turn: nil, range: range)
        }
        if !chat.attachments.isEmpty {
            append(.notice("attached to the chat: " + Self.named(chat.attachments)), turn: nil, range: range)
        }
        for entry in chat.turns {
            if let summary = entry.compaction {
                append(.notice(Self.compacted(summary)), turn: entry.turn, range: range)
            } else {
                if !entry.text.isEmpty { append(.user(entry.text), turn: entry.turn, range: range) }
                if !entry.attachments.isEmpty {
                    append(.notice("attached: " + Self.named(entry.attachments)), turn: entry.turn, range: range)
                }
            }
            if !entry.tools.isEmpty {
                var activity = Activity()
                activity.lines = entry.tools.map(Self.line)
                activity.typed = entry.isCompaction ? nil : entry.text
                append(.activity(activity), turn: entry.turn, range: range)
            }
            if !entry.answer.isEmpty {
                append(.answer(Answer(text: entry.answer, finished: true, usage: nil)), turn: entry.turn, range: range)
            }
            if !entry.answered && !entry.isCompaction {
                append(.unanswered, turn: entry.turn, range: range)
            }
        }
        if !chat.turns.isEmpty {
            append(.divider(Self.earlierDivider), turn: nil, range: range)
        }
    }

    /// The divider's words, where the live session takes over from the turns
    /// read back.
    public static let earlierDivider = "earlier in this conversation"

    /// A compaction where it happened, in the words of `chats show`'s own text.
    static func compacted(_ summary: String) -> String {
        "history compacted; what came before is this summary:\n\(summary)"
    }

    /// A tool a turn called, as the live line names it (`[tool] read_file`),
    /// with its arguments and the size of what it returned.
    static func line(_ call: SavedToolCall) -> String {
        var line = "[tool] \(call.name)"
        if !call.summary.isEmpty { line += " \(call.summary)" }
        if let bytes = call.resultBytes { line += " -- returned \(size(bytes))" }
        return line
    }

    /// Attachments by name, each with its files and size where the read says.
    static func named(_ attachments: [SavedAttachment]) -> String {
        attachments.map { attachment in
            let facts = [
                attachment.files.map { "\($0) \($0 == 1 ? "file" : "files")" },
                attachment.bytes.map(size),
            ].compactMap { $0 }
            return facts.isEmpty ? attachment.name : "\(attachment.name) (\(facts.joined(separator: ", ")))"
        }.joined(separator: ", ")
    }

    static func size(_ bytes: Int) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(bytes), countStyle: .file)
    }

    // MARK: - The session's events

    public mutating func apply(_ event: SessionEvent) {
        switch event {
        case .event(let event):
            apply(event)
        case .ended(let end):
            closeActivity()
            closeStandingCards()
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
                // A resumed conversation whose turns were not read back (a
                // binary before the CLI's M27 has no `chats show`): say they
                // are there rather than show a conversation that seems to
                // start here.
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
            // A card the turn left standing -- a stop while it asked -- can
            // take no answer now: the CLI drops one sent after the turn.
            closeStandingCards()
            let answered = items.contains { item in
                if case .answer = item.content, isLive(item, turn) { true } else { false }
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
            closeStandingCards()
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

    private mutating func append(_ content: TranscriptItem.Content, turn: Int?, range: Int? = nil) {
        items.append(TranscriptItem(id: nextID, turn: turn, range: range ?? self.range, content: content))
        nextID += 1
        revision += 1
    }

    /// Whether `item` is the live child's, of `turn`: what every live event
    /// is matched by -- its range and its turn, never the turn alone.
    private func isLive(_ item: TranscriptItem, _ turn: Int?) -> Bool {
        item.range == range && item.turn == turn
    }

    /// Every card still standing is closed: its turn ended, or its child did,
    /// before it was answered.
    private mutating func closeStandingCards() {
        for index in items.indices {
            switch items[index].content {
            case .question(var card) where card.isStanding:
                card.closed = true
                items[index].content = .question(card)
                revision += 1
            case .permission(var card) where card.isStanding:
                card.closed = true
                items[index].content = .permission(card)
                revision += 1
            default:
                break
            }
        }
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
        if let last = items.indices.last, case .activity(var activity) = items[last].content, isLive(items[last], turn) {
            change(&activity)
            items[last].content = .activity(activity)
            revision += 1
        } else {
            var activity = Activity()
            activity.live = true
            activity.typed = typed(turn)
            change(&activity)
            append(.activity(activity), turn: turn)
        }
    }

    /// What the person sent as `turn`.
    private func typed(_ turn: Int?) -> String? {
        guard let turn else { return nil }
        for item in items.reversed() where isLive(item, turn) {
            if case .user(let text) = item.content { return text }
        }
        return nil
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
            if case .answer(let answer) = item.content, isLive(item, turn), !answer.finished { true } else { false }
        }
    }

    private mutating func finishAnswers(_ turn: Int?, usage: Usage?) {
        for index in items.indices where isLive(items[index], turn) {
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
    /// The stretch of the conversation it belongs to: `Transcript.earlierRange`
    /// for the turns read back, else the child that said it, numbered from 1.
    /// With `turn`, what a live event is matched by.
    public let range: Int
    public var content: Content

    public enum Content: Equatable {
        /// What the user sent.
        case user(String)
        /// The turn's work: its reasoning and the lines of what it ran.
        case activity(Activity)
        case answer(Answer)
        /// A line the session said for the user, quiet, kept.
        case notice(String)
        /// What reading the saved conversation could not carry over, in the
        /// CLI's words: a gap in the turns that follow.
        case warning(String)
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
        /// A turn read back from the saved conversation with no answer kept:
        /// cancelled or failed, which the history does not say (`chats show`'s
        /// `answered: false`).
        case unanswered
        /// Where the live session takes over from the turns read back: the
        /// thin divider, and its words.
        case divider(String)
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
    /// What the turn was sent as -- a `/play` names its symphony there.
    public var typed: String?

    public init() {}

    /// The symphony the lines narrate being played, when they narrate one.
    public var play: PlayNarration? { PlayNarration(lines: lines, typed: typed) }
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
    /// Its turn ended -- stopped or failed -- or its child did, before it was
    /// answered: it takes no answer now (the CLI drops one sent after).
    public var closed = false

    /// Whether it waits on the person: not answered, not closed.
    public var isStanding: Bool { answers == nil && !closed }
}

public struct PermissionCard: Equatable {
    public let question: Question
    /// `yes`, `session`, `always` or `no`, once answered.
    public var answer: String?
    /// Its turn ended, or its child did, before it was answered: it takes no
    /// answer now.
    public var closed = false

    /// Whether it waits on the person: not answered, not closed.
    public var isStanding: Bool { answer == nil && !closed }
}

public struct Ending: Equatable {
    public let reason: SessionEnd.Reason
    /// One line, the child's own last words included.
    public let message: String
    /// The child's stderr tail.
    public let details: String
}
