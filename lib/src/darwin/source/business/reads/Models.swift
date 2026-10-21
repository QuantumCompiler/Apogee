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
                components: entry["components"]?.intValue, bytes: entry["bytes"]?.intValue)
            return row.name.isEmpty ? nil : row
        }
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
            return Facts(
                format: value["format"]?.stringValue, handle: value["handle"]?.stringValue, bytes: value["bytes"]?.intValue,
                architecture: value["architecture"]?.stringValue, window: window, cache: cache, lineage: lineage,
                convertsTo: value["converts_to"]?.arrayValue.map { $0.compactMap(\.stringValue) })
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
