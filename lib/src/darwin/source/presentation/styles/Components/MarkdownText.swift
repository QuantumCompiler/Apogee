import AppKit
import Synchronization
import SwiftUI

/// A model's markdown drawn in the design's terms (lib/design/spec/html/
/// main.html, the chat's answer): prose with emphasis, strong, links and
/// inline code in the mono face on `surface.control`, rounded; headings by
/// the type scale; lists, nested; block quotes; code blocks as a bordered
/// card on `surface.code` under their language's caption, with copy, the
/// code coloured by the language its fence names (`CodeLexer`) in the
/// design's five code colours, and a diff by line; thematic breaks; and
/// tables as a grid.
///
/// Parsed by `MarkdownDocument` -- Foundation's own markdown -- and laid out
/// here. The text is parsed again only when it changes, so a streaming answer
/// is re-read once per redraw, however many deltas arrived since the last.
/// Every block's text is selectable, as a plain answer's is.
///
/// A link opens through `opener` only for `http`, `https` and `mailto`; any
/// other is drawn and not followed, and hovering a link shows where it goes.
/// An image is drawn as its alt text and its address, and never fetched.
public struct MarkdownText: View {
    private let source: String
    private let streaming: Bool
    private let opener: LinkOpener

    /// `source` drawn; `streaming` while it is an answer still arriving, so
    /// its edge is read tolerantly (`MarkdownDocument.init(parsing:streaming:)`).
    public init(_ source: String, streaming: Bool = false, opener: LinkOpener = .system) {
        self.source = source
        self.streaming = streaming
        self.opener = opener
    }

    public var body: some View {
        MarkdownBlocks(source: source, streaming: streaming)
            .equatable()
            .environment(\.openURL, Self.linkAction(opener))
    }

    /// What a click on a link does: `opener` follows it, or nothing does.
    static func linkAction(_ opener: LinkOpener) -> OpenURLAction {
        OpenURLAction { url in
            MainActor.assumeIsolated { opener.follow(url) } ? .handled : .discarded
        }
    }
}

extension LinkOpener {
    /// Opens a followed link with the system: the browser, the mail app.
    public static let system = LinkOpener { url in
        _ = NSWorkspace.shared.open(url)
    }
}

extension MarkdownCode {
    /// Puts the code on `pasteboard` -- the person's, unless a test names its own.
    @MainActor
    public func copy(to pasteboard: NSPasteboard = .general) {
        pasteboard.clearContents()
        pasteboard.setString(text, forType: .string)
    }
}

// MARK: - Blocks

/// The parse and its blocks: compared by its text, so the parse runs only when
/// the text or its streaming changes.
private struct MarkdownBlocks: View, Equatable {
    let source: String
    let streaming: Bool

    var body: some View {
        BlockStack(blocks: MarkdownDocument(parsing: source, streaming: streaming).blocks, tone: .primary, depth: 0)
    }
}

/// How strongly a block's prose reads: a quote's is secondary.
private enum Tone {
    case primary, secondary

    var color: Color {
        switch self {
        case .primary: Palette.textPrimary
        case .secondary: Palette.textSecondary
        }
    }
}

/// Blocks one above the other: apart as paragraphs are at the top, closer
/// inside a list's item.
private struct BlockStack: View {
    let blocks: [MarkdownBlock]
    let tone: Tone
    let depth: Int
    var spacing = Spacing.m

    var body: some View {
        VStack(alignment: .leading, spacing: spacing) {
            ForEach(Array(blocks.enumerated()), id: \.offset) { _, block in
                BlockView(block: block, tone: tone, depth: depth)
            }
        }
    }
}

private struct BlockView: View {
    let block: MarkdownBlock
    let tone: Tone
    /// How deeply lists nest here: an unordered item's bullet follows it.
    let depth: Int

