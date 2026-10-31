import DataProtocol
import Foundation

/// A saved conversation's turns, as `chats show <id> --output-format json`
/// reads them (the CLI's M27): what a session that resumes it draws above
/// its live turns.
///
/// Read tolerantly, as the protocol grows by addition (the CLI's ADR 0006):
/// a field this build does not know is ignored, and one an older or newer
/// binary leaves out reads as empty -- never a failed read. Nothing the
/// history never held is here: no thinking, no injected context, no tool's
/// result, no attachment's bytes (machine-mode.md -> Everything else is a CLI
/// command).
public struct SavedChat: Sendable, Equatable {
    /// What `--resume` and every `chats` verb take.
    public let id: String
    /// As `chats list`'s row names it; the id when it has no name.
    public let name: String
    /// The generated title; `nil` when it has none.
    public let title: String?
    /// The backend it runs on and the roster model it pinned there, as
    /// `chats list`'s row says them; `nil` when empty.
    public let backend: String?
    public let model: String?
    /// Every attachment the chat holds, in the order attached -- one
    /// retrieved rather than inlined, which rides no message, included.
    public let attachments: [SavedAttachment]
    /// The whole conversation in order: its turns, and its compactions where
    /// they happened.
    public let turns: [SavedTurn]
    /// What the loader could not read, as the document says it (the CLI's
    /// 55b, `[]` when it read everything): a turn missing from `turns` is
    /// said here, never only on stderr. `nil` from a binary before 55b, whose
    /// document has no `warnings` and which says them on stderr alone.
    public let warnings: [SavedWarning]?

    public init(
        id: String, name: String, title: String?, backend: String?, model: String?, attachments: [SavedAttachment],
        turns: [SavedTurn], warnings: [SavedWarning]? = nil
    ) {
        self.id = id
        self.name = name
        self.title = title
        self.backend = backend
        self.model = model
        self.attachments = attachments
        self.turns = turns
        self.warnings = warnings
    }

    /// The chat a `chats show` document reads as; `nil` when the document is
    /// none -- another read's (`object` not `chat`), or no `turns` list.
    public init?(document: ReadDocument) {
        if let object = document.object, object != "chat" { return nil }
        guard let turns = document.value["turns"]?.arrayValue else { return nil }
        let id = document.value["id"]?.stringValue ?? ""
        self.id = id
        name = Self.nonEmpty(document.value["name"]) ?? id
        title = Self.nonEmpty(document.value["title"])
        backend = Self.nonEmpty(document.value["backend"])
        model = Self.nonEmpty(document.value["model"])
        attachments = SavedAttachment.list(document.value["attachments"])
        self.turns = turns.compactMap(SavedTurn.init(entry:))
        warnings = document.value["warnings"]?.arrayValue.map { $0.compactMap(SavedWarning.init(entry:)) }
    }

    /// The arguments of the read after `apogee`, for the chat `id`.
    public static func arguments(id: String) -> [String] {
        ["chats", "show", id]
    }

    static func nonEmpty(_ value: JSONValue?) -> String? {
        value?.stringValue.flatMap { $0.isEmpty ? nil : $0 }
    }
}

/// One thing `chats show`'s loader could not read (the CLI's 55b): its kind
/// -- `field_dropped` -- and the CLI's sentence for it.
public struct SavedWarning: Sendable, Equatable {
    public let kind: String
    public let message: String

    public init(kind: String, message: String) {
        self.kind = kind
        self.message = message
    }

    /// An entry of the document's `warnings`; one with no message is skipped.
    init?(entry: JSONValue) {
        guard let message = entry["message"]?.stringValue, !message.isEmpty else { return nil }
        self.init(kind: entry["kind"]?.stringValue ?? "", message: message)
    }
}

