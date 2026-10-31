import DataProtocol
import Foundation

/// One row of `models list --output-format json`: a configured backend and
/// its model, a stored pipeline, a stored voice model.
public struct ModelRow: Sendable, Equatable, Identifiable {
    /// The backend's name -- what `models info`, `-m` and the role verbs take.
    public let backend: String
    public let backendType: String
    public let model: String
    /// The roles it holds, as the CLI writes them (`chat, embedding`).
    public let roles: String
    public let source: String
    public let architecture: String
    public let state: String
    public let verified: String
    /// `local` or `cloud`, and the table's group in it (`gguf`, `anthropic` ...).
    public let section: String
    public let group: String
    public let note: String?
    public let format: String?
    public let quant: String?
    /// The window, in the CLI's words where the row states one (an MLX model's).
    public let window: String?
    /// A pipeline's components; a pipeline's or voice model's bytes on disk.
    public let components: Int?
    public let bytes: Int?
    /// What an MLX directory is, as the row's `kind` states it (the CLI's
    /// 53e): stored MLX weights and an `mlx` backend over them carry it,
    /// every other row none -- and so does every row of a binary before 53e,
    /// whose MLX sets read as chat models' (`MLXKind.reading`). Named apart
    /// from `kind`, which is where a backend runs.
    public let mlxKind: MLXKind?
    /// The drafter an `mlx` entry names, as its row states it (the CLI's
    /// M32): absent when none is paired -- and on every row of a binary
    /// before M32, which states none (`statesDrafts` tells the two apart).
    public let draft: Draft?

    /// An `mlx` entry's drafter, as `models list` states it (the CLI's M32,
    /// the check `models info`'s `draft:` line makes): its directory as the
    /// config holds it, written absolute, whether it drafts beside the entry,
    /// and, when it can't, why, in the CLI's words.
    public struct Draft: Sendable, Equatable {
        public let path: String
        public let canDraft: Bool
        /// Why it cannot draft there -- its drafter gone, mlx-lm's build of
        /// the base, a base of another width -- with the way out it names;
        /// `nil` where it drafts.
        public let refusal: String?

        public init(path: String, canDraft: Bool, refusal: String?) {
            self.path = path
            self.canDraft = canDraft
            self.refusal = refusal
        }
    }

    /// The vocabulary's date from which `models list` states each `mlx`
    /// row's drafter (the CLI's M32). A development build between the CLI's
    /// M29 and M32 announces this date too without the field: there a paired
    /// base reads as unpaired, and no release is affected.
    public static let draftsStatedSince = "2026-10-26"

    /// Whether `rows` -- one `models list` -- states each `mlx` entry's
    /// drafter, so a row without `draft` has none paired: a row carries one
    /// (the field's presence), or the binary announcing `vocabulary` is
    /// dated M32's or later. Otherwise -- a binary before M32, or one that
    /// hasn't said -- a row without it says nothing either way, and its
    /// drafter is read from its `models info` (GUI M5).
    public static func statesDrafts(_ rows: [ModelRow], vocabulary: String?) -> Bool {
        rows.contains { $0.draft != nil } || (vocabulary ?? "") >= draftsStatedSince
    }

    /// The backend `models list` writes for stored weights no backend points
    /// at -- its table's placeholder, with `-` as their type.
    public static let unconfigured = "(not configured)"

    /// Stored weights no backend points at -- a SafeTensors set, an MLX
    /// model, a stored GGUF, a pipeline, a voice model -- named by their store
    /// handle (`<model>/<format>/<id>`), which `models list` gives as `model`.
    public var isStored: Bool { backend == Self.unconfigured && backendType == "-" }

    /// The name the CLI's verbs take: a configured row's backend, stored
    /// weights' handle. Unique in a list, as a backend is in the config and a
    /// handle in the store.
    public var name: String { isStored ? model : backend }

    public var id: String { name }

    /// Where it runs, from its backend's type; `nil` when that type is none
    /// of the three (the mock backend) or unknown.
    public var kind: ModelKind? { ModelKind(backendType: backendType) }

