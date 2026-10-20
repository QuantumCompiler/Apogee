import Foundation

/// Bytes from a pipe, cut into lines.
///
/// A pipe hands over whatever was written, cut anywhere -- one event across
/// two reads, three in one. The framer keeps the unfinished end until the
/// newline that closes it arrives, so every line comes out whole. A line is
/// decoded as UTF-8 only once it is whole, so a character is never cut in two
/// (a newline byte is never part of a multi-byte character); bytes that are
/// not UTF-8 become U+FFFD, the CLI's own mending.
public struct LineFramer: Sendable {
    private var pending = Data()

    public init() {}

    /// The lines `chunk` completes, in order, without their newlines -- a
    /// `\r\n` ending loses its `\r` too.
    public mutating func push(_ chunk: Data) -> [String] {
        pending.append(chunk)
        var lines: [String] = []
        while let newline = pending.firstIndex(of: 0x0A) {
            var line = pending[pending.startIndex..<newline]
            if line.last == 0x0D {
                line = line.dropLast()
            }
            lines.append(String(decoding: line, as: UTF8.self))
            pending.removeSubrange(pending.startIndex...newline)
        }
        return lines
    }

    /// What is left when the stream ends: a last line with no newline, if any.
    public mutating func finish() -> String? {
        defer { pending.removeAll() }
        return pending.isEmpty ? nil : String(decoding: pending, as: UTF8.self)
    }
}
