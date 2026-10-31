import Foundation

/// Any JSON value, kept exactly as it arrived.
///
/// The protocol grows by addition (machine-mode.md -> The stability promise),
/// so a client keeps what it does not understand rather than dropping it: an
/// event's every field, a read's whole document, as values a later client --
/// or a quiet "unknown" row -- can still show.
public enum JSONValue: Sendable, Hashable {
    case null
    case bool(Bool)
    case number(Double)
    case string(String)
    case array([JSONValue])
    case object([String: JSONValue])

    /// One JSON text, or `nil` when it is not one.
    public static func parse(_ text: String) -> JSONValue? {
        guard let data = text.data(using: .utf8),
            let parsed = try? JSONSerialization.jsonObject(with: data, options: [.fragmentsAllowed])
        else { return nil }
        return JSONValue(foundation: parsed)
    }

    /// A value `JSONSerialization` produced.
    init(foundation value: Any) {
        switch value {
        case let number as NSNumber where CFGetTypeID(number) == CFBooleanGetTypeID():
            self = .bool(number.boolValue)
        case let number as NSNumber:
            self = .number(number.doubleValue)
        case let string as String:
            self = .string(string)
        case let array as [Any]:
            self = .array(array.map(JSONValue.init(foundation:)))
        case let object as [String: Any]:
            self = .object(object.mapValues(JSONValue.init(foundation:)))
        default:
            self = .null
        }
    }

    public subscript(key: String) -> JSONValue? {
        if case .object(let object) = self { object[key] } else { nil }
    }

    public var stringValue: String? {
        if case .string(let string) = self { string } else { nil }
    }

    public var boolValue: Bool? {
        if case .bool(let bool) = self { bool } else { nil }
    }

    /// The number, when it is a whole one.
    public var intValue: Int? {
        if case .number(let number) = self, number.rounded() == number, abs(number) < 1e15 {
            Int(number)
        } else {
            nil
        }
    }

    public var arrayValue: [JSONValue]? {
        if case .array(let array) = self { array } else { nil }
    }

    public var objectValue: [String: JSONValue]? {
        if case .object(let object) = self { object } else { nil }
    }

    /// The value as one line of JSON, its keys sorted.
    public var serialized: String {
        switch self {
        case .null: "null"
        case .bool(let bool): bool ? "true" : "false"
        case .number(let number):
            if let int = intValue { String(int) } else { "\(number)" }
        case .string(let string): JSONValue.quoted(string)
        case .array(let array): "[" + array.map(\.serialized).joined(separator: ",") + "]"
        case .object(let object):
            "{" + object.keys.sorted().map { JSONValue.quoted($0) + ":" + object[$0]!.serialized }
                .joined(separator: ",") + "}"
        }
    }

    /// `text` as a JSON string literal, quotes included.
    public static func quoted(_ text: String) -> String {
        let data = try! JSONSerialization.data(
            withJSONObject: text, options: [.fragmentsAllowed, .withoutEscapingSlashes])
        return String(decoding: data, as: UTF8.self)
    }
}
