import Foundation

/// A model's markdown as the blocks `MarkdownText` lays out: Foundation's own
/// parser read into a tree the app draws itself.
///
/// No package and no parser of the app's own (the darwin layering check
/// forbids a dependency): `AttributedString(markdown:)` with its full syntax
/// marks every run with the blocks it sits in (`PresentationIntent`,
/// innermost first, each with an identity) and its inline styles, and this
/// type only groups those runs back into blocks. SwiftUI-free, so the
/// segmentation is tested without a view.
///
/// A single newline inside a paragraph stays a line break, as the CLI's
/// terminal renderer keeps it (it renders line by line): an answer with no
/// markdown in it reads as it was written.
///
/// Every character the model wrote reaches the screen: the blocks may style
/// it, never drop it. A reference-style link definition (`[1]: https://…`),
/// which Foundation would take as link metadata and show nowhere, is kept as
/// the line of text it is, as the CLI's terminal renderer prints it -- its
/// address linked as any bare address is, and a reference to it (`[text][1]`)
/// left as written, never resolved.
public struct MarkdownDocument: Equatable, Sendable {
    /// The blocks, in order.
    public let blocks: [MarkdownBlock]

    /// `source` parsed. `streaming` says it is an answer still arriving, and
    /// reads its edge tolerantly -- what is unfinished there is either drawn
    /// as what it already is or held back a moment, never shown as markers
    /// and then reshaped. Only an unfinished construct's own marker line is
    /// ever held, never a line of content, and never past its next line:
    /// - an open fence is a code block in progress (CommonMark runs an
    ///   unclosed fence to the end of the text);
    /// - an unclosed emphasis or code marker shows literally until it closes;
    /// - a last line that is nothing yet but a construct's own markers
    ///   (`isMarkerLine`: a fence or a closing fence being typed, a rule or a
    ///   setext underline, a heading's, a quote's or a list item's marker
    ///   with no words yet, a table's delimiter row) waits for the rest of
    ///   its line;
    /// - a last line that may be a table's first row -- led by a pipe, after
    ///   no other pipe-led line, no blank line after it -- waits for its next
    ///   line, which says whether it is a table (`endsAtTableHeader`).
    ///
    /// Finished, the answer is read as written: a fence opened with nothing
    /// under it shows as its line, and text the parse makes nothing of shows
    /// as written.
    public init(parsing source: String, streaming: Bool = false) {
        let text = streaming ? Self.heldBack(source) : source
        let definitions = Self.keepingDefinitions(text)
        let marked = definitions ?? text
        // A finished answer ending at a fence's line is parsed with a probe
        // line after it, so a fence it opens with nothing under it is known.
        if !streaming, let fence = Self.fenceAtEnd(text), !marked.unicodeScalars.contains(Self.probe),
            let probed = Self.parse(marked + (marked.hasSuffix("\n") ? "" : "\n") + String(Self.probe), unmarking: definitions != nil),
            let blocks = Self.withoutProbe(probed, fence: fence)
        {
            self.blocks = blocks.isEmpty ? Self.asWritten(text) : blocks
            return
        }
        guard var blocks = Self.parse(marked, unmarking: definitions != nil) else {
            // Never lose an answer: what cannot be read is shown as written.
            self.blocks = Self.asWritten(text)
            return
        }
        if streaming {
            if case .paragraph(let spans)? = blocks.last, Self.endsAtTableHeader(text) {
                blocks.removeLast()
                let above = Self.droppingLastLine(spans)
                if !above.isEmpty { blocks.append(.paragraph(above)) }
            }
        } else if blocks.isEmpty {
            blocks = Self.asWritten(text)
        }
        self.blocks = blocks
    }

    /// A document of `blocks`, as a test or a preview states one.
    public init(blocks: [MarkdownBlock]) {
        self.blocks = blocks
    }

    /// `text` read by Foundation's markdown into blocks -- `unmarking` the
    /// definitions' marks out of them again -- or `nil` when it cannot be read.
    private static func parse(_ text: String, unmarking: Bool) -> [MarkdownBlock]? {
        let options = AttributedString.MarkdownParsingOptions(
            allowsExtendedAttributes: false, interpretedSyntax: .full,
            failurePolicy: .returnPartiallyParsedIfPossible, languageCode: nil)
        guard let parsed = try? AttributedString(markdown: text, options: options) else { return nil }
        return blocks(of: tree(parsed, unmarking: unmarking))
    }

