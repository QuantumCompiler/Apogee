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
    /// The longest line kept, in bytes; `nil`, no limit. A line past it is
    /// cut to its first `maxLine` bytes, and the rest of it, up to its
    /// newline, dropped -- so a runaway line holds no more than that (GUI
    /// M22, stderr's lines).
    public let maxLine: Int?
    private var pending = Data()
    /// The pending line passed `maxLine`: its rest is dropped until its
    /// newline.
    private var over = false

    /// A framer keeping lines of at most `maxLine` bytes; none, every line
    /// whole.
    public init(maxLine: Int? = nil) {
        self.maxLine = maxLine
    }

    /// The lines `chunk` completes, in order, without their newlines -- a
    /// `\r\n` ending loses its `\r` too.
    public mutating func push(_ chunk: Data) -> [String] {
        var lines: [String] = []
        var rest = chunk[...]
        while let newline = rest.firstIndex(of: 0x0A) {
            keep(rest[rest.startIndex..<newline])
            var line = pending[...]
            if line.last == 0x0D {
                line = line.dropLast()
            }
            lines.append(String(decoding: line, as: UTF8.self))
            pending.removeAll()
            over = false
            rest = rest[rest.index(after: newline)...]
        }
        keep(rest)
        return lines
    }

    /// What is left when the stream ends: a last line with no newline, if any.
    public mutating func finish() -> String? {
        defer {
            pending.removeAll()
            over = false
        }
        return pending.isEmpty ? nil : String(decoding: pending, as: UTF8.self)
    }

    /// `bytes`, part of the pending line, kept up to the limit.
    private mutating func keep(_ bytes: Data) {
        guard !over else { return }
        if let maxLine, pending.count + bytes.count > maxLine {
            pending.append(bytes.prefix(max(0, maxLine - pending.count)))
            over = true
        } else {
            pending.append(bytes)
        }
    }
}