    var body: some View {
        switch block {
        case .paragraph(let spans):
            InlineText(spans: spans, size: TypeScale.prose, color: tone.color)
        case .heading(let level, let spans):
            InlineText(spans: spans, size: Self.size(level), weight: .semibold, color: Palette.textPrimary)
                .accessibilityAddTraits(.isHeader)
        case .list(let list):
            ListView(list: list, tone: tone, depth: depth)
        case .quote(let blocks):
            BlockStack(blocks: blocks, tone: .secondary, depth: depth)
                .padding(.leading, Spacing.m)
                .overlay(alignment: .leading) {
                    Rectangle().fill(Palette.strong).frame(width: Spacing.xxs)
                }
        case .code(let code):
            CodeBlockView(code: code)
        case .rule:
            Rectangle()
                .fill(Palette.border)
                .frame(height: 1)
                .frame(maxWidth: .infinity)
                .padding(.vertical, Spacing.xs)
                .accessibilityHidden(true)
        case .table(let table):
            TableView(table: table, tone: tone)
        }
    }

    /// A heading's step: `#` the largest, `###` and deeper the prose size, set
    /// apart by weight.
    static func size(_ level: Int) -> CGFloat {
        switch level {
        case 1: TypeScale.h1
        case 2: TypeScale.h2
        default: TypeScale.h3
        }
    }
}

/// A list: each item's marker -- its number as written, or a bullet for how
/// deep it sits -- beside the item's blocks.
private struct ListView: View {
    let list: MarkdownList
    let tone: Tone
    let depth: Int

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.xs) {
            ForEach(Array(list.items.enumerated()), id: \.offset) { _, item in
                HStack(alignment: .firstTextBaseline, spacing: Spacing.s) {
                    // The widest marker sets the column, so the items' text lines up.
                    ZStack(alignment: .trailing) {
                        Text(verbatim: widest).hidden()
                        Text(verbatim: marker(item))
                    }
                    .font(.apogeeProse.monospacedDigit())
                    .foregroundStyle(Palette.textTertiary)
                    .accessibilityHidden(true)
                    BlockStack(blocks: item.blocks, tone: tone, depth: depth + 1, spacing: Spacing.xs)
                }
            }
        }
    }

    private func marker(_ item: MarkdownListItem) -> String {
        list.ordered ? "\(item.ordinal)." : Self.bullets[depth % Self.bullets.count]
    }

    private var widest: String {
        list.ordered ? "\(list.items.map(\.ordinal).max() ?? 1)." : Self.bullets[depth % Self.bullets.count]
    }

    static let bullets = ["\u{2022}", "\u{25E6}", "\u{25AA}"]
}

/// A code block as the design draws it: a bordered card on `surface.code`,
/// its caption naming the language with copy beside it, the code in the mono
/// face and coloured by its language, scrolled sideways rather than wrapped.
/// Copy takes the code as written, with no colour.
private struct CodeBlockView: View {
    let code: MarkdownCode
    @State private var copied = false
    @State private var hovering = false

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: Spacing.s) {
                Text(verbatim: code.caption)
                    .lineLimit(1)
                Spacer(minLength: Spacing.s)
                Button {
                    code.copy()
                    copied = true
                } label: {
                    Text(copied ? "copied" : "copy")
                        .padding(.horizontal, Spacing.s)
                        .padding(.vertical, Spacing.xxs)
                        .background(hovering ? Palette.control : .clear, in: RoundedRectangle(cornerRadius: Radius.chip))
                }
                .buttonStyle(.plain)
                .onHover { hovering = $0 }
                .help("Copy the code")
                .accessibilityLabel(copied ? "Copied" : "Copy the code")
            }
            .font(.apogeeMeta.monospaced())
            .foregroundStyle(Palette.textTertiary)
            .padding(.leading, Spacing.m)
            .padding(.trailing, Spacing.xs)
            .padding(.vertical, Spacing.xs)
            Rectangle().fill(Palette.border).frame(height: 1)
            ScrollView(.horizontal) {
                CodeText(code: code)
                    .equatable()
                    .padding(.vertical, Spacing.s)
                    .padding(.horizontal, Spacing.m)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Palette.code, in: RoundedRectangle(cornerRadius: Radius.control))
        .clipShape(RoundedRectangle(cornerRadius: Radius.control))
        .overlay(RoundedRectangle(cornerRadius: Radius.control).strokeBorder(Palette.border))
        .task(id: copied) {
            guard copied else { return }
            try? await Task.sleep(for: .seconds(2))
            copied = false
        }
    }
}