    /// `text` as written, one paragraph: what the parse made nothing of.
    /// Only whitespace is nothing to show.
    private static func asWritten(_ text: String) -> [MarkdownBlock] {
        let written = text.trimmingCharacters(in: .whitespacesAndNewlines)
        return written.isEmpty ? [] : [.paragraph([MarkdownSpan(written)])]
    }

    // MARK: - Link definitions, kept as text

    /// What marks a line that could be a link definition, between its `]`
    /// and `:`: a noncharacter, so no answer means it, and Foundation reads
    /// the line as the text it is. Taken out of every span again.
    static let definitionMark = Unicode.Scalar(UInt32(0xFDD0))!

    /// `text` with every line that could be a link reference definition
    /// marked -- past indentation and any quote or list markers, a
    /// `[label]` with a colon straight after it, the label closed on its
    /// line -- so Foundation reads it as text rather than metadata it shows
    /// nowhere. A footnote's (`[^1]: note`) too. A mark that lands in code
    /// or anything else that is not a definition is taken out all the same,
    /// so it changes nothing there. `nil` when there is nothing to mark, or
    /// the text already holds the mark.
    static func keepingDefinitions(_ text: String) -> String? {
        let bytes = Array(text.utf8)
        var marks: [Int] = []
        var line = 0
        while line < bytes.count {
            var at = afterContainerMarkers(bytes, from: line)
            if at < bytes.count, bytes[at] == Edge.open {
                at += 1
                while at < bytes.count, bytes[at] != Edge.close, bytes[at] != Edge.newline {
                    at += bytes[at] == Edge.backslash ? 2 : 1
                }
                if at + 1 < bytes.count, bytes[at] == Edge.close, bytes[at + 1] == Edge.colon {
                    marks.append(at + 1)
                }
            }
            while at < bytes.count, bytes[at] != Edge.newline { at += 1 }
            line = at + 1
        }
        guard !marks.isEmpty, !text.unicodeScalars.contains(definitionMark) else { return nil }
        let mark = Array(String(definitionMark).utf8)
        var marked: [UInt8] = []
        marked.reserveCapacity(bytes.count + marks.count * mark.count)
        var from = 0
        for place in marks {
            marked += bytes[from..<place]
            marked += mark
            from = place
        }
        marked += bytes[from...]
        return String(decoding: marked, as: UTF8.self)
    }

    /// Where a line's content starts, past its indentation and the markers
    /// of the quotes and list items it opens (`> `, `- `, `1. `).
    private static func afterContainerMarkers(_ bytes: [UInt8], from start: Int) -> Int {
        var at = start
        func blanks() {
            while at < bytes.count, Edge.isBlank(bytes[at]) { at += 1 }
        }
        blanks()
        while at < bytes.count {
            if bytes[at] == Edge.quote {
                at += 1
            } else if Edge.bullets.contains(bytes[at]), at + 1 < bytes.count, Edge.isBlank(bytes[at + 1]) {
                at += 1
            } else if let end = orderedMarkerEnd(bytes, from: at), end < bytes.count, Edge.isBlank(bytes[end]) {
                at = end
            } else {
                break
            }
            blanks()
        }
        return at
    }

    /// Past an ordered list item's number and its `.` or `)` at `start`;
    /// `nil` when there is none (CommonMark's one to nine digits).
    private static func orderedMarkerEnd(_ bytes: some RandomAccessCollection<UInt8>, from start: Int) -> Int? {
        var at = start
        while at < bytes.count, at - start < 10, Edge.isDigit(bytes[bytes.index(bytes.startIndex, offsetBy: at)]) { at += 1 }
        guard (1...9).contains(at - start), at < bytes.count else { return nil }
        let after = bytes[bytes.index(bytes.startIndex, offsetBy: at)]
        return after == Edge.dot || after == Edge.paren ? at + 1 : nil
    }

    // MARK: - A finished answer's last fence

    /// The probe line a finished answer ending at a fence's line is parsed
    /// with: a noncharacter, in the block the fence leaves open, or after it.
    static let probe = Unicode.Scalar(UInt32(0xFDD1))!

