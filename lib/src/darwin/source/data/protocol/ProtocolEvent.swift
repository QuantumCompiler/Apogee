/// One line the `apogee` child wrote on stdout, read.
///
/// The known events of machine-mode.md as typed cases; a task run's lifecycle
/// kept whole as `task`; everything else `unknown` -- and every event keeps
/// its every field in `fields`, the ones this client reads and the ones it
/// does not. Decoding never fails: a type this client has not heard of, a
/// field it does not know, even a line that is not JSON, all arrive as events
/// (rule 1 of the protocol: tolerate what is new).
public struct ProtocolEvent: Sendable, Equatable {
    public enum Kind: Sendable, Equatable {
        /// Once, first: the protocol, the model, what the session can do.
        case session(SessionInfo)
        /// The model began reasoning; again with `budgetReached` when the
        /// reasoning was ended at its budget.
        case thinking(budgetReached: Bool)
        /// A chunk of reasoning. Droppable.
        case thinkingDelta(String)
        /// What a turn was handed from earlier conversations.
        case memory(chats: Int, decisions: Int)
        /// A tool, or another model call, at work -- a line for display.
        case toolStatus(String)
        /// A line for the user that is neither progress nor an error.
        case notice(String)
        /// The session's todo, whole: drawn in place of the last.
        case todo([TodoItem])
        case answerStart
        /// A chunk of the answer. Concatenated in order.
        case answerDelta(String)
        case answerEnd
        /// The turn's end, carrying the whole answer.
        case result(TurnResult)
        /// The child waits for answers: `ask_user`, or the permission gate.
        case question(Question)
        /// The turn failed, in the CLI's own words.
        case error(message: String)
        /// A task run's lifecycle event (`task_started`, `task_round` ...),
        /// kept whole in `fields`; the type is the line's own.
        case task(type: String)
        /// A type this client does not know -- or a known type it could not
        /// read, a field it needs missing. Kept, never dropped.
        case unknown(type: String)
        /// A line that is not one JSON object with a type. stdout should never
        /// carry one; if it does, it is shown, not swallowed.
        case unreadable(line: String)
    }

    public let kind: Kind
    /// Every field the line carried, as it carried it.
    public let fields: [String: JSONValue]

    public init(kind: Kind, fields: [String: JSONValue]) {
        self.kind = kind
        self.fields = fields
    }

    /// The line's `type`.
    public var type: String { fields["type"]?.stringValue ?? "" }

    /// The turn the event belongs to: the number of the `user` line it answers.
    public var turn: Int? { fields["turn"]?.intValue }
}

/// The `session` event's facts.
public struct SessionInfo: Sendable, Equatable {
    public let protocolVersion: Int
    public let model: String
    /// The number the next `user` line will get.
    public let nextTurn: Int
    /// Absent from a binary that predates them.
    public let capabilities: Capabilities?

    public init(protocolVersion: Int, model: String, nextTurn: Int, capabilities: Capabilities?) {
        self.protocolVersion = protocolVersion
        self.model = model
        self.nextTurn = nextTurn
        self.capabilities = capabilities
    }
}

/// What a session can do, announced before its first turn.
public struct Capabilities: Sendable, Equatable {
    /// Every event type the build can write.
    public let events: [String]
    /// The line types the session reads.
    public let accepts: [String]
    /// Whether the model can call tools.
    public let tools: Bool
    /// Whether questions reach the driver.
    public let ask: Bool
    /// The vocabulary's version, a date.
    public let schema: String

    public init(events: [String], accepts: [String], tools: Bool, ask: Bool, schema: String) {
        self.events = events
        self.accepts = accepts
        self.tools = tools
        self.ask = ask
        self.schema = schema
    }
}

public struct TodoItem: Sendable, Equatable {
    public let text: String
    /// `pending`, `active` or `done` -- or a state a later build adds.
    public let state: String

    public init(text: String, state: String) {
        self.text = text
        self.state = state
    }
}

/// A turn's `result`.
public struct TurnResult: Sendable, Equatable {
    /// The whole answer.
    public let text: String
    public let model: String?
    /// `stop`, `cancelled` ... a string that may grow.
    public let finishReason: String?
    /// Absent when the provider reported none: absent is not zero.
    public let usage: Usage?

    public init(text: String, model: String?, finishReason: String?, usage: Usage?) {
        self.text = text
        self.model = model
        self.finishReason = finishReason
        self.usage = usage
    }

    public var wasCancelled: Bool { finishReason == "cancelled" }
}

public struct Usage: Sendable, Equatable {
    public let inputTokens: Int?
    public let outputTokens: Int?

    public init(inputTokens: Int?, outputTokens: Int?) {
        self.inputTokens = inputTokens
        self.outputTokens = outputTokens
    }
}

/// A `question` event: one or more questions, each answered by one `answer`
/// line, in order.
public struct Question: Sendable, Equatable {
    /// `permission` for the permission gate; absent for `ask_user`.
    public let kind: String?
    /// The tool asking, for a permission question.
    public let tool: String?
    /// What it would act on: a path, a command, a website.
    public let target: String?
    /// Whether answering yes sends data off the machine.
    public let outbound: Bool
    /// The whole of what would leave, for an outbound tool: show it.
    public let detail: String?
    public let items: [QuestionItem]

    public init(
        kind: String?, tool: String?, target: String?, outbound: Bool, detail: String?, items: [QuestionItem]
    ) {
        self.kind = kind
        self.tool = tool
        self.target = target
        self.outbound = outbound
        self.detail = detail
        self.items = items
    }

    public var isPermission: Bool { kind == "permission" }
}

public struct QuestionItem: Sendable, Equatable {
    public let header: String
    public let question: String
    public let multiSelect: Bool
    /// A convenience, not a constraint: free text is always accepted.
    public let options: [QuestionOption]

    public init(header: String, question: String, multiSelect: Bool, options: [QuestionOption]) {
        self.header = header
        self.question = question
        self.multiSelect = multiSelect
        self.options = options
    }
}

public struct QuestionOption: Sendable, Equatable {
    public let label: String
    public let description: String

    public init(label: String, description: String) {
        self.label = label
        self.description = description
    }
}