/// The code itself, in the mono face and coloured by its language
/// (`MarkdownText.codeText`), selectable. Compared by its code, so its spans
/// are found again only when the code changes -- once per redraw while the
/// block streams, as the answer's parse is, and never for a hover or a copy.
private struct CodeText: View, Equatable {
    let code: MarkdownCode

    var body: some View {
        MarkdownText.codeText(code)
            .font(.apogeeCode)
            .foregroundStyle(Palette.textPrimary)
            .textSelection(.enabled)
            .fixedSize()
    }
}

extension MarkdownText {
    /// A code block's text as one `Text`: each span of `CodeLexer` in its
    /// colour over the code's own, `text.primary`. A fence naming no language
    /// or one the lexer does not know -- or code with nothing to colour -- is
    /// a plain `Text(verbatim:)`, exactly as before colouring. The colours
    /// are attributes over the code's characters, never characters
    /// themselves: what is selected and copied is the code as written.
    ///
    /// Built as one `NSAttributedString` with each span's colour over its
    /// range -- a fifth of the time of appending an `AttributedString` piece
    /// per run, which a long block streaming would pay on every redraw.
    nonisolated static func codeText(_ code: MarkdownCode) -> Text {
        let spans = CodeLexer.spans(code.text, language: code.language)
        guard !spans.isEmpty else { return Text(verbatim: code.text) }
        let colours = Dictionary(uniqueKeysWithValues: CodeKind.allCases.map { ($0, token(for: $0).nsColor) })
        let coloured = NSMutableAttributedString(string: code.text)
        // The spans count UTF-8 bytes; an NSString counts UTF-16 units.
        let bytes = Array(code.text.utf8)
        var byte = 0
        var unit = 0
        func advance(to target: Int) {
            while byte < target {
                let lead = bytes[byte]
                let width = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2
                unit += width == 4 ? 2 : 1
                byte += width
            }
        }
        for span in spans {
            advance(to: span.range.lowerBound)
            let start = unit
            advance(to: span.range.upperBound)
            if let colour = colours[span.kind] {
                coloured.addAttribute(.foregroundColor, value: colour, range: NSRange(location: start, length: unit - start))
            }
        }
        return Text(AttributedString(coloured))
    }

    /// The token a span of `kind` draws in: the design's five code colours;
    /// in a diff, coloured by line as the design draws a patch, an added line
    /// in the accent, a removed one in the error colour, and its headers and
    /// context in the tertiary text colour.
    nonisolated static func token(for kind: CodeKind) -> ColorToken {
        switch kind {
        case .keyword: Palette.codeKeywordToken
        case .string: Palette.codeStringToken
        case .comment: Palette.codeCommentToken
        case .number: Palette.codeNumberToken
        case .function: Palette.codeFunctionToken
        case .added: Palette.accentToken
        case .removed: Palette.errorToken
        case .header, .context: Palette.textTertiaryToken
        }
    }
}

/// A table as a grid: its header set apart, a line between rows, each column
/// aligned as its delimiter row says.
private struct TableView: View {
    let table: MarkdownTable
    let tone: Tone

    var body: some View {
        Grid(alignment: .leading, horizontalSpacing: 0, verticalSpacing: 0) {
            GridRow {
                ForEach(Array(table.header.enumerated()), id: \.offset) { column, spans in
                    cell(spans, column: column, weight: .semibold, color: Palette.textPrimary)
                        .gridColumnAlignment(Self.alignment(table.alignments[column]))
                }
            }
            ForEach(Array(table.rows.enumerated()), id: \.offset) { _, row in
                Rectangle().fill(Palette.border).frame(height: 1).gridCellUnsizedAxes(.horizontal)
                GridRow {
                    ForEach(Array(row.enumerated()), id: \.offset) { column, spans in
                        cell(spans, column: column, weight: .regular, color: tone.color)
                    }
                }
            }
        }
        .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.control))
        .overlay(RoundedRectangle(cornerRadius: Radius.control).strokeBorder(Palette.border))
    }

    private func cell(_ spans: [MarkdownSpan], column: Int, weight: Font.Weight, color: Color) -> some View {
        InlineText(spans: spans, size: TypeScale.body, weight: weight, color: color, fills: false)
            .padding(.horizontal, Spacing.s)
            .padding(.vertical, Spacing.xs)
    }

    static func alignment(_ alignment: MarkdownTable.Alignment) -> HorizontalAlignment {
        switch alignment {
        case .leading: .leading
        case .center: .center
        case .trailing: .trailing
        }
    }
}