    /// Whether a chat runs on it -- the one answer every place that offers a
    /// backend to chat with or to train asks (the GUI's M4). The binary's
    /// word decides, never a name: an MLX row of kind `chat` or `vision`
    /// chats, one of any other kind -- `embedding`, `drafter`, `video`,
    /// `voice`, or a word this app does not know -- does not, as the CLI
    /// refuses its chat (its 53e, 53h). A row with no kind -- a non-MLX
    /// backend, and every row of a binary before 53e -- chats, as every row
    /// did before kinds existed.
    public var chats: Bool { mlxKind?.chats ?? true }

    /// The backends a chat, or a role that chats -- a teacher, a judge -- can
    /// be offered: each configured backend that `chats`, once, in the list's
    /// order. Stored weights are no backend (the list's placeholder is not
    /// one). The places that show backends -- the Backends tab, Roles,
    /// Settings -- keep every entry whatever its kind: nothing the CLI lists
    /// is hidden, only the choices narrow (Roles' chat role asks `chats` too).
    public static func chatBackends(_ rows: [ModelRow]) -> [String] {
        var names: [String] = []
        for row in rows where !row.isStored && row.chats && !names.contains(row.backend) {
            names.append(row.backend)
        }
        return names
    }

    /// How many stored sets `models list` folded away -- those a conversion
    /// consumed -- which `models list --all` lists.
    public static func folded(in document: ReadDocument) -> Int {
        document.value["folded"]?.intValue ?? 0
    }

    /// Every row of a `models list` document, in its order; one with no name
    /// -- no backend, or stored weights with no handle -- is skipped.
    public static func list(from document: ReadDocument) -> [ModelRow] {
        (document.entries ?? []).compactMap { entry in
            func text(_ key: String) -> String { entry[key]?.stringValue ?? "" }
            func optional(_ key: String) -> String? { entry[key]?.stringValue.flatMap { $0.isEmpty ? nil : $0 } }
            let backend = text("backend")
            guard !backend.isEmpty else { return nil }
            let row = ModelRow(
                backend: backend, backendType: text("backend_type"), model: text("model"), roles: text("roles"),
                source: text("source"), architecture: text("architecture"), state: text("state"),
                verified: text("verified"), section: optional("section") ?? "local", group: optional("group") ?? "other",
                note: optional("note"), format: optional("format"), quant: optional("quant"), window: optional("window"),
                components: entry["components"]?.intValue, bytes: entry["bytes"]?.intValue,
                mlxKind: optional("kind").map(MLXKind.init(word:)), draft: Self.draft(entry["draft"]))
            return row.name.isEmpty ? nil : row
        }
    }

    /// A row's `draft`, where it states one with its path; any other shape
    /// says nothing.
    static func draft(_ value: JSONValue?) -> Draft? {
        guard let value, let path = value["path"]?.stringValue, !path.isEmpty else { return nil }
        let refusal = value["refusal"]?.stringValue.flatMap { $0.isEmpty ? nil : $0 }
        return Draft(path: path, canDraft: value["can_draft"]?.boolValue ?? (refusal == nil), refusal: refusal)
    }

    /// The name `models rename` takes: stored weights' directory -- the
    /// handle's first part; else its backends' name without the level --
    /// `qwen3-8b-Q4KM` is `qwen3-8b` -- when the backend ends in its own
    /// quantization; else the backend's name as it is.
    public var renameName: String {
        if isStored { return model.split(separator: "/").first.map(String.init) ?? model }
        guard let quant, !quant.isEmpty else { return backend }
        let level = "-" + quant.replacingOccurrences(of: "_", with: "")
        if backend.lowercased().hasSuffix(level.lowercased()) {
            return String(backend.dropLast(level.count))
        }
        return backend
    }
}