    /// A finished `text`'s last line with anything on it, when it is a
    /// fence's line -- at most three spaces in, three or more backticks or
    /// tildes -- and so may open a fence with nothing under it.
    static func fenceAtEnd(_ text: String) -> String? {
        let bytes = text.utf8
        var end = bytes.endIndex
        while end > bytes.startIndex, Edge.isSpace(bytes[bytes.index(before: end)]) { end = bytes.index(before: end) }
        guard end > bytes.startIndex else { return nil }
        var start = end
        while start > bytes.startIndex, bytes[bytes.index(before: start)] != Edge.newline { start = bytes.index(before: start) }
        let line = bytes[start..<end]
        let indent = line.prefix { $0 == Edge.space }.count
        let rest = line.dropFirst(indent)
        guard indent <= 3, let marker = rest.first, marker == Edge.backtick || marker == Edge.tilde,
            rest.prefix(while: { $0 == marker }).count >= 3
        else { return nil }
        return String(text[start..<end]).trimmingCharacters(in: .whitespaces)
    }

    /// `blocks`, parsed with the probe line after the answer, as they are
    /// without it: the probe taken out of where it landed -- a paragraph of
    /// its own after a closed fence, the end of an open block's code, or the
    /// end of an HTML block's text -- and a fence it alone was the code of,
    /// one opened with nothing under it, shown as its `fence` line as
    /// written. `nil` where it landed anywhere else.
    private static func withoutProbe(_ blocks: [MarkdownBlock], fence: String) -> [MarkdownBlock]? {
        let alone = String(probe)
        let after = "\n" + alone
        var blocks = blocks
        switch blocks.popLast() {
        case .paragraph(let spans)? where spans == [MarkdownSpan(alone)]:
            break
        case .paragraph(var spans)? where spans.last?.text.hasSuffix(after) == true:
            spans[spans.count - 1].text.removeLast(after.count)
            if spans[spans.count - 1].text.isEmpty { spans.removeLast() }
            blocks.append(.paragraph(spans))
        case .code(let code)? where code.text == alone:
            blocks.append(.paragraph([MarkdownSpan(fence)]))
        case .code(let code)? where code.text.hasSuffix(after):
            blocks.append(.code(MarkdownCode(language: code.language, text: String(code.text.dropLast(after.count)))))
        default:
            return nil
        }
        return blocks
    }

    // MARK: - The stream's edge

    /// `source` with its last line held back while it is non-empty and
    /// nothing yet but a construct's own markers (`isMarkerLine`): "Text\n-"
    /// is a heading until the next character says it is a list, and
    /// "```python\nx\n``" is a closing fence being typed, not code. A line
    /// with content -- "42", "128 * 8 = 1024", a table's row -- is never held.
    static func heldBack(_ source: String) -> String {
        let bytes = source.utf8
        var start = bytes.endIndex
        while start > bytes.startIndex, bytes[bytes.index(before: start)] != Edge.newline { start = bytes.index(before: start) }
        let line = Array(bytes[start...])
        guard !line.isEmpty, isMarkerLine(line) else { return source }
        return String(source[..<start])
    }

    /// Whether `line` is nothing yet but the markers of a construct still
    /// being typed, past its indentation and any quote's or list item's
    /// marker: nothing more (indentation, `> `, `- `, `1. `); a run of
    /// backticks or of tildes (a fence or a closing fence); hashes (a
    /// heading's); `-`, `*`, `_`, `=`, `+` and spaces (a rule, a setext
    /// underline, a bullet with no space yet); a number and its `.` or `)`
    /// (an ordered item's); or pipes, colons, dashes and spaces with a pipe
    /// among them (a table's delimiter row, or a row's opening pipe).
    static func isMarkerLine(_ line: [UInt8]) -> Bool {
        var rest = line[afterContainerMarkers(line, from: 0)...]
        while let last = rest.last, Edge.isBlank(last) { rest.removeLast() }
        guard let first = rest.first else { return true }
        if first == Edge.backtick || first == Edge.tilde || first == Edge.hash {
            return rest.allSatisfy { $0 == first }
        }
        if rest.allSatisfy({ Edge.rules.contains($0) || Edge.isBlank($0) }) { return true }
        if orderedMarkerEnd(rest, from: 0) == rest.count { return true }
        return rest.contains(Edge.pipe) && rest.allSatisfy { Edge.delimiters.contains($0) || Edge.isBlank($0) }
    }