// MARK: - Inline text

/// A paragraph's, a heading's or a cell's spans as one selectable text at the
/// `size` step: strong in semibold, emphasis in italic, inline code in the
/// mono face on a rounded `surface.control`, links underlined, an image as
/// its alt text and address. Hovering a link shows where it goes.
private struct InlineText: View {
    let spans: [MarkdownSpan]
    let size: CGFloat
    var weight: Font.Weight = .regular
    let color: Color
    /// Whether it spans its column, as a paragraph does; a table's cell does not.
    var fills = true
    @State private var places = LinkPlaces()
    @State private var hovered: LinkPlace?
    @State private var width: CGFloat = 0

    var body: some View {
        let text = Self.text(spans, size: size, weight: weight)
            .foregroundStyle(color)
            .tint(color)
            .textSelection(.enabled)
        // The renderer only where it draws something: text with no inline
        // code and no link is drawn exactly as a plain `Text` is.
        if spans.contains(where: { $0.link != nil }) {
            text
                .textRenderer(SpanRenderer(places: places))
                // Across the column, so where a link goes has room to show;
                // leading and top, so the renderer's places stay where they are.
                .frame(maxWidth: fills ? .infinity : nil, alignment: .leading)
                .onContinuousHover { phase in
                    let place: LinkPlace? =
                        switch phase {
                        case .active(let point): places.link(at: point)
                        case .ended: nil
                        }
                    if place != hovered { hovered = place }
                }
                .onGeometryChange(for: CGFloat.self) { $0.size.width } action: { width = $0 }
                .overlay(alignment: .topLeading) {
                    // Always in place, shown only while a link is hovered: a
                    // bubble inserted by the hover took no alignment guides.
                    LinkTarget(place: hovered, within: width)
                }
        } else if spans.contains(where: { $0.style.contains(.code) }) {
            text.textRenderer(SpanRenderer(places: nil))
        } else {
            text
        }
    }

    /// The spans as one `Text`: runs that read alike share an attributed
    /// string; inline code and links are their own pieces, marked for the
    /// renderer, and the pieces are joined by interpolation.
    static func text(_ spans: [MarkdownSpan], size: CGFloat, weight: Font.Weight) -> Text {
        var joined: Text?
        var plain = AttributedString()
        func append(_ next: Text) {
            joined = joined.map { Text("\($0)\(next)") } ?? next
        }
        func flush() {
            guard !plain.characters.isEmpty else { return }
            append(Text(plain))
            plain = AttributedString()
        }
        for span in spans {
            if let image = span.image {
                flush()
                append(imageText(span.text, image, size: size))
                continue
            }
            var piece = AttributedString(span.text)
            piece.swiftUI.font = font(span.style, size: size, weight: weight)
            if span.style.contains(.strikethrough) {
                piece.swiftUI.strikethroughStyle = .single
            }
            guard span.style.contains(.code) || span.link != nil else {
                plain.append(piece)
                continue
            }
            flush()
            if let link = span.link {
                piece.link = link
                piece.swiftUI.underlineStyle = Text.LineStyle(pattern: .solid, color: Palette.strong)
            }
            var text = Text(piece)
            if span.style.contains(.code) { text = text.customAttribute(InlineCodeMark()) }
            if let link = span.link { text = text.customAttribute(LinkMark(url: link)) }
            append(text)
        }
        flush()
        return joined ?? Text(verbatim: "")
    }