/// What an MLX directory is: the word the CLI's one reader decides (its 53e)
/// -- the directory's own record, else its files -- and states as `models
/// list`'s `kind` and `models info`'s `data.kind`. The six words the CLI
/// makes today, and any other carried exactly as written: a later CLI's kind
/// is shown, never guessed at (the protocol grows additively, ADR 0006).
public enum MLXKind: Sendable, Equatable, Hashable {
    /// A language model: an `mlx` backend chats with it (mlx-lm's build).
    case chat
    /// A vision model, its towers kept (mlx-vlm's build): it chats, and reads
    /// images.
    case vision
    /// An embedding model (mlx-embeddings' build): an `mlx` entry the
    /// embedding role names embeds with it, and answers no chat (53h).
    case embedding
    /// A drafter: it runs beside its base model as that backend's draft,
    /// never alone (53i).
    case drafter
    /// A video model: `video generate` runs it (53k, 53l).
    case video
    /// A voice model: `voice say` speaks with it (53m).
    case voice
    /// A word this app does not know, as the CLI wrote it.
    case other(String)

    public init(word: String) {
        switch word {
        case "chat": self = .chat
        case "vision": self = .vision
        case "embedding": self = .embedding
        case "drafter": self = .drafter
        case "video": self = .video
        case "voice": self = .voice
        default: self = .other(word)
        }
    }

    /// The word, as the CLI writes it.
    public var word: String {
        switch self {
        case .chat: "chat"
        case .vision: "vision"
        case .embedding: "embedding"
        case .drafter: "drafter"
        case .video: "video"
        case .voice: "voice"
        case .other(let word): word
        }
    }

    /// Whether an `mlx` backend chats with it: only `chat` and `vision`
    /// (the CLI's 53e).
    public var chats: Bool { self == .chat || self == .vision }

    /// Whether an `mlx` backend entry runs it at all -- chats with it, or
    /// embeds with it: what `config scan --register` registers (53e, 53h).
    public var registers: Bool { chats || self == .embedding }

    /// The kind an MLX set reads as: the one stated, else -- a binary before
    /// the CLI's 53e, which states none -- a chat model's, as every MLX set
    /// was before kinds existed.
    public static func reading(_ stated: MLXKind?) -> MLXKind { stated ?? .chat }
}

/// `models info <name> --output-format json`: its `label: value` record, and
/// -- for one stored set, since the CLI's 52e -- the typed `data`. Until then
/// the record's prose is all there is (the design's fallback).
public struct ModelInfo: Sendable, Equatable {
    public struct Window: Sendable, Equatable {
        public let tokens: Int
        /// `default` or `context_size`.
        public let source: String?
        public let trained: Int?
    }

    public struct Cache: Sendable, Equatable {
        public let bytes: Int
        public let type: String?
        public let slidingPositions: Int?
    }

    /// One step back in where a set came from.
    public struct Step: Sendable, Equatable {
        /// `pulled`, `converted`, `quantized`, `trained`.
        public let kind: String
        public let from: String?
        public let detail: String?
        public let inferred: Bool
        public let missing: Bool
    }

    /// What an MLX conversion of a set runs, as `data.mlx_conversion` states
    /// it (the CLI's 53f, with the converters 53h, 53k and 53m added).
    public struct MLXConversion: Sendable, Equatable {
        /// The towers its `config.json` declares (`vision_config`,
        /// `audio_config` ...): `[]` for a language model.
        public let towers: [String]
        /// What a plain `--mlx` runs -- `mlx-lm`, `mlx-vlm`, `mlx-embeddings`,
        /// `mlx-video`, `mlx-audio`, or a word a later CLI adds -- or `nil`
        /// where only `--text-only` can build it.
        public let converter: String?
        /// Whether `--mlx --text-only` -- mlx-lm's build of its language
        /// model alone, the towers left out -- is offered.
        public let textOnly: Bool

        public init(towers: [String], converter: String?, textOnly: Bool) {
            self.towers = towers
            self.converter = converter
            self.textOnly = textOnly
        }
    }

    /// The typed facts: each `nil` where the format's own reader states nothing.
    public struct Facts: Sendable, Equatable {
        public let format: String?
        public let handle: String?
        public let bytes: Int?
        public let architecture: String?
        public let window: Window?
        public let cache: Cache?
        public let lineage: [Step]
        /// A SafeTensors set's conversions, as the CLI decides them (its M20):
        /// `gguf`, `mlx`, or none for a draft head or a diffusion model.
        /// `nil` where the document does not say -- another format, or a
        /// binary before M20.
        public let convertsTo: [String]?
        /// What its MLX conversion runs, where one is offered (the CLI's
        /// 53f); `nil` where none is, on what conversions make, and before 53f.
        public let mlxConversion: MLXConversion?
        /// An MLX set's kind (the CLI's 53e); `nil` on every other format, and
        /// before 53e.
        public let kind: MLXKind?
        /// How wide an MLX embedding model's vectors are, as its files state
        /// it (the CLI's 53h); `nil` for every other set, and before 53h.
        public let vectorWidth: Int?
    }

