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
public struct MarkdownDocument: Equatable, Sendable {
    /// The blocks, in order.
    public let blocks: [MarkdownBlock]

    /// `source` parsed. `streaming` says it is an answer still arriving, and
    /// reads its edge tolerantly -- what is unfinished there is either drawn
    /// as what it already is or held back a moment, never shown as markers
    /// and then reshaped:
    /// - an open fence is a code block in progress (CommonMark runs an
    ///   unclosed fence to the end of the text);
    /// - an unclosed emphasis or code marker shows literally until it closes;
    /// - a last line that is nothing but markers yet (a fence, a closing
    ///   fence being typed, a rule, a heading's or a list item's marker, a
    ///   table's delimiter row) waits for the rest of its line;
    /// - a last paragraph that opens with a pipe is a table whose delimiter
    ///   row has not arrived, and waits for it.
    public init(parsing source: String, streaming: Bool = false) {
        let text = streaming ? Self.heldBack(source) : source
        let options = AttributedString.MarkdownParsingOptions(
            allowsExtendedAttributes: false, interpretedSyntax: .full,
            failurePolicy: .returnPartiallyParsedIfPossible, languageCode: nil)
        guard let parsed = try? AttributedString(markdown: text, options: options) else {
            // Never lose an answer: what cannot be read is shown as written.
            blocks = text.isEmpty ? [] : [.paragraph([MarkdownSpan(text)])]
            return
        }
        var blocks = Self.blocks(of: Self.tree(parsed))
        if streaming, case .paragraph(let spans)? = blocks.last, spans.first?.text.hasPrefix("|") == true {
            blocks.removeLast()
        }
        self.blocks = blocks
    }

    /// A document of `blocks`, as a test or a preview states one.
    public init(blocks: [MarkdownBlock]) {
        self.blocks = blocks
    }

    // MARK: - The stream's edge

    /// What a line holds while it is still only a block's markers: fences,
    /// rules, setext underlines, heading, quote and list markers, a table's
    /// delimiter row.
    static let markers = Set(" \t`~-=*_#|>+:.)0123456789")

    /// `source` with its last line held back while that line is non-empty and
    /// nothing but markers: "Text\n-" is a heading until the next character
    /// says it is a list, and "```python\nx\n``" is a closing fence being
    /// typed, not code.
    static func heldBack(_ source: String) -> String {
        let start = source.lastIndex(of: "\n").map(source.index(after:)) ?? source.startIndex
        let line = source[start...]
        guard !line.isEmpty, line.allSatisfy(markers.contains) else { return source }
        return String(source[..<start])
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
    /// long as their intents' identities agree, outermost first.
    private static func tree(_ parsed: AttributedString) -> Node {
        let root = Node(.root, identity: 0)
        for run in parsed.runs {
            let span = MarkdownSpan(String(parsed[run.range].characters), run.inlinePresentationIntent ?? [], link: run.link, image: run.imageURL)
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
