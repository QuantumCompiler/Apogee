import DataProtocol
import Foundation

/// A task recipe, as `task recipes [show <name>] --output-format json` carries
/// it (the CLI's 40c): a named goal with its checks baked in, which `task run
/// --recipe <name> [--input …]` expands into the very flags a hand-typed run
/// takes. Its grants are suggestions -- given only when the line gives them.
public struct TaskRecipe: Sendable, Equatable, Identifiable {
    public let name: String
    public let description: String
    /// `shipped`, `seeded` or `user`.
    public let source: String
    public let path: String?
    /// What `--input` is for; empty when the goal reads none.
    public let input: String
    public let tools: Bool
    public let require: [String]
    public let requireFiles: [String]
    /// The grants it suggests, never assumes.
    public let allow: [String]
    public let agent: String
    public let rounds: Int?
    /// The goal's template, `{{input}}` where the input goes -- `show`'s
    /// alone.
    public let goal: String?
    public var id: String { name }

    /// Whether the goal reads an input.
    public var takesInput: Bool { !input.isEmpty }

    public init?(_ value: JSONValue) {
        guard let name = value["name"]?.stringValue else { return nil }
        self.name = name
        description = value["description"]?.stringValue ?? ""
        source = value["source"]?.stringValue ?? ""
        path = value["path"]?.stringValue
        input = value["input"]?.stringValue ?? ""
        tools = value["tools"]?.boolValue ?? false
        require = (value["require"]?.arrayValue ?? []).compactMap(\.stringValue)
        requireFiles = (value["require_files"]?.arrayValue ?? []).compactMap(\.stringValue)
        allow = (value["allow"]?.arrayValue ?? []).compactMap(\.stringValue)
        agent = value["agent"]?.stringValue ?? ""
        rounds = value["rounds"]?.intValue
        goal = value["goal"]?.stringValue
    }

    public init?(document: ReadDocument) {
        guard document.value["object"]?.stringValue == "task.recipe" else { return nil }
        self.init(document.value)
    }

    public static func list(from document: ReadDocument) -> [TaskRecipe] {
        (document.entries ?? []).compactMap { TaskRecipe($0) }
    }

    /// The goal with every `{{…}}` read as the input, as the CLI renders it
    /// (a parsed recipe holds no other variable).
    public func renderedGoal(input: String) -> String? {
        guard let goal else { return nil }
        var rest = Substring(goal)
        var out = ""
        while let open = rest.range(of: "{{") {
            out += rest[..<open.lowerBound]
            guard let close = rest[open.upperBound...].range(of: "}}") else {
                out += rest[open.lowerBound...]
                return out
            }
            out += input
            rest = rest[close.upperBound...]
        }
        return out + rest
    }
}