    /// A span's face: inline code in the mono face -- the code step in prose
    /// and smaller, a heading's own step in a heading -- strong in semibold,
    /// emphasis in italic.
    static func font(_ style: MarkdownSpan.Style, size: CGFloat, weight: Font.Weight) -> Font {
        let strong: Font.Weight = weight == .regular ? .semibold : .bold
        var font: Font
        if style.contains(.code) {
            let step = size > TypeScale.prose ? size : TypeScale.code
            font = .system(size: TypeScale.scaled(step), weight: style.contains(.strong) ? strong : weight, design: .monospaced)
        } else {
            font = .system(size: TypeScale.scaled(size), weight: style.contains(.strong) ? strong : weight)
        }
        return style.contains(.emphasis) ? font.italic() : font
    }

    /// An image, never fetched: a picture's symbol, its alt text, and its
    /// address in the mono face.
    static func imageText(_ alt: String, _ address: URL, size: CGFloat) -> Text {
        var words = AttributedString(alt.isEmpty ? "image" : alt)
        words.swiftUI.font = .system(size: TypeScale.scaled(size))
        var place = AttributedString(address.absoluteString)
        place.swiftUI.font = .system(size: TypeScale.scaled(TypeScale.code), design: .monospaced)
        place.swiftUI.foregroundColor = Palette.textTertiary
        let symbol = Text(Image(systemName: "photo")).foregroundStyle(Palette.textTertiary)
        return Text("\(symbol) \(Text(words)) \(Text(place))")
    }
}

/// Marks an inline code run, for the renderer to set on its rounded ground.
private struct InlineCodeMark: TextAttribute {}

/// Marks a link's run with where it goes, for the renderer to note where it drew it.
private struct LinkMark: TextAttribute {
    let url: URL
}

/// Draws inline code's rounded ground under its runs, then every run, and
/// notes where each link was drawn. SwiftUI's `Text` says where a run landed
/// only to its renderer.
private struct SpanRenderer: TextRenderer {
    /// Where to note the links; `nil` in text with none.
    let places: LinkPlaces?

    func draw(layout: Text.Layout, in context: inout GraphicsContext) {
        var links: [LinkPlace] = []
        for line in layout {
            for run in line {
                let bounds = run.typographicBounds.rect
                if run[InlineCodeMark.self] != nil {
                    let ground = Path(roundedRect: bounds.insetBy(dx: -Spacing.xxs, dy: 0), cornerRadius: Radius.chip)
                    context.fill(ground, with: .color(Palette.control))
                }
                if let link = run[LinkMark.self] {
                    links.append(LinkPlace(url: link.url, rect: bounds))
                }
            }
        }
        for line in layout {
            for run in line {
                context.draw(run)
            }
        }
        places?.replace(links)
    }
}

/// A link's run and where it was drawn, in its text's own space.
private struct LinkPlace: Equatable, Sendable {
    let url: URL
    let rect: CGRect
}

/// Where a text's links were last drawn: written by its renderer as it draws,
/// read as the pointer moves.
private final class LinkPlaces: Sendable {
    private let places = Mutex<[LinkPlace]>([])

    func replace(_ links: [LinkPlace]) {
        places.withLock { $0 = links }
    }

    func link(at point: CGPoint) -> LinkPlace? {
        places.withLock { $0.first { $0.rect.contains(point) } }
    }
}

/// Where a hovered link goes, drawn just above it -- and, for one a click does
/// not follow, that it is not opened. Nothing shows with no link hovered. It
/// starts where the link does, moved left only as far as keeps it `within`
/// its text's width.
private struct LinkTarget: View {
    let place: LinkPlace?
    let within: CGFloat

    var body: some View {
        let rect = place?.rect ?? .zero
        HStack(spacing: Spacing.xs) {
            Text(verbatim: place?.url.absoluteString ?? "")
                .lineLimit(1)
                .truncationMode(.middle)
            if let url = place?.url, !LinkOpener.follows(url) {
                Text("not opened")
                    .foregroundStyle(Palette.textTertiary)
            }
        }
        .font(.apogeeMeta.monospaced())
        .foregroundStyle(Palette.textSecondary)
        .padding(.horizontal, Spacing.s)
        .padding(.vertical, Spacing.xxs)
        .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.chip))
        .overlay(RoundedRectangle(cornerRadius: Radius.chip).strokeBorder(Palette.border))
        .fixedSize(horizontal: false, vertical: true)
        .alignmentGuide(.leading) { -min(rect.minX, max(0, within - $0.width)) }
        .alignmentGuide(.top) { $0[.bottom] + Spacing.xxs - rect.minY }
        .opacity(place == nil ? 0 : 1)
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }
}

