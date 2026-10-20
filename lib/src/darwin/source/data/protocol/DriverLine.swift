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
        }
    }
}
