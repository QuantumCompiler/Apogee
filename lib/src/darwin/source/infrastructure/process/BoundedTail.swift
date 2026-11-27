import Foundation

/// The last bytes a stream carried, and no more.
///
/// A child's stderr is captured, never shown raw: what it said is kept to its
/// last `limit` bytes and folded into a failure when the child fails -- the
/// CLI's own discipline for its children (8 KiB for a vendor CLI), mirrored.
/// A tail is only the end: what a long run said first can be gone from it
/// (`cut`), so a reader that needs every line asks the spawner for them as
/// lines (`ChildCommand.framesErrors`, GUI M22).
public struct BoundedTail: Sendable, Equatable {
    /// How many bytes are kept.
    public let limit: Int
    private var bytes = Data()
    /// Whether bytes were dropped to keep the limit: the text then starts
    /// wherever the cut fell, mid-line as likely as not (GUI M22).
    public private(set) var cut = false

    public init(limit: Int = 8 * 1024) {
        self.limit = limit
    }

    public mutating func append(_ chunk: Data) {
        bytes.append(chunk)
        if bytes.count > limit {
            bytes = Data(bytes.suffix(limit))
            cut = true
        }
    }

    /// Everything kept, as text.
    public var text: String {
        String(decoding: bytes, as: UTF8.self)
    }

    /// The last line with anything on it, trimmed: what a one-line failure says.
    public var lastLine: String? {
        text.split(whereSeparator: \.isNewline)
            .map { $0.trimmingCharacters(in: .whitespaces) }
            .last { !$0.isEmpty }
    }

    public var isEmpty: Bool { bytes.isEmpty }
}
