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
