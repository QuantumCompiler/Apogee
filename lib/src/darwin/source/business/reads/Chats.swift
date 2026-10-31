import DataProtocol
import Foundation

/// One saved conversation, as `chats list --output-format json` lists it.
public struct ChatSummary: Sendable, Equatable, Identifiable {
    /// What `--resume` and every `chats` verb take.
    public let id: String
    public let name: String
    /// When it last changed; `nil` when the CLI's stamp did not parse.
    public let updated: Date?
    public let turns: Int
    /// The backend it runs on and the roster model it pinned there -- since
    /// the CLI's 52a; `nil` from a build before it (the design's G8 fallback).
    public let backend: String?
    public let model: String?

    public init(id: String, name: String, updated: Date?, turns: Int, backend: String? = nil, model: String? = nil) {
        self.id = id
        self.name = name
        self.updated = updated
        self.turns = turns
        self.backend = backend
        self.model = model
    }

    /// The rows of a `chats list` document, newest first as listed; a row
    /// without an id is skipped, never guessed.
    public static func list(from document: ReadDocument) -> [ChatSummary] {
        (document.entries ?? []).compactMap { entry in
            guard let id = entry["id"]?.stringValue, !id.isEmpty else { return nil }
            let name = entry["name"]?.stringValue.flatMap { $0.isEmpty ? nil : $0 } ?? id
            return ChatSummary(
                id: id, name: name, updated: entry["updated"]?.stringValue.flatMap(Self.date),
                turns: entry["turns"]?.intValue ?? 0,
                backend: entry["backend"]?.stringValue.flatMap { $0.isEmpty ? nil : $0 },
                model: entry["model"]?.stringValue.flatMap { $0.isEmpty ? nil : $0 })
        }
    }

    /// `2026-10-21T00:38:43Z`.
    static func date(_ text: String) -> Date? {
        try? Date(text, strategy: .iso8601)
    }
}

/// A `label: value` record -- `models status`, `models info` -- as its
/// document's `fields` carry it, in order, each label as printed.
public struct Field: Sendable, Equatable {
    public let label: String
    public let value: String

    public init(_ label: String, _ value: String) {
        self.label = label
        self.value = value
    }

    public static func list(from document: ReadDocument) -> [Field] {
        (document.value["fields"]?.arrayValue ?? []).compactMap { entry in
            guard let label = entry["field"]?.stringValue else { return nil }
            return Field(label, entry["value"]?.stringValue ?? "")
        }
    }

    /// A record a verb prints as text alone, one `label: value` a line --
    /// `chats info` (GUI 4k), which takes no `--output-format`: each line
    /// split at its first colon, both sides trimmed, in order. A line with
    /// no colon, or nothing before it, is no field.
    public static func lines(_ text: String) -> [Field] {
        text.split(whereSeparator: \.isNewline).compactMap { line in
            guard let colon = line.firstIndex(of: ":") else { return nil }
            let label = line[..<colon].trimmingCharacters(in: .whitespaces)
            guard !label.isEmpty else { return nil }
            return Field(label, line[line.index(after: colon)...].trimmingCharacters(in: .whitespaces))
        }
    }
}

/// Where a model runs, as the design marks it: on this machine, a cloud API
/// billed per use, or a vendor's app on a subscription.
public enum ModelKind: String, Sendable, Equatable {
    case local
    case api
    case subscription

    /// The kind a backend's `type` is; `nil` for one that is none of them
    /// (the mock backend) or a type this build does not know -- never guessed.
    public init?(backendType: String) {
        switch backendType {
        case "llamacpp", "mlx", "ollama-cli": self = .local
        case "anthropic", "openai", "google": self = .api
        case "claude-cli", "codex-cli", "gemini-cli": self = .subscription
        default: return nil
        }
    }

    /// Each configured backend's kind, from a `models list` document's rows.
    public static func byBackend(from document: ReadDocument) -> [String: ModelKind] {
        var kinds: [String: ModelKind] = [:]
        for entry in document.entries ?? [] {
            if let backend = entry["backend"]?.stringValue, let type = entry["backend_type"]?.stringValue,
                let kind = ModelKind(backendType: type)
            {
                kinds[backend] = kind
            }
        }
        return kinds
    }
}

/// One saved conversation's details, as `chats info <id> --output-format json`
/// types them since the CLI's M31 (`object: "chat.info"`): `chats list`'s row,
/// then every fact the text prints beside it -- the text `chats info` printed
/// alone before, which a binary without the document still gets read as
/// (`Field.lines`).
public struct ChatDetails: Sendable, Equatable {
    /// What `--resume` and every `chats` verb take.
    public let id: String
    /// The name it is listed by: the one `chats title` gave it, else its title,
    /// else its id.
    public let name: String
    /// The name `chats title` or `/title` gave it; empty for none.
    public let customName: String
    /// Its generated title; empty for none.
    public let title: String
    /// The backend it runs on; empty for the default -- the text's `(default)`.
    public let backend: String
    /// The roster model it pinned there; empty for the backend's own.
    public let model: String
    public let started: String
    public let updated: String
    public let turns: Int
    /// How many messages its history holds.
    public let messages: Int
    /// How many times its history was folded into a summary.
    public let compactions: Int
    /// Its settings, `nil` where unset -- the lines the text leaves out.
    public let temperature: Double?
    public let maxTokens: Int?
    /// The session file's schema, `0` for one written before the schema
    /// existed -- the text's `legacy`; `nil` where the document says none.
    public let schemaVersion: Int?

