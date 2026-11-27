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
    /// Whether this machine can run it, as the binary resolved it here (the
    /// CLI's 47c); `nil` where the row doesn't say.
    public var runnable: Bool?
    /// What this machine lacks to run it, in the CLI's words, where it can't.
    public var missing: String?
    /// The command that provides what is missing, where the CLI names one.
    public var remedy: String?
    /// What it needs done outside Apogee, in the words the listing prints
    /// under it (the CLI's 56d: Xcode's switch, the open project, Xcode's
    /// approval); present only on a row that has one.
    public var note: String?
    public var id: String { name }

    public init(
        name: String, description: String, provides: String, register: String?, refusal: String?,
        runnable: Bool? = nil, missing: String? = nil, remedy: String? = nil, note: String? = nil
    ) {
        self.name = name
        self.description = description
        self.provides = provides
        self.register = register
        self.refusal = refusal
        self.runnable = runnable
        self.missing = missing
        self.remedy = remedy
        self.note = note
    }

    /// The server name its own `register` line gives -- `apogee mcp create
    /// <server> --from <preset>` (the CLI's `mcp_presets.cpp`) -- never one
    /// the app makes up; `nil` where the row has no such line.
    public var serverName: String? {
        let words = (register ?? "").split(separator: " ").map(String.init)
        guard words.count == 6, words[1] == "mcp", words[2] == "create", words[4] == "--from", words[5] == name else { return nil }
        return words[3]
    }

    /// What registering it runs, as a person types it -- its line's `mcp
    /// create <server> --from <preset>`, built as Library's Register builds
    /// it; `nil` where the row has no such line.
    public var registration: [String]? {
        serverName.map { ["mcp", "create", $0, "--from", name] }
    }

    public static func list(from document: ReadDocument) -> [MCPPreset] {
        (document.entries ?? []).compactMap { v in
            v["name"]?.stringValue.map {
                MCPPreset(
                    name: $0, description: v["description"]?.stringValue ?? "", provides: v["provides"]?.stringValue ?? "",
                    register: v["register"]?.stringValue, refusal: v["refusal"]?.stringValue,
                    runnable: v["runnable"]?.boolValue, missing: v["missing"]?.stringValue, remedy: v["remedy"]?.stringValue,
                    note: v["note"]?.stringValue)
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