    /// Whether `text` ends at a line that may be a table's first row: led by
    /// a pipe, the line before it led by none, and no blank line after it.
    /// That line waits for the next, which says whether it is a table; pipe-
    /// led prose past it, or ended by a blank line, is shown as it is.
    static func endsAtTableHeader(_ text: String) -> Bool {
        var bytes = text.utf8[...]
        if bytes.last == Edge.newline { bytes.removeLast() }
        var start = bytes.endIndex
        while start > bytes.startIndex, bytes[bytes.index(before: start)] != Edge.newline { start = bytes.index(before: start) }
        guard pipeLed(bytes[start...]) else { return false }
        guard start > bytes.startIndex else { return true }
        let end = bytes.index(before: start)
        var previous = end
        while previous > bytes.startIndex, bytes[bytes.index(before: previous)] != Edge.newline { previous = bytes.index(before: previous) }
        return !pipeLed(bytes[previous..<end])
    }

    private static func pipeLed(_ line: Substring.UTF8View.SubSequence) -> Bool {
        line.first { !Edge.isBlank($0) } == Edge.pipe
    }

    /// A paragraph's spans without its last line: all of it before the last
    /// line break.
    static func droppingLastLine(_ spans: [MarkdownSpan]) -> [MarkdownSpan] {
        var spans = spans
        while var last = spans.popLast() {
            if let newline = last.text.lastIndex(of: "\n") {
                last.text = String(last.text[..<newline])
                if !last.text.isEmpty { spans.append(last) }
                return spans
            }
        }
        return []
    }

    /// The bytes the edge's rules look for.
    private enum Edge {
        static let newline = UInt8(ascii: "\n")
        static let space = UInt8(ascii: " ")
        static let tab = UInt8(ascii: "\t")
        static let backslash = UInt8(ascii: "\\")
        static let open = UInt8(ascii: "[")
        static let close = UInt8(ascii: "]")
        static let colon = UInt8(ascii: ":")
        static let quote = UInt8(ascii: ">")
        static let dot = UInt8(ascii: ".")
        static let paren = UInt8(ascii: ")")
        static let backtick = UInt8(ascii: "`")
        static let tilde = UInt8(ascii: "~")
        static let hash = UInt8(ascii: "#")
        static let pipe = UInt8(ascii: "|")
        /// A list item's bullets.
        static let bullets = Set("-*+".utf8)
        /// What a rule, a setext underline or a bullet with no space yet is made of.
        static let rules = Set("-*_=+".utf8)
        /// What a table's delimiter row is made of.
        static let delimiters = Set("|:-".utf8)

        static func isBlank(_ byte: UInt8) -> Bool { byte == space || byte == tab }
        static func isSpace(_ byte: UInt8) -> Bool { isBlank(byte) || byte == newline || byte == UInt8(ascii: "\r") }
        static func isDigit(_ byte: UInt8) -> Bool { byte >= UInt8(ascii: "0") && byte <= UInt8(ascii: "9") }
    }

    // MARK: - Runs into blocks

    /// A block in the making: what the runs say it is, its identity, and the
    /// blocks or spans inside it.
    private final class Node {
        enum Kind {
            case root
            /// Runs the parser placed in no block: an HTML block, kept as written.
            case raw
            case intent(PresentationIntent.Kind)
        }

        let kind: Kind
        let identity: Int
        var children: [Node] = []
        var spans: [MarkdownSpan] = []

        init(_ kind: Kind, identity: Int) {
            self.kind = kind
            self.identity = identity
        }

        /// `span` after the last, joined to it when they read alike.
        func append(_ span: MarkdownSpan) {
            if let last = spans.last, last.joins(span) {
                spans[spans.count - 1].text += span.text
            } else {
                spans.append(span)
            }
        }
    }

    /// Every run placed in its blocks: consecutive runs share a block for as
    /// long as their intents' identities agree, outermost first. `unmarking`
    /// takes the definitions' marks out of each run's text.
    private static func tree(_ parsed: AttributedString, unmarking: Bool) -> Node {
        let root = Node(.root, identity: 0)
        for run in parsed.runs {
            var text = String(parsed[run.range].characters)
            // The mark's first UTF-8 byte, 0xEF, is rare in an answer: only a
            // run holding one is searched.
            if unmarking, text.utf8.contains(0xEF) {
                text.unicodeScalars.removeAll { $0 == definitionMark }
                if text.isEmpty { continue }
            }
            let span = MarkdownSpan(text, run.inlinePresentationIntent ?? [], link: run.link, image: run.imageURL)
            guard let intent = run.presentationIntent else {
                if let last = root.children.last, case .raw = last.kind {
                    last.append(span)
                } else {
                    let raw = Node(.raw, identity: -1)
                    raw.append(span)
                    root.children.append(raw)
                }
                continue
            }
            var node = root
            for component in intent.components.reversed() {
                if let last = node.children.last, case .intent = last.kind, last.identity == component.identity {
                    node = last
                } else {
                    let next = Node(.intent(component.kind), identity: component.identity)
                    node.children.append(next)
                    node = next
                }
            }
            node.append(span)
        }
        return root
    }