    /// The `chat.info` document's facts; `nil` for any other document.
    public init?(document: ReadDocument) {
        let v = document.value
        guard document.object == "chat.info", let id = v["id"]?.stringValue, !id.isEmpty else { return nil }
        self.id = id
        name = v["name"]?.stringValue.flatMap { $0.isEmpty ? nil : $0 } ?? id
        customName = v["custom_name"]?.stringValue ?? ""
        title = v["title"]?.stringValue ?? ""
        backend = v["backend"]?.stringValue ?? ""
        model = v["model"]?.stringValue ?? ""
        started = v["started"]?.stringValue ?? ""
        updated = v["updated"]?.stringValue ?? ""
        turns = v["turns"]?.intValue ?? 0
        messages = v["messages"]?.intValue ?? 0
        compactions = v["compactions"]?.intValue ?? 0
        temperature = if case .number(let n)? = v["temperature"] { n } else { nil }
        maxTokens = v["max_tokens"]?.intValue
        schemaVersion = v["schema_version"]?.intValue
    }

    /// The facts as the details sheet lists them, typed: under the text's
    /// own labels where the text says the same fact -- so the two readings of
    /// one chat agree line for line -- then what only the document states,
    /// the pinned model and that it was never compacted.
    public var fields: [Field] {
        var fields = [Field("id", id)]
        if !customName.isEmpty { fields.append(Field("name", customName)) }
        if !title.isEmpty { fields.append(Field("title", title)) }
        fields.append(Field("backend", backend.isEmpty ? "(default)" : backend))
        if !model.isEmpty { fields.append(Field("model", model)) }
        fields += [
            Field("started", started), Field("updated", updated), Field("turns", String(turns)),
            Field("messages", String(messages)),
            Field("compacted", compactions > 0 ? "\(compactions) time(s)" : "never"),
        ]
        if let temperature {
            fields.append(Field("temp", temperature.rounded() == temperature ? String(Int(temperature)) : String(temperature)))
        }
        if let maxTokens { fields.append(Field("max_tok", String(maxTokens))) }
        if schemaVersion == 0 { fields.append(Field("schema", "legacy (pre-versioning)")) }
        return fields
    }
}

/// What recall holds of earlier chats, as `chats recall --output-format json`
/// lists it since the CLI's M31: each summary itself -- what Forget erases --
/// newest first, with the count line's other facts. Before it, the read printed
/// the count line alone (`countLine(in:)`).
public struct RecallListing: Sendable, Equatable {
    /// One chat's summary, as the index recorded it when it summarised the
    /// chat.
    public struct Summary: Sendable, Equatable, Identifiable {
        /// The chat it summarises: what `chats show` and `--resume` take.
        public let chat: String
        /// The name it was recorded under -- what the `memory` event names it
        /// by; an older entry may leave it empty.
        public let name: String
        /// When the index summarised it; `nil` undated.
        public let updated: Date?
        /// The text the model is handed, whole.
        public let summary: String
        public var id: String { chat }

        /// What a person knows it by: its name, else its chat's id.
        public var title: String { name.isEmpty ? chat : name }
    }

    public let summaries: [Summary]
    /// The index's path, as the CLI names it.
    public let index: String
    /// How many chats are marked due a summary and not yet summarised.
    public let pending: Int

    /// The listing's facts; `nil` for any other document.
    public init?(document: ReadDocument) {
        let v = document.value
        guard document.object == "list", let data = document.entries, let index = v["index"]?.stringValue else { return nil }
        summaries = data.compactMap { entry in
            guard let chat = entry["chat"]?.stringValue, !chat.isEmpty else { return nil }
            return Summary(
                chat: chat, name: entry["name"]?.stringValue ?? "",
                updated: entry["updated"]?.stringValue.flatMap(ChatSummary.date),
                summary: entry["summary"]?.stringValue ?? "")
        }
        self.index = index
        pending = v["pending"]?.intValue ?? 0
    }

    /// The count line, as the text says it: `recall holds 3 chat summaries in
    /// <index>`, and how many more are due one.
    public var countLine: String {
        "recall holds \(summaries.count) chat summar\(summaries.count == 1 ? "y" : "ies") in \(index)"
            + (pending == 0 ? "" : ", \(pending) more due one")
    }

    /// Whether Forget has anything to erase: the CLI says "nothing to
    /// forget" otherwise.
    public var holdsAny: Bool { !summaries.isEmpty || pending > 0 }

    /// How many Forget erases and which, for its question: `3 chat
    /// summaries: “Launch Emails”, “Dev Server Port” and “Release
    /// Checklist”`, and those still due one.
    public var forgetting: String {
        let titles = summaries.map { "“\($0.title)”" }
        let named: String
        switch titles.count {
        case 0: named = ""
        case 1: named = ": " + titles[0]
        default: named = ": " + titles.dropLast().joined(separator: ", ") + " and " + titles[titles.count - 1]
        }
        let count = "\(summaries.count) chat summar\(summaries.count == 1 ? "y" : "ies")"
        return count + named + (pending == 0 ? "" : ", and \(pending) more due one")
    }

    /// The count line in what `chats recall` printed: its line that begins
    /// `recall holds` -- an M31 binary lists a line per summary after it --
    /// else its last line; `nil` for nothing printed.
    public static func countLine(in text: String) -> String? {
        let lines = text.split(whereSeparator: \.isNewline).map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
        return lines.first { $0.hasPrefix("recall holds") } ?? lines.last
    }
}