#if DEBUG
#Preview("A whole answer: every shape") {
    ScrollView {
        MarkdownText(StylesPreview.markdownAnswer)
            .frame(width: 720)
            .padding(Spacing.xl)
    }
    .frame(height: 900)
}

#Preview("A code block") {
    MarkdownText(StylesPreview.codeBlock)
        .frame(width: 720)
        .padding(Spacing.xl)
}

#Preview("Mid-stream: an open fence") {
    MarkdownText(StylesPreview.openFence, streaming: true)
        .frame(width: 720)
        .padding(Spacing.xl)
}

#Preview("Mid-stream: a table's header, waiting for its delimiter row") {
    MarkdownText(StylesPreview.tableHeader, streaming: true)
        .frame(width: 720)
        .padding(Spacing.xl)
}

#Preview("A plain answer, as written") {
    MarkdownText(StylesPreview.plainAnswer)
        .frame(width: 720)
        .padding(Spacing.xl)
}

#Preview("Code coloured by language: Graphite Light") {
    ScrollView {
        MarkdownText(StylesPreview.codeAnswer)
            .frame(width: 720)
            .padding(Spacing.xl)
    }
    .frame(height: 900)
    .previewTheme(.light)
}

#Preview("Code coloured by language: Graphite Dark") {
    ScrollView {
        MarkdownText(StylesPreview.codeAnswer)
            .frame(width: 720)
            .padding(Spacing.xl)
    }
    .frame(height: 900)
    .previewTheme(.dark)
}

#Preview("Code coloured by language: High Contrast") {
    ScrollView {
        MarkdownText(StylesPreview.codeAnswer)
            .frame(width: 720)
            .padding(Spacing.xl)
    }
    .frame(height: 900)
    .previewTheme(.contrast)
}

#Preview("A diff, coloured by line") {
    MarkdownText(StylesPreview.fence("```diff"))
        .frame(width: 720)
        .padding(Spacing.xl)
}

#Preview("Mid-stream: a fence open, a string open at the edge") {
    ScrollView {
        MarkdownText(StylesPreview.openString, streaming: true)
            .frame(width: 720)
            .padding(Spacing.xl)
    }
    .frame(height: 900)
}

#Preview("Every line the model wrote: definitions, shifts, pipe-led prose") {
    ScrollView {
        MarkdownText(StylesPreview.edgesAnswer)
            .frame(width: 720)
            .padding(Spacing.xl)
    }
    .frame(height: 900)
}

#Preview("Mid-stream: a short arithmetic line, shown as it arrives") {
    MarkdownText(StylesPreview.arithmeticArriving, streaming: true)
        .frame(width: 720)
        .padding(Spacing.xl)
}

#Preview("Mid-stream: a table's row under prose, shown as it arrives") {
    MarkdownText(StylesPreview.tableRowArriving, streaming: true)
        .frame(width: 720)
        .padding(Spacing.xl)
}

#Preview("Mid-stream: link definitions, kept as they arrive") {
    ScrollView {
        MarkdownText(StylesPreview.definitionsArriving, streaming: true)
            .frame(width: 720)
            .padding(Spacing.xl)
    }
    .frame(height: 900)
}

#Preview("Mid-stream: pipe-led prose, shown past its first line") {
    ScrollView {
        MarkdownText(StylesPreview.pipeProseArriving, streaming: true)
            .frame(width: 720)
            .padding(Spacing.xl)
    }
    .frame(height: 900)
}

#Preview("Ended at a fence opener: the line as written") {
    ScrollView {
        MarkdownText(StylesPreview.endedAtAFence)
            .frame(width: 720)
            .padding(Spacing.xl)
    }
    .frame(height: 900)
}
#endif