    private static func blocks(of node: Node) -> [MarkdownBlock] {
        node.children.flatMap(block)
    }

    private static func block(_ node: Node) -> [MarkdownBlock] {
        switch node.kind {
        case .root:
            return blocks(of: node)
        case .raw:
            return [.paragraph(trimmingNewlines(node.spans))]
        case .intent(let kind):
            switch kind {
            case .paragraph:
                return [.paragraph(node.spans)]
            case .header(let level):
                return [.heading(level: level, node.spans)]
            case .codeBlock(let hint):
                var code = node.spans.map(\.text).joined()
                if code.hasSuffix("\n") { code.removeLast() }
                let language = hint?.trimmingCharacters(in: .whitespaces)
                return [.code(MarkdownCode(language: language?.isEmpty == false ? language : nil, text: code))]
            case .thematicBreak:
                return [.rule]
            case .blockQuote:
                return [.quote(blocks(of: node))]
            case .orderedList, .unorderedList:
                let items = node.children.enumerated().map { index, item in
                    if case .intent(.listItem(let ordinal)) = item.kind {
                        MarkdownListItem(ordinal: ordinal, blocks: blocks(of: item))
                    } else {
                        MarkdownListItem(ordinal: index + 1, blocks: block(item))
                    }
                }
                if case .orderedList = kind {
                    return [.list(MarkdownList(ordered: true, items: items))]
                }
                return [.list(MarkdownList(ordered: false, items: items))]
            case .table(let columns):
                return [.table(table(node, columns: columns))]
            default:
                // A list item, a row or a cell found outside its parent, or a
                // kind this Foundation adds later: its contents, as blocks.
                return blocks(of: node) + (node.spans.isEmpty ? [] : [.paragraph(node.spans)])
            }
        }
    }

    private static func table(_ node: Node, columns: [PresentationIntent.TableColumn]) -> MarkdownTable {
        let alignments = columns.map { column -> MarkdownTable.Alignment in
            switch column.alignment {
            case .center: .center
            case .right: .trailing
            default: .leading
            }
        }
        var header = [[MarkdownSpan]](repeating: [], count: columns.count)
        var rows: [[[MarkdownSpan]]] = []
        for row in node.children {
            var cells = [[MarkdownSpan]](repeating: [], count: columns.count)
            for cell in row.children {
                if case .intent(.tableCell(let column)) = cell.kind, cells.indices.contains(column) {
                    cells[column] = cell.spans
                }
            }
            if case .intent(.tableHeaderRow) = row.kind {
                header = cells
            } else {
                rows.append(cells)
            }
        }
        return MarkdownTable(alignments: alignments, header: header, rows: rows)
    }

    private static func trimmingNewlines(_ spans: [MarkdownSpan]) -> [MarkdownSpan] {
        var spans = spans
        while var last = spans.last, last.text.hasSuffix("\n") {
            last.text.removeLast()
            spans.removeLast()
            if !last.text.isEmpty { spans.append(last) }
        }
        return spans
    }
}

/// One block of an answer.
public indirect enum MarkdownBlock: Equatable, Sendable {
    /// Prose: its spans, with emphasis, strong, inline code and links.
    case paragraph([MarkdownSpan])
    /// A heading, `#` (1) to `######` (6).
    case heading(level: Int, [MarkdownSpan])
    /// An ordered or unordered list, its items holding blocks of their own --
    /// a nested list among them.
    case list(MarkdownList)
    /// A block quote's blocks.
    case quote([MarkdownBlock])
    /// A fenced or indented code block.
    case code(MarkdownCode)
    /// A thematic break, `---`.
    case rule
    /// A table, where the parser read one.
    case table(MarkdownTable)
}

/// A list, ordered or not.
public struct MarkdownList: Equatable, Sendable {
    public let ordered: Bool
    public let items: [MarkdownListItem]

    public init(ordered: Bool, items: [MarkdownListItem]) {
        self.ordered = ordered
        self.items = items
    }
}

/// A list's item: its number and its blocks.
public struct MarkdownListItem: Equatable, Sendable {
    /// An ordered list's number as written (a list may start at 3); an
    /// unordered item's place.
    public let ordinal: Int
    public let blocks: [MarkdownBlock]