/// One entry of a saved conversation: a turn -- a user message and
/// everything after it up to the next -- or a compaction, where it happened.
public struct SavedTurn: Sendable, Equatable {
    /// The conversation's turn count once the entry ended: an answered
    /// turn's is the number its `user` line was given; an unanswered turn and
    /// a compaction carry the one before. `nil` when the document leaves it out.
    public let turn: Int?
    /// The user's words; empty on a compaction.
    public let text: String
    /// What rode the message, by name, never its bytes.
    public let attachments: [SavedAttachment]
    /// The model's words in the turn, every message's joined; on a
    /// compaction, the last answer it kept.
    public let answer: String
    /// Each tool the turn called, in order.
    public let tools: [SavedToolCall]
    /// Whether the history holds the model's answer: `false` for a turn
    /// cancelled or failed, and a compaction that kept none.
    public let answered: Bool
    /// On a compaction alone: the summary that stands in for the turns
    /// before it.
    public let compaction: String?

    public init(
        turn: Int?, text: String, attachments: [SavedAttachment], answer: String, tools: [SavedToolCall],
        answered: Bool, compaction: String?
    ) {
        self.turn = turn
        self.text = text
        self.attachments = attachments
        self.answer = answer
        self.tools = tools
        self.answered = answered
        self.compaction = compaction
    }

    /// An entry of the document's `turns`; `nil` for one that is not an
    /// object. `answered`, which the CLI writes on every entry, reads -- if a
    /// binary ever leaves it out -- as whether the entry holds any answer.
    init?(entry: JSONValue) {
        guard entry.objectValue != nil else { return nil }
        let user = entry["user"]
        let answer = entry["answer"]
        let text = answer?["text"]?.stringValue ?? ""
        let tools = (answer?["tools"]?.arrayValue ?? []).compactMap(SavedToolCall.init(entry:))
        self.init(
            turn: entry["turn"]?.intValue, text: user?["text"]?.stringValue ?? "",
            attachments: SavedAttachment.list(user?["attachments"]), answer: text, tools: tools,
            answered: entry["answered"]?.boolValue ?? !(text.isEmpty && tools.isEmpty),
            compaction: entry["compacted"].flatMap { $0.objectValue != nil ? ($0["summary"]?.stringValue ?? "") : nil })
    }

    /// Whether it is a compaction rather than a turn.
    public var isCompaction: Bool { compaction != nil }
}

/// An attachment, named: media a message carries by its media type or URL,
/// or what the chat attached, with how many files and their size.
public struct SavedAttachment: Sendable, Equatable {
    public let name: String
    /// `nil` where the document does not say.
    public let files: Int?
    /// `nil` where the history does not say (an image named by URL).
    public let bytes: Int?

    public init(name: String, files: Int?, bytes: Int?) {
        self.name = name
        self.files = files
        self.bytes = bytes
    }

    /// The attachments a list names; an entry with no name is skipped.
    static func list(_ value: JSONValue?) -> [SavedAttachment] {
        (value?.arrayValue ?? []).compactMap { entry in
            guard let name = entry["name"]?.stringValue, !name.isEmpty else { return nil }
            return SavedAttachment(name: name, files: entry["files"]?.intValue, bytes: entry["bytes"]?.intValue)
        }
    }
}

/// A tool a turn called: its arguments in one line, and the size of what it
/// returned -- never the result itself.
public struct SavedToolCall: Sendable, Equatable {
    public let name: String
    /// Its arguments, as compact JSON clipped to one line.
    public let summary: String
    /// `nil` when the history keeps no result.
    public let resultBytes: Int?

    public init(name: String, summary: String, resultBytes: Int?) {
        self.name = name
        self.summary = summary
        self.resultBytes = resultBytes
    }

    /// A call of the document's `tools`; one with no name is skipped.
    init?(entry: JSONValue) {
        guard let name = entry["name"]?.stringValue, !name.isEmpty else { return nil }
        self.init(name: name, summary: entry["summary"]?.stringValue ?? "", resultBytes: entry["result_bytes"]?.intValue)
    }
}
