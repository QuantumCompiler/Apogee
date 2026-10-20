/// One document a read printed: `apogee <read> --output-format json`.
///
/// Every read prints one JSON document of the same facts as its human view
/// (machine-mode.md -> Everything else is a CLI command). The document is kept
/// whole; the accessors read the shapes the reads share -- a list's entries,
/// its total, what did not parse.
public struct ReadDocument: Sendable, Equatable {
    public let value: JSONValue

    public init(value: JSONValue) {
        self.value = value
    }

    /// The document `text` holds, or `nil` when it is not one JSON object.
    public init?(text: String) {
        guard let value = JSONValue.parse(text), value.objectValue != nil else { return nil }
        self.value = value
    }

    /// The document's `object` -- `list`, `task.recipe`, `graph.path` ...
    public var object: String? { value["object"]?.stringValue }

    /// A list's entries (`{"object":"list","data":[…]}`).
    public var entries: [JSONValue]? { value["data"]?.arrayValue }

    /// How many there are in all, when the list says (it may show fewer).
    public var total: Int? { value["total"]?.intValue }

    /// What the read could not parse, named by it.
    public var problems: [JSONValue] { value["problems"]?.arrayValue ?? [] }
}
