import DataProtocol
import Foundation

/// One agent, as `agents list --output-format json` lists it: its prompt and
/// schema files (relative to the data directory), its model and tool policy.
public struct AgentEntry: Sendable, Equatable, Identifiable {
    public let name: String
    public let description: String
    /// Shipped with Apogee rather than the person's own.
    public let bundled: Bool
    /// The backend override; empty for `models.default`.
    public let model: String
    /// `read-only`, `all` or `none`.
    public let tools: String
    public let prompts: [String]
    public let schemas: [String]
    public let collection: String
    public var id: String { name }

    public init?(_ value: JSONValue) {
        guard let name = value["name"]?.stringValue else { return nil }
        self.name = name
        description = value["description"]?.stringValue ?? ""
        bundled = value["bundled"]?.boolValue ?? false
        model = value["model"]?.stringValue ?? ""
        tools = value["tools"]?.stringValue ?? ""
        prompts = (value["prompts"]?.arrayValue ?? []).compactMap(\.stringValue)
        schemas = (value["schemas"]?.arrayValue ?? []).compactMap(\.stringValue)
        collection = value["collection"]?.stringValue ?? ""
    }

    public static func list(from document: ReadDocument) -> [AgentEntry] {
        (document.entries ?? []).compactMap(AgentEntry.init)
    }
}

/// A configured MCP server, as `mcp list --output-format json` connects to it:
/// its state and the tools it offers. Its environment is said only as set or
/// not -- never its values.
public struct MCPServer: Sendable, Equatable, Identifiable {
    public let name: String
    public let command: String
    public let args: [String]
    public let enabled: Bool
    public let envSet: Bool
    /// `connected`, or how connecting failed, in the CLI's words.
    public let state: String
    public let tools: [String]
    public let error: String?
    public var id: String { name }

    public init?(_ value: JSONValue) {
        guard let name = value["name"]?.stringValue else { return nil }
        self.name = name
        command = value["command"]?.stringValue ?? ""
        args = (value["args"]?.arrayValue ?? []).compactMap(\.stringValue)
        enabled = value["enabled"]?.boolValue ?? true
        envSet = value["env_set"]?.boolValue ?? false
        state = value["state"]?.stringValue ?? ""
        tools = (value["tools"]?.arrayValue ?? []).compactMap(\.stringValue)
        error = value["error"]?.stringValue
    }

    public static func list(from document: ReadDocument) -> [MCPServer] {
        (document.entries ?? []).compactMap(MCPServer.init)
    }
}

/// An MCP server Apogee ships (`mcp presets`): what it provides, and how it is
/// registered -- or why it is not registered into Apogee.
public struct MCPPreset: Sendable, Equatable, Identifiable {
    public let name: String
    public let description: String
    public let provides: String
    /// The command that registers it, as the CLI writes it.
    public let register: String?
    /// Why it is not registered into Apogee (it is for another host), in the
    /// CLI's words.
    public let refusal: String?
    public var id: String { name }

    public static func list(from document: ReadDocument) -> [MCPPreset] {
        (document.entries ?? []).compactMap { v in
            v["name"]?.stringValue.map {
                MCPPreset(
                    name: $0, description: v["description"]?.stringValue ?? "", provides: v["provides"]?.stringValue ?? "",
                    register: v["register"]?.stringValue, refusal: v["refusal"]?.stringValue)
            }
        }
    }
}

/// A symphony, as `symphonies list | show --output-format json` carries it: its
/// input, its stages in play order, and anything wrong with it.
public struct Symphony: Sendable, Equatable, Identifiable {
    public struct Stage: Sendable, Equatable, Identifiable {
        public let name: String
        /// The role it asks (`utility`, `chat` ...), or nil for a stage that
        /// plays another symphony.
        public let role: String?
        public let plays: String?
        public let prompt: String
        public var id: String { name }
    }

    public let name: String
    public let description: String
    /// `shipped` (a seeded starter), `config` (an entry, no file) or `file`
    /// (the person's own spec file).
    public let source: String
    public let path: String?
    public let inputDescription: String
    public let takesImage: Bool
    public let stages: [Stage]
    public let problems: [String]
    public var id: String { name }

    /// A spec file of the person's own: what the app edits in place.
    public var isOwnFile: Bool { source == "file" && path != nil }

    public init?(_ value: JSONValue) {
        guard let name = value["name"]?.stringValue else { return nil }
        self.name = name
        description = value["description"]?.stringValue ?? ""
        source = value["source"]?.stringValue ?? ""
        path = value["path"]?.stringValue.flatMap { $0.isEmpty ? nil : $0 }
        inputDescription = value["input"]?["description"]?.stringValue ?? ""
        takesImage = value["input"]?["image"]?.boolValue ?? false
        stages = (value["stages"]?.arrayValue ?? []).compactMap { s in
            s["name"]?.stringValue.map {
                Stage(name: $0, role: s["role"]?.stringValue, plays: s["play"]?.stringValue ?? s["play"]?["symphony"]?.stringValue, prompt: s["prompt"]?.stringValue ?? "")
            }
        }
        problems = (value["problems"]?.arrayValue ?? []).compactMap { $0.stringValue ?? $0["message"]?.stringValue }
    }

    public static func list(from document: ReadDocument) -> [Symphony] {
        (document.entries ?? []).compactMap(Symphony.init)
    }
}

/// `symphonies play --output-format json`: each stage's answer, the backend
/// that gave it, and the play's output.
public struct SymphonyPlay: Sendable, Equatable {
    public struct Stage: Sendable, Equatable, Identifiable {
        public let name: String
        public let role: String
        public let backend: String
        public let answer: String
        public var id: String { name }
    }

    public let symphony: String
    public let output: String
    public let stages: [Stage]

    public init?(document: ReadDocument) {
        let v = document.value
        guard v["object"]?.stringValue == "symphony.play" else { return nil }
        symphony = v["symphony"]?.stringValue ?? ""
        output = v["output"]?.stringValue ?? ""
        stages = (v["stages"]?.arrayValue ?? []).compactMap { s in
            s["name"]?.stringValue.map {
                Stage(name: $0, role: s["role"]?.stringValue ?? "", backend: s["backend"]?.stringValue ?? "", answer: s["answer"]?.stringValue ?? "")
            }
        }
    }
}