    public init(ordinal: Int, blocks: [MarkdownBlock]) {
        self.ordinal = ordinal
        self.blocks = blocks
    }
}

/// A code block: its code exactly, and the language its fence named.
public struct MarkdownCode: Equatable, Sendable {
    /// The fence's info string (`python`); `nil` when it named none.
    public let language: String?
    /// The code, without the fences or its last newline: what Copy puts on
    /// the pasteboard.
    public let text: String

    public init(language: String?, text: String) {
        self.language = language
        self.text = text
    }

    /// What the block's caption says: its language, or "code".
    public var caption: String { language ?? "code" }
}

/// A table: each column's alignment, its header's cells and its rows' cells,
/// every row as wide as the header.
public struct MarkdownTable: Equatable, Sendable {
    /// A column's alignment, from its delimiter row (`:-:`, `--:`).
    public enum Alignment: Equatable, Sendable {
        case leading, center, trailing
    }

    public let alignments: [Alignment]
    public let header: [[MarkdownSpan]]
    public let rows: [[[MarkdownSpan]]]

    public init(alignments: [Alignment], header: [[MarkdownSpan]], rows: [[[MarkdownSpan]]]) {
        self.alignments = alignments
        self.header = header
        self.rows = rows
    }
}

/// A run of inline text with one look: its styles, and the link or image it is.
public struct MarkdownSpan: Equatable, Sendable {
    /// The inline styles markdown marks.
    public struct Style: OptionSet, Equatable, Sendable {
        public let rawValue: Int
        public init(rawValue: Int) { self.rawValue = rawValue }

        public static let emphasis = Style(rawValue: 1 << 0)
        public static let strong = Style(rawValue: 1 << 1)
        public static let code = Style(rawValue: 1 << 2)
        public static let strikethrough = Style(rawValue: 1 << 3)
    }

    /// The words, as shown: an image's alt text.
    public var text: String
    public var style: Style
    /// Where a link points: followed only for `LinkOpener.followedSchemes`.
    public var link: URL?
    /// An image's address. It is drawn as its alt text and this address, and
    /// never fetched: an answer never reaches the network.
    public var image: URL?

    public init(_ text: String, style: Style = [], link: URL? = nil, image: URL? = nil) {
        self.text = text
        self.style = style
        self.link = link
        self.image = image
    }

    /// A span from a parsed run: its inline intents read as styles, a soft
    /// break kept as the newline it was written as.
    init(_ text: String, _ intent: InlinePresentationIntent, link: URL?, image: URL?) {
        var style: Style = []
        if intent.contains(.emphasized) { style.insert(.emphasis) }
        if intent.contains(.stronglyEmphasized) { style.insert(.strong) }
        if intent.contains(.code) { style.insert(.code) }
        if intent.contains(.strikethrough) { style.insert(.strikethrough) }
        self.init(intent.contains(.softBreak) ? "\n" : text, style: style, link: link, image: image)
    }

    /// Whether `next` reads exactly as this span does, so the two are one.
    /// Images never join: each is its own alt text and address.
    func joins(_ next: MarkdownSpan) -> Bool {
        image == nil && next.image == nil && style == next.style && link == next.link
    }
}

/// Where a click on an answer's link goes: through the system, only for the
/// schemes a chat may follow.
///
/// `http`, `https` and `mailto` open in the person's browser and mail app. A
/// `file:` link -- and every other scheme -- is drawn and never followed: an
/// answer is a model's words, and a click on them must not open the person's
/// files or start another application. The view that draws the links opens
/// them (`LinkOpener.system`, through `NSWorkspace`), never a view-model.
public struct LinkOpener: Sendable {
    /// The schemes a link is followed for.
    public static let followedSchemes: Set<String> = ["http", "https", "mailto"]

    /// Whether `url` is one a click follows.
    public static func follows(_ url: URL) -> Bool {
        url.scheme.map { followedSchemes.contains($0.lowercased()) } ?? false
    }

    private let open: @MainActor @Sendable (URL) -> Void

    /// An opener that hands each followed link to `open`.
    public init(open: @escaping @MainActor @Sendable (URL) -> Void) {
        self.open = open
    }

    /// Opens `url` when its scheme is followed; returns whether it did.
    @MainActor
    @discardableResult
    public func follow(_ url: URL) -> Bool {
        guard Self.follows(url) else { return false }
        open(url)
        return true
    }
}