    public let name: String
    public let fields: [Field]
    public let data: Facts?

    public init(document: ReadDocument) {
        name = document.value["name"]?.stringValue ?? ""
        fields = Field.list(from: document)
        data = document.value["data"].flatMap { value in
            guard value.objectValue != nil else { return nil }
            let window = value["window"].flatMap { w -> Window? in
                guard let tokens = w["tokens"]?.intValue else { return nil }
                return Window(tokens: tokens, source: w["source"]?.stringValue, trained: w["trained"]?.intValue)
            }
            let cache = value["cache"].flatMap { c -> Cache? in
                guard let bytes = c["bytes"]?.intValue else { return nil }
                return Cache(bytes: bytes, type: c["type"]?.stringValue, slidingPositions: c["sliding_positions"]?.intValue)
            }
            let lineage = (value["lineage"]?.arrayValue ?? []).compactMap { step -> Step? in
                guard let kind = step["kind"]?.stringValue else { return nil }
                return Step(
                    kind: kind, from: step["from"]?.stringValue, detail: step["detail"]?.stringValue,
                    inferred: step["inferred"]?.boolValue ?? false, missing: step["missing"]?.boolValue ?? false)
            }
            let conversion = value["mlx_conversion"].flatMap { c -> MLXConversion? in
                guard c.objectValue != nil else { return nil }
                return MLXConversion(
                    towers: (c["towers"]?.arrayValue ?? []).compactMap(\.stringValue), converter: c["converter"]?.stringValue,
                    textOnly: c["text_only"]?.boolValue ?? false)
            }
            return Facts(
                format: value["format"]?.stringValue, handle: value["handle"]?.stringValue, bytes: value["bytes"]?.intValue,
                architecture: value["architecture"]?.stringValue, window: window, cache: cache, lineage: lineage,
                convertsTo: value["converts_to"]?.arrayValue.map { $0.compactMap(\.stringValue) },
                mlxConversion: conversion, kind: value["kind"]?.stringValue.map(MLXKind.init(word:)),
                vectorWidth: value["vector_width"]?.intValue)
        }
    }

    /// Memory a conversation holds at the window: the weights and the cache,
    /// when the document states both -- never an estimate.
    public var memory: Int? {
        guard let bytes = data?.bytes, let cache = data?.cache?.bytes else { return nil }
        return bytes + cache
    }

    /// A field's value by its label.
    public func field(_ label: String) -> String? {
        fields.first { $0.label == label }?.value
    }
}

/// A role and the backend it resolves to, as `models status` says it.
public struct RoleAssignment: Sendable, Equatable, Identifiable {
    /// `chat`, `embedding`, `extraction`, `vision`, `transcription`, `utility`.
    public let role: String
    /// The CLI's words: `actor   (via models.default)`, `(unset -- the chat's own backend)`.
    public let said: String

    public var id: String { role }

    /// The backend it points at, when the CLI names one first; `nil` when unset.
    public var backend: String? {
        let first = said.split(separator: " ").first.map(String.init) ?? ""
        return first.isEmpty || first.hasPrefix("(") ? nil : first
    }

    /// Whether the role names its backend itself rather than falling back.
    public var isSet: Bool { backend != nil && !said.contains("(via ") }

    /// The verb that points the role at a backend: `config set-default-vision`;
    /// the chat role's is `config set-default`.
    public var verb: [String] {
        role == "chat" ? ["config", "set-default"] : ["config", "set-default-\(role)"]
    }

    public static func list(from document: ReadDocument) -> [RoleAssignment] {
        Field.list(from: document).map { RoleAssignment(role: $0.label, said: $0.value) }
    }
}
