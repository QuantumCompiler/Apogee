/// A line the client writes to the child's stdin.
///
/// One JSON object per line (machine-mode.md -> Writing to the child), its
/// `type` first and its other fields in a fixed order, so the same line is
/// always the same bytes. `encoded` carries no newline; the writer adds it.
public enum DriverLine: Sendable, Equatable {
    /// The optional first line: who is driving, and what it wants.
    case hello(clientName: String, clientVersion: String, wants: [String])
    /// A turn.
    case user(String)
    /// One answer to one question of a `question` event.
    case answer(String)
    /// A file, folder or glob attached to the chat; `graph` is `code` or `off`.
    case attach(path: String, graph: String?)
    /// Stops the turn in flight.
    case cancel
    /// Detaches what was attached under `path` (the CLI's 52d).
    case detach(path: String)
    /// Moves the session's run settings from the next turn (52d).
    case set(RunSettings)
    /// Compacts the conversation now (52d).
    case compact

    public var encoded: String {
        func object(_ fields: [(String, String)]) -> String {
            "{" + fields.map { JSONValue.quoted($0.0) + ":" + $0.1 }.joined(separator: ",") + "}"
        }
        let q = JSONValue.quoted
        return switch self {
        case .hello(let name, let version, let wants):
            object([
                ("type", q("hello")),
                ("client", object([("name", q(name)), ("version", q(version))])),
                ("wants", "[" + wants.map(q).joined(separator: ",") + "]"),
            ])
        case .user(let text):
            object([("type", q("user")), ("text", q(text))])
        case .answer(let text):
            object([("type", q("answer")), ("text", q(text))])
        case .attach(let path, let graph):
            object([("type", q("attach")), ("path", q(path))] + (graph.map { [("graph", q($0))] } ?? []))
        case .cancel:
            object([("type", q("cancel"))])
        case .detach(let path):
            object([("type", q("detach")), ("path", q(path))])
        case .set(let settings):
            object([("type", q("set"))] + settings.fields)
        case .compact:
            object([("type", q("compact"))])
        }
    }
}

/// The run settings a session moves: what `-m`, `-s`, `--think` and
/// `--effort` choose at launch, and the `set` line moves after.
public struct RunSettings: Sendable, Equatable {
    /// A backend, a roster model or `backend:model`.
    public var model: String?
    /// The system prompt, replaced whole.
    public var system: String?
    /// `on`, `off` or `auto`.
    public var think: String?
    /// `low` ... `max`, or `auto`.
    public var effort: String?
    public var temperature: Double?

    public init(model: String? = nil, system: String? = nil, think: String? = nil, effort: String? = nil, temperature: Double? = nil) {
        self.model = model
        self.system = system
        self.think = think
        self.effort = effort
        self.temperature = temperature
    }

    public var isEmpty: Bool { self == RunSettings() }

    /// The set ones, in the order the CLI applies them: the model first.
    var fields: [(String, String)] {
        var fields: [(String, String)] = []
        if let model { fields.append(("model", JSONValue.quoted(model))) }
        if let system { fields.append(("system", JSONValue.quoted(system))) }
        if let think { fields.append(("think", JSONValue.quoted(think))) }
        if let effort { fields.append(("effort", JSONValue.quoted(effort))) }
        if let temperature { fields.append(("temperature", JSONValue.number(temperature).serialized)) }
        return fields
    }

    /// What `other` sets differently from `self`: the settings a `set` line
    /// would carry. A setting `other` leaves unset is no change -- a session's
    /// setting is moved, never cleared.
    public func changes(to other: RunSettings) -> RunSettings {
        RunSettings(
            model: other.model.flatMap { $0 == model ? nil : $0 },
            system: other.system.flatMap { $0 == system ? nil : $0 },
            think: other.think.flatMap { $0 == think ? nil : $0 },
            effort: other.effort.flatMap { $0 == effort ? nil : $0 },
            temperature: other.temperature.flatMap { $0 == temperature ? nil : $0 })
    }

    /// `self` with every setting `other` sets laid over it.
    public func merging(_ other: RunSettings) -> RunSettings {
        RunSettings(
            model: other.model ?? model, system: other.system ?? system, think: other.think ?? think,
            effort: other.effort ?? effort, temperature: other.temperature ?? temperature)
    }
}
