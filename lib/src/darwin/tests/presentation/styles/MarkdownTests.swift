import AppKit
import Foundation
@testable import PresentationStyles
import SwiftUI
import Synchronization
import Testing

/// A recorded fixture, by its path under `tests/`.
private func recorded(_ path: String) -> String {
    let tests = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
    return (try? String(contentsOf: tests.appendingPathComponent(path), encoding: .utf8)) ?? ""
}

/// A recorded stream's events, each line decoded.
private func events(_ path: String) -> [[String: Any]] {
    recorded(path).split(separator: "\n").compactMap { line in
        try? JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any]
    }
}

/// The `answer_delta` texts of a recorded stream, in order: the pieces an
/// answer arrives in.
private func deltas(_ path: String) -> [String] {
    events(path).compactMap { $0["type"] as? String == "answer_delta" ? $0["text"] as? String : nil }
}

/// The scripted mock's markdown answer, recorded from the branch's binary
/// driven as the app drives a chat (tests/data/protocol/Fixtures/
/// markdown-answer.jsonl): 150 deltas of eight bytes, every shape the app
/// draws -- and that chat's `chats show` (tests/business/reads/Fixtures/
/// chats-show-markdown.json).
private let markdownStream = "data/protocol/Fixtures/markdown-answer.jsonl"
private let markdownChat = "business/reads/Fixtures/chats-show-markdown.json"

private func plain(_ text: String) -> MarkdownSpan { MarkdownSpan(text) }
private func code(_ text: String) -> MarkdownSpan { MarkdownSpan(text, style: .code) }
private func link(_ text: String, _ url: String) -> MarkdownSpan { MarkdownSpan(text, link: URL(string: url)!) }
private func item(_ ordinal: Int, _ blocks: MarkdownBlock...) -> MarkdownListItem { MarkdownListItem(ordinal: ordinal, blocks: blocks) }

/// Every block of the recorded answer, as the app lays it out.
private let recordedBlocks: [MarkdownBlock] = [
    .heading(level: 2, [plain("Timeouts that keep the batch going")]),
    .paragraph([
        plain("Catch the timeout "), MarkdownSpan("inside", style: .strong), plain(" the child, not around the group. The "),
        code("TaskGroup"), plain(" stays "), MarkdownSpan("all-or-nothing", style: .emphasis),
        plain(" for real failures, and a slow host just logs and moves on:"),
    ]),
    .code(MarkdownCode(language: "python", text: """
        async def _fetch(self, url: str) -> bytes | None:
            try:
                async with asyncio.timeout(10):
                    return await self.client.get(url)
            except TimeoutError:
                log.warning("timed out: %s", url)  # the batch keeps going
                return None
        """)),
    .paragraph([plain("What changes:")]),
    .list(MarkdownList(ordered: false, items: [
        item(1,
             .paragraph([code("_fetch()"), plain(" returns "), code("None"), plain(" on a timeout")]),
             .list(MarkdownList(ordered: false, items: [
                 item(1, .paragraph([plain("the caller skips it")])),
                 item(2, .paragraph([plain("the log keeps the address")])),
             ]))),
        item(2, .paragraph([plain("every other failure still cancels the group")])),
    ])),
    .list(MarkdownList(ordered: true, items: [
        item(1, .paragraph([plain("Run the scraper again.")])),
        item(2, .paragraph([plain("Check the log for "), code("timed out"), plain(".")])),
    ])),
    .quote([.paragraph([plain("A cancel from outside still arrives as "), code("CancelledError"), plain(", so shutdown works.")])]),
    .table(MarkdownTable(
        alignments: [.leading, .center, .leading],
        header: [[plain("Option")], [plain("Keeps the batch")], [plain("Cancels on failure")]],
        rows: [
            [[code("asyncio.timeout"), plain(" in the child")], [plain("yes")], [plain("yes")]],
            [[code("gather(..., return_exceptions=True)")], [plain("yes")], [plain("no")]],
        ])),
    .rule,
    .paragraph([
        plain("See the "), link("asyncio docs", "https://docs.python.org/3/library/asyncio-task.html"), plain(" or "),
        link("write to the team", "mailto:team@example.com"), plain("; the notes are in "),
        link("runner.md", "file:///Users/someone/Documents/runner.md"), plain("."),
    ]),
    .paragraph([MarkdownSpan("The batch timeline", image: URL(string: "https://example.com/timeline.png")!)]),
]

/// What a block shows that would be a marker left raw: a closed pair of
/// emphasis, strong or code markers, or a link, read as plain text; a block's
/// own marker read as a paragraph's words; a fence inside a code block's code.
private func rawMarkers(in blocks: [MarkdownBlock]) -> [String] {
    let closed = [#"\*\*\S[^*]*\*\*"#, #"\*[^\s*][^*]*\*"#, "`[^`]+`", #"\[[^\]]+\]\([^)]+\)"#].map { try! Regex($0) }
    func spans(_ spans: [MarkdownSpan]) -> [String] {
        spans.filter { !$0.style.contains(.code) }.flatMap { span in
            closed.compactMap { span.text.contains($0) ? "a closed marker read raw: \(span.text)" : nil }
        }
    }
    return blocks.flatMap { block -> [String] in
        switch block {
        case .paragraph(let content):
            let text = content.map(\.text).joined()
            let opener = ["#", "|", "```", "~~~", "> ", "- ", "* ", "1. "].first { text.hasPrefix($0) }
            return spans(content) + (opener == nil ? [] : ["a block's marker read as words: \(text)"])
        case .heading(_, let content):
            return spans(content)
        case .list(let list):
            return list.items.flatMap { rawMarkers(in: $0.blocks) }
        case .quote(let inner):
            return rawMarkers(in: inner)
        case .code(let code):
            return code.text.split(separator: "\n").contains { $0.allSatisfy { "`~".contains($0) } }
                ? ["a fence read as code: \(code.text)"] : []
        case .table(let table):
            return (table.header + table.rows.flatMap { $0 }).flatMap(spans)
        case .rule:
            return []
        }
    }
}

/// The scripted mock's edge-case answer (4d), recorded from the branch's
/// binary driven as the app drives a chat (tests/data/protocol/Fixtures/
/// edges-answer.jsonl): 102 deltas of eight bytes -- a short arithmetic line,
/// a table straight under prose, a here-document and shifts in shell and
/// Ruby, a reference use and its link definitions, pipe-led prose, and a
/// last line of one number -- and that chat's `chats show`
/// (tests/business/reads/Fixtures/chats-show-edges.json).
private let edgesStream = "data/protocol/Fixtures/edges-answer.jsonl"
private let edgesChat = "business/reads/Fixtures/chats-show-edges.json"

/// Every block of the recorded edge-case answer, finished: every line the
/// model wrote, the link definitions among them.
private let edgesBlocks: [MarkdownBlock] = [
    .heading(level: 2, [plain("Sizing the batch")]),
    .paragraph([plain("Start from the arithmetic:")]),
    .paragraph([plain("128 * 8 = 1024")]),
    .paragraph([plain("So one host takes 1024 rows, and 2 + 2 = 4 workers share them. Per worker:")]),
    .table(MarkdownTable(
        alignments: [.leading, .trailing, .trailing],
        header: [[plain("Workers")], [plain("Rows each")], [plain("Total")]],
        rows: [
            [[plain("2")], [plain("512")], [plain("1024")]],
            [[plain("4")], [plain("256")], [plain("1024")]],
        ])),
    .paragraph([plain("The loader writes its settings with a here-document, and sizes its mask with shifts:")]),
    .code(MarkdownCode(language: "bash", text: """
        cat <<EOF > batch.conf
        rows=1024
        EOF
        echo $((a<<b))
        (( mask = 1 <<bits ))
        echo "done"
        """)),
    .paragraph([plain("In Ruby, "), code("<<"), plain(" appends; a here-document's marker follows a space:")]),
    .code(MarkdownCode(language: "ruby", text: """
        list<<Const
        total = 1 << 2
        sql = <<~SQL
          SELECT count(*) FROM rows
        SQL
        puts sql
        """)),
    .paragraph([plain("See [the sizing guide][1] and [the loader's notes][2].")]),
    .paragraph([
        plain("[1]: "), link("https://a.example/docs", "https://a.example/docs"),
        plain("\n[2]: "), link("https://b.example/guide", "https://b.example/guide"), plain(" \"Loader notes\""),
    ]),
    .paragraph([plain("|x| is the absolute value of x, so |-3| = 3.\n|rows - 1024| is how far a host is from full.")]),
    .paragraph([plain("The answer, then:")]),
    .paragraph([plain("42")]),
]

/// What the CLI's terminal renderer prints for the recorded answer's
/// reference use and link definitions (lib/src/cli/source/presentation/
/// markdown/), the oracle for a line Foundation would swallow: each is an
/// ordinary line (`process` falls through to `ordinary`,
/// stream_renderer.cpp:517, which renders it inline as written, :599);
/// `link()` takes only the inline form, a closing bracket followed by a
/// parenthesis (inline.cpp:187), so `[text][1]` and `[1]:` stay as written;
/// and the address after `[1]: ` is a bare URL, underlined as a link
/// (`bare_url`, inline.cpp:284). Each row: its text, and the runs it
/// underlines. The branch's binary, rendering the recorded answer on a
/// terminal, prints exactly these rows.
private let cliRows: [(text: String, links: [String])] = [
    ("See [the sizing guide][1] and [the loader's notes][2].", []),
    ("[1]: https://a.example/docs", ["https://a.example/docs"]),
    ("[2]: https://b.example/guide \"Loader notes\"", ["https://b.example/guide"]),
]

/// A block's lines as they read, one per row: its spans' text, split where
/// the answer broke the line; a table's rows its cells.
private func lines(_ block: MarkdownBlock) -> [String] {
    switch block {
    case .paragraph(let spans), .heading(_, let spans):
        return spans.map(\.text).joined().components(separatedBy: "\n")
    case .list(let list):
        return list.items.flatMap { $0.blocks.flatMap(lines) }
    case .quote(let blocks):
        return blocks.flatMap(lines)
    case .code(let code):
        return code.text.components(separatedBy: "\n")
    case .table(let table):
        return ([table.header] + table.rows).map { $0.map { $0.map(\.text).joined() }.joined(separator: " | ") }
    case .rule:
        return []
    }
}

/// Every line `blocks` show, in order.
private func shownLines(_ blocks: [MarkdownBlock]) -> [String] {
    blocks.flatMap(lines)
}

/// `text` streamed as a parse sees it at every cut on a character's boundary.
private func everyPrefix(_ text: String) -> [String] {
    var prefixes: [String] = []
    var prefix = ""
    for character in text {
        prefix.append(character)
        prefixes.append(prefix)
    }
    return prefixes
}

/// Every network request made while it is registered, refused rather than
/// sent: a test's proof that nothing was fetched.
private final class RecordingProtocol: URLProtocol, @unchecked Sendable {
    static let seen = Mutex<[URL]>([])

    override class func canInit(with request: URLRequest) -> Bool {
        seen.withLock { $0.append(request.url ?? URL(fileURLWithPath: "/")) }
        return true
    }

    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        client?.urlProtocol(self, didFailWithError: URLError(.notConnectedToInternet))
    }

    override func stopLoading() {}
}

/// Where a test's opener puts the links it was asked to follow.
private final class Followed: Sendable {
    let urls = Mutex<[URL]>([])
    var opener: LinkOpener {
        LinkOpener { url in self.urls.withLock { $0.append(url) } }
    }
    var all: [URL] { urls.withLock { $0 } }
}

@Suite("Markdown", .serialized)
struct MarkdownTests {
    @Test("the recorded answer's blocks, every shape pinned: heading, prose, code, nested and ordered lists, quote, table, rule, links, image")
    func recordedAnswer() throws {
        let answer = deltas(markdownStream).joined()
        #expect(deltas(markdownStream).count == 150)
        #expect(MarkdownDocument(parsing: answer).blocks == recordedBlocks)
        let result = try #require(events(markdownStream).first { $0["type"] as? String == "result" }?["text"] as? String)
        #expect(result == answer, "the result's text is the deltas joined")
    }

    @Test("read back by chats show, the answer is the live one's text, so it draws block for block the same")
    func readBack() throws {
        let document = try #require(try JSONSerialization.jsonObject(with: Data(recorded(markdownChat).utf8)) as? [String: Any])
        let turns = try #require(document["turns"] as? [[String: Any]])
        let answers = turns.compactMap { ($0["answer"] as? [String: Any])?["text"] as? String }
        #expect(answers == [deltas(markdownStream).joined()])
        #expect(MarkdownDocument(parsing: answers[0]).blocks == recordedBlocks)

        // M7's read-back conversation, whose answers carried markdown shown raw.
        let earlier = try #require(try JSONSerialization.jsonObject(with: Data(recorded("business/reads/Fixtures/chats-show.json").utf8)) as? [String: Any])
        let saved = (earlier["turns"] as? [[String: Any]] ?? []).compactMap { ($0["answer"] as? [String: Any])?["text"] as? String }
        #expect(MarkdownDocument(parsing: saved[2]).blocks == [.paragraph([
            plain("notes.md holds two items: "), MarkdownSpan("buy milk", style: .strong), plain(" and "),
            MarkdownSpan("call the plumber", style: .emphasis), plain("."),
        ])])
        #expect(MarkdownDocument(parsing: saved[4]).blocks == [
            .paragraph([plain("The report says the launch moved to "), MarkdownSpan("November", style: .strong), plain(":")]),
            .list(MarkdownList(ordered: false, items: [
                item(1, .paragraph([plain("the beta ends on 2026-10-31")])),
                item(2, .paragraph([plain("the launch is on 2026-11-12")])),
            ])),
        ])
    }

    @Test("streamed in its recorded pieces: no prefix shows a closed marker raw, and no block before the edge ever changes")
    func streamedInPieces() {
        let pieces = deltas(markdownStream)
        var text = ""
        for (index, piece) in pieces.enumerated() {
            text += piece
            let blocks = MarkdownDocument(parsing: text, streaming: true).blocks
            #expect(blocks.count <= recordedBlocks.count, "delta \(index)")
            #expect(Array(blocks.dropLast()) == Array(recordedBlocks.prefix(max(0, blocks.count - 1))), "delta \(index): a settled block changed")
            for problem in rawMarkers(in: blocks) {
                Issue.record("delta \(index): \(problem)")
            }
        }
        #expect(MarkdownDocument(parsing: text, streaming: true).blocks.count == recordedBlocks.count)
        #expect(MarkdownDocument(parsing: text).blocks == recordedBlocks, "finished, it is the whole answer")
    }

    @Test("an open fence at the stream's edge is a code block in progress, its language named; a closing fence being typed is not code")
    func openFence() {
        var text = ""
        for piece in deltas(markdownStream) {
            text += piece
            if text.contains("self.client.get") { break }
        }
        guard case .code(let inProgress)? = MarkdownDocument(parsing: text, streaming: true).blocks.last else {
            Issue.record("the recorded prefix ends in a code block")
            return
        }
        #expect(inProgress.language == "python")
        #expect(inProgress.text.hasPrefix("async def _fetch(self, url: str) -> bytes | None:\n    try:\n"))
        #expect(inProgress.text.contains("self.client.get"))

        let block: [MarkdownBlock] = [.code(MarkdownCode(language: "python", text: "x = 1"))]
        for edge in ["```python\nx = 1", "```python\nx = 1\n", "```python\nx = 1\n`", "```python\nx = 1\n``", "```python\nx = 1\n```", "```python\nx = 1\n```\n"] {
            #expect(MarkdownDocument(parsing: edge, streaming: true).blocks == block, "\(edge.debugDescription)")
        }
        // A fence with no code yet draws nothing: its line is never shown raw.
        for edge in ["Intro\n\n`", "Intro\n\n``", "Intro\n\n```", "Intro\n\n```py", "Intro\n\n```python\n"] {
            #expect(MarkdownDocument(parsing: edge, streaming: true).blocks == [.paragraph([plain("Intro")])], "\(edge.debugDescription)")
        }
    }

    @Test("the stream's edge: a line of markers waits for the rest of it, a table's header for its delimiter row, an open marker shows as written")
    func edge() {
        func streaming(_ text: String) -> [MarkdownBlock] { MarkdownDocument(parsing: text, streaming: true).blocks }
        let text: [MarkdownBlock] = [.paragraph([plain("Text")])]
        // Finished, "Text\n-" is a heading (CommonMark's setext); streaming, the dash waits.
        #expect(MarkdownDocument(parsing: "Text\n-").blocks == [.heading(level: 2, [plain("Text")])])
        #expect(streaming("Text\n-") == text)
        #expect(streaming("Text\n- ") == text)
        #expect(streaming("Text\n- a") == text + [.list(MarkdownList(ordered: false, items: [item(1, .paragraph([plain("a")]))]))])
        #expect(streaming("Text\n\n--") == text, "a rule on its way")
        #expect(streaming("Text\n\n---\n") == text + [.rule])
        #expect(streaming("Text\n\n1.") == text, "a list's number waits for its words")
        #expect(streaming("Text\n\n#") == text, "a heading's marker waits for its words")
        #expect(streaming("Text\n\n> ") == text, "a quote's marker waits for its words")

        let header = MarkdownTable(alignments: [.leading, .leading], header: [[plain("a")], [plain("b")]], rows: [])
        #expect(streaming("Text\n\n| a | b |") == text, "a table's header waits for its delimiter row")
        #expect(streaming("Text\n\n| a | b |\n|---|--") == text)
        #expect(streaming("Text\n\n| a | b |\n|---|---|\n") == text + [.table(header)])
        #expect(MarkdownDocument(parsing: "| a | b |").blocks == [.paragraph([plain("| a | b |")])], "finished, a lone row is what was written")

        #expect(streaming("a **bol") == [.paragraph([plain("a **bol")])], "an open marker shows as written")
        #expect(streaming("a **bold**") == [.paragraph([plain("a "), MarkdownSpan("bold", style: .strong)])])
        #expect(streaming("a `co") == [.paragraph([plain("a `co")])])
    }

    @Test("a plain answer reads as written: one paragraph, one unstyled span, its line breaks kept")
    func plainAnswers() {
        for path in [
            "data/protocol/Fixtures/resumed-chat.jsonl", "data/protocol/Fixtures/mock-actor.jsonl",
            "data/protocol/Fixtures/execute-play.jsonl", "data/protocol/Fixtures/execute-orchestrate.jsonl",
        ] {
            let answer = deltas(path).joined()
            #expect(!answer.isEmpty, "\(path)")
            #expect(MarkdownDocument(parsing: answer).blocks == [.paragraph([plain(answer)])], "\(path)")
        }
        #expect(deltas("data/protocol/Fixtures/execute-orchestrate.jsonl").joined().contains("\n"), "one recorded plain answer breaks a line")
        #expect(MarkdownDocument(parsing: "one\ntwo\n\nthree").blocks == [.paragraph([plain("one\ntwo")]), .paragraph([plain("three")])])
        #expect(MarkdownDocument(parsing: "").blocks.isEmpty)
        #expect(MarkdownDocument(parsing: " \n ").blocks.isEmpty)
    }

    @Test("what the parser places in no block, and inline HTML, stay as written; lists keep their numbers; quotes nest; tables pad short rows")
    func structure() {
        #expect(MarkdownDocument(parsing: "<div>\nblock\n</div>").blocks == [.paragraph([plain("<div>\nblock\n</div>")])])
        #expect(MarkdownDocument(parsing: "a <b>x</b>").blocks == [.paragraph([plain("a <b>x</b>")])])
        #expect(MarkdownDocument(parsing: "3. a\n4. b").blocks == [.list(MarkdownList(ordered: true, items: [
            item(3, .paragraph([plain("a")])), item(4, .paragraph([plain("b")])),
        ]))])
        #expect(MarkdownDocument(parsing: "> - q1\n> - q2\n>\n> > deep").blocks == [.quote([
            .list(MarkdownList(ordered: false, items: [item(1, .paragraph([plain("q1")])), item(2, .paragraph([plain("q2")]))])),
            .quote([.paragraph([plain("deep")])]),
        ])])
        #expect(MarkdownDocument(parsing: "| a | b |\n|--:|---|\n| 1 |").blocks == [.table(MarkdownTable(
            alignments: [.trailing, .leading], header: [[plain("a")], [plain("b")]], rows: [[[plain("1")], []]]))])
        #expect(MarkdownDocument(parsing: "~~gone~~").blocks == [.paragraph([MarkdownSpan("gone", style: .strikethrough)])])
        #expect(MarkdownDocument(parsing: "```\nx\n```").blocks == [.code(MarkdownCode(language: nil, text: "x"))])
        #expect(MarkdownCode(language: nil, text: "x").caption == "code")
    }

    @Test("a link is followed only for http, https and mailto: file: and every other scheme are drawn and never opened")
    @MainActor
    func linkSchemes() {
        let followed = Followed()
        let opener = followed.opener
        let yes = ["https://docs.python.org/3/library/asyncio-task.html", "http://example.com", "mailto:team@example.com", "HTTPS://EXAMPLE.COM"]
        let no = [
            "file:///Users/someone/Documents/runner.md", "file:///etc/passwd", "ftp://example.com/x", "javascript:alert(1)",
            "x-apple.systempreferences:com.apple.preference.security", "ssh://host", "tel:123", "runner.md", "/etc/passwd",
        ]
        for url in yes { #expect(opener.follow(URL(string: url)!), "\(url)") }
        for url in no { #expect(!opener.follow(URL(string: url)!), "\(url)") }
        #expect(followed.all == yes.map { URL(string: $0)! })
    }

    @Test("a click on the recorded answer's links, as the view's open action takes it: the web and mail opened, the file: link not")
    @MainActor
    func linkClicks() {
        let followed = Followed()
        let action = MarkdownText.linkAction(followed.opener)
        guard case .paragraph(let spans) = recordedBlocks[recordedBlocks.count - 2] else {
            Issue.record("the links' paragraph")
            return
        }
        let links = spans.compactMap(\.link)
        #expect(links.map(\.scheme) == ["https", "mailto", "file"])
        for url in links { action(url) }
        #expect(followed.all == Array(links.prefix(2)))
    }

    @Test("an image is drawn as its alt text and its address, never fetched: drawing the recorded answer asks the network for nothing")
    @MainActor
    func imageNeverFetched() {
        guard case .paragraph(let spans)? = recordedBlocks.last else {
            Issue.record("the image's paragraph")
            return
        }
        #expect(spans == [MarkdownSpan("The batch timeline", image: URL(string: "https://example.com/timeline.png")!)])

        RecordingProtocol.seen.withLock { $0 = [] }
        URLProtocol.registerClass(RecordingProtocol.self)
        defer { URLProtocol.unregisterClass(RecordingProtocol.self) }
        let followed = Followed()
        let renderer = ImageRenderer(content: MarkdownText(deltas(markdownStream).joined(), opener: followed.opener).frame(width: 720))
        #expect(renderer.cgImage != nil, "the answer was drawn")
        #expect(RecordingProtocol.seen.withLock { $0 }.isEmpty, "nothing was fetched")
        #expect(followed.all.isEmpty, "nothing was opened")

        // The recorder sees a fetch when one is made -- refused, never sent.
        let control = URL(string: "https://example.com/control.png")!
        #expect(NSImage(contentsOf: control) == nil)
        #expect(RecordingProtocol.seen.withLock { $0 }.contains(control), "the network check would have seen a fetch")
    }

    @Test("copy puts the code on the pasteboard exactly: no fences, no caption")
    @MainActor
    func copyCode() {
        guard case .code(let code) = recordedBlocks[2] else {
            Issue.record("the code block")
            return
        }
        #expect(code.caption == "python")
        let pasteboard = NSPasteboard(name: NSPasteboard.Name("apogee-markdown-tests-\(UUID().uuidString)"))
        defer { pasteboard.releaseGlobally() }
        code.copy(to: pasteboard)
        #expect(pasteboard.string(forType: .string) == code.text)
        #expect(code.text.hasPrefix("async def") && code.text.hasSuffix("return None"))
    }

    // MARK: - Every line the model wrote (4d)

    @Test("the audit's probes, streaming and finished: link definitions, short arithmetic answers, pipe-led prose and a bare fence opener all reach the screen")
    func probeMatrix() {
        let sources = MarkdownBlock.paragraph([plain("Sources:")])
        let definitions = MarkdownBlock.paragraph([
            plain("[1]: "), link("https://a.example/docs", "https://a.example/docs"),
            plain("\n[2]: "), link("https://b.example/guide", "https://b.example/guide"),
        ])
        let table = MarkdownBlock.table(MarkdownTable(
            alignments: [.leading, .leading], header: [[plain("a")], [plain("b")]], rows: [[[plain("1")], [plain("2")]]]))
        let probes: [(source: String, streaming: [MarkdownBlock], finished: [MarkdownBlock])] = [
            // 1. Reference-style link definitions, kept as the text lines they are, at every step of the stream.
            ("Sources:\n\n[1]: https://a.example/docs\n[2]: https://b.example/guide", [sources, definitions], [sources, definitions]),
            ("Sources:\n\n[2", [sources, .paragraph([plain("[2")])], [sources, .paragraph([plain("[2")])]),
            ("Sources:\n\n[2]:", [sources, .paragraph([plain("[2]:")])], [sources, .paragraph([plain("[2]:")])]),
            ("Sources:\n\n[2]: https://b.ex", [sources, .paragraph([plain("[2]: "), link("https://b.ex", "https://b.ex")])],
             [sources, .paragraph([plain("[2]: "), link("https://b.ex", "https://b.ex")])]),
            // 2. Short answers made of digits, spaces and operators: content, never held.
            ("128 * 8 = 1024", [.paragraph([plain("128 * 8 = 1024")])], [.paragraph([plain("128 * 8 = 1024")])]),
            ("42", [.paragraph([plain("42")])], [.paragraph([plain("42")])]),
            ("2 + 2 = 4", [.paragraph([plain("2 + 2 = 4")])], [.paragraph([plain("2 + 2 = 4")])]),
            // 3. Pipe-led prose: only a table's first row waits, for its next line.
            ("|x| means the absolute value", [], [.paragraph([plain("|x| means the absolute value")])]),
            ("|x| means the absolute value\nof x.", [.paragraph([plain("|x| means the absolute value\nof x.")])],
             [.paragraph([plain("|x| means the absolute value\nof x.")])]),
            ("+---+---+\n| a | b |\n+---+---+\n", [.paragraph([plain("+---+---+\n| a | b |\n+---+---+")])],
             [.paragraph([plain("+---+---+\n| a | b |\n+---+---+")])]),
            ("|-- src\n|   |-- main.swift\n|-- tests", [.paragraph([plain("|-- src\n|   |-- main.swift\n|-- tests")])],
             [.paragraph([plain("|-- src\n|   |-- main.swift\n|-- tests")])]),
            ("| a | b |\n|---|---|\n| 1 | 2 |", [table], [table]),
            // 4. A bare fence opener: nothing yet while it streams, the line as written once the answer ends.
            ("```python", [], [.paragraph([plain("```python")])]),
        ]
        for probe in probes {
            #expect(MarkdownDocument(parsing: probe.source, streaming: true).blocks == probe.streaming, "streaming \(probe.source.debugDescription)")
            #expect(MarkdownDocument(parsing: probe.source).blocks == probe.finished, "finished \(probe.source.debugDescription)")
        }
    }

    @Test("link definitions read as the CLI prints them: each a line of text, its address a link, a reference use as written")
    func linkDefinitionsAsTheCLIPrintsThem() {
        let answer = deltas(edgesStream).joined()
        for blocks in [MarkdownDocument(parsing: answer).blocks, MarkdownDocument(parsing: answer, streaming: true).blocks] {
            let shown = shownLines(blocks)
            for row in cliRows {
                #expect(shown.contains(row.text), "the CLI's row \(row.text.debugDescription) is shown")
            }
            let paragraphs = blocks.compactMap { if case .paragraph(let spans) = $0 { spans } else { nil } }
            let links = paragraphs.filter { $0.contains { $0.link != nil } }
            #expect(links.count == 1, "the definitions are the one paragraph with links")
            #expect(links.first?.compactMap { $0.link == nil ? nil : $0.text } == cliRows.flatMap(\.links), "the CLI's underlined runs are the links")
            #expect(links.first?.compactMap(\.link?.absoluteString) == cliRows.flatMap(\.links), "each goes where it says")
            #expect(paragraphs.contains { $0 == [plain(cliRows[0].text)] }, "a reference use is resolved into no link")
        }

        // The CLI's other forms: a footnote, an address in angle brackets, a
        // title, a definition with no space; inside a quote and a list item.
        let cases: [(String, [MarkdownBlock])] = [
            ("See [the docs][1].\n\n[1]: https://a.example/docs", [
                .paragraph([plain("See [the docs][1].")]),
                .paragraph([plain("[1]: "), link("https://a.example/docs", "https://a.example/docs")]),
            ]),
            ("A note[^1].\n\n[^1]: The batch is sized for one host.", [
                .paragraph([plain("A note[^1].")]), .paragraph([plain("[^1]: The batch is sized for one host.")]),
            ]),
            ("[1]: <https://a.example>", [.paragraph([plain("[1]: "), link("https://a.example", "https://a.example")])]),
            // Written with no space, the address is linked by Foundation's own
            // autolinking, which links after a colon where the CLI's does not
            // (inline.cpp:287): the text is kept either way.
            ("[1]:https://a.example", [.paragraph([plain("[1]:"), link("https://a.example", "https://a.example")])]),
            ("  [1]: ./notes.md \"Notes\"", [.paragraph([plain("[1]: ./notes.md \"Notes\"")])]),
            ("> [1]: ./notes.md", [.quote([.paragraph([plain("[1]: ./notes.md")])])]),
            ("- [1]: ./notes.md", [.list(MarkdownList(ordered: false, items: [item(1, .paragraph([plain("[1]: ./notes.md")]))]))]),
        ]
        for (source, blocks) in cases {
            #expect(MarkdownDocument(parsing: source).blocks == blocks, "\(source.debugDescription)")
            #expect(MarkdownDocument(parsing: source, streaming: true).blocks == blocks, "streaming \(source.debugDescription)")
        }
        // In code they are code, exactly: nothing added, nothing taken.
        for fenced in ["```md\n[1]: https://a.example/docs\nsee [x][1]\n```", "`[1]: x`", "    [1]: x", "> [1]: x\n> [2]: y"] {
            for streaming in [false, true] {
                let shown = shownLines(MarkdownDocument(parsing: fenced, streaming: streaming).blocks).joined()
                #expect(Set(shown).isSubset(of: Set(fenced)), "\(fenced.debugDescription): nothing shown the answer did not write")
            }
        }
        #expect(MarkdownDocument(parsing: "```md\n[1]: https://a.example/docs\n```").blocks == [.code(MarkdownCode(language: "md", text: "[1]: https://a.example/docs"))])
        #expect(MarkdownDocument(parsing: "`[1]: x`").blocks == [.paragraph([code("[1]: x")])])
    }

    @Test("a link definition never flickers away while it streams: every cut shows every character of it that has arrived")
    func definitionsStream() {
        let answer = "Sources:\n\n[1]: https://a.example/docs\n[2]: https://b.example/guide \"Guide\"\n"
        let start = answer.range(of: "[1]")!.lowerBound
        for prefix in everyPrefix(answer) where prefix.endIndex > start {
            let arrived = prefix[start...].components(separatedBy: "\n").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
            let shown = shownLines(MarkdownDocument(parsing: prefix, streaming: true).blocks)
            for line in arrived {
                #expect(shown.contains(line), "\(prefix.debugDescription): \(line.debugDescription) is shown")
            }
        }
    }

    @Test("a finished content line is never held: only a construct's own marker line waits at the stream's edge")
    func onlyMarkersWait() {
        let text: [MarkdownBlock] = [.paragraph([plain("Text")])]
        let markers = [
            "`", "``", "```", "~~~", "*", "**", "#", "## ", "-", "- ", "--", "---", "* * *", "_", "=", "==", "+",
            "1.", "12) ", ">", "> ", "> - ", "|", "|---|--", ":-:|", "   ",
        ]
        for marker in markers {
            #expect(MarkdownDocument(parsing: "Text\n\n" + marker, streaming: true).blocks == text, "\(marker.debugDescription) waits")
        }
        let content = [
            "42", "128 * 8 = 1024", "2 + 2 = 4", "1", "2026", "3.14", "-1", "+1", "- 5", "...", ":)", "= 4", "#hashtag",
            "1.5x", "> 42", "x",
        ]
        for line in content {
            for source in [line, "Text\n\n" + line] {
                let streaming = MarkdownDocument(parsing: source, streaming: true).blocks
                #expect(streaming == MarkdownDocument(parsing: source).blocks, "\(source.debugDescription) is shown as it will finish")
                #expect(!streaming.isEmpty)
            }
        }
        // A table's completed row is shown at once, a lone pipe starting the next waits.
        let row = MarkdownTable(alignments: [.leading, .leading], header: [[plain("a")], [plain("b")]], rows: [[[plain("1")], [plain("2")]]])
        #expect(MarkdownDocument(parsing: "| a | b |\n|---|---|\n| 1 | 2 |", streaming: true).blocks == [.table(row)])
        #expect(MarkdownDocument(parsing: "| a | b |\n|---|---|\n| 1 | 2 |\n|", streaming: true).blocks == [.table(row)])
    }

    @Test("the pipe rule is bounded to a table's first two lines: a header waits for its delimiter row, and pipe-led prose shows once it has gone further")
    func pipeRuleBounded() {
        func streaming(_ text: String) -> [MarkdownBlock] { MarkdownDocument(parsing: text, streaming: true).blocks }
        let header = MarkdownTable(alignments: [.leading, .leading], header: [[plain("a")], [plain("b")]], rows: [])
        // A table's first row, and its delimiter row while it is typed.
        for edge in ["| a | b |", "| a | b |\n", "| a | b |\n|", "| a | b |\n|---", "| a | b |\n|---|---|"] {
            #expect(streaming(edge) == [], "\(edge.debugDescription) waits")
        }
        #expect(streaming("| a | b |\n|---|---|\n") == [.table(header)])
        // Straight under prose, the header waits and the prose stays as it will be.
        let prose = MarkdownBlock.paragraph([plain("Per worker:")])
        #expect(streaming("Per worker:\n| a | b |") == [prose])
        #expect(streaming("Per worker:\n| a | b |\n|---") == [prose])
        #expect(streaming("Per worker:\n| a | b |\n|---|---|\n") == [prose, .table(header)])
        #expect(MarkdownDocument(parsing: "Per worker:\n| a | b |").blocks == [.paragraph([plain("Per worker:\n| a | b |")])])
        // Past its first two lines, or ended by a blank line, it is prose.
        for shown in ["| a | b |\n| c | d |", "| a | b |\n\n", "|x| is abs\nof x", "| a | b |\n|-- c", "Per worker:\n| a | b |\nmore"] {
            #expect(streaming(shown) == MarkdownDocument(parsing: shown).blocks, "\(shown.debugDescription) is shown")
            #expect(!streaming(shown).isEmpty)
        }
        // A real table never shows its header or delimiter as prose, and the prose above it never changes.
        for answer in ["Text\n\n| Workers | Rows each |\n|---|--:|\n| 2 | 512 |\n", "Per worker:\n| Workers | Rows each |\n|---|--:|\n| 2 | 512 |\n"] {
            let finished = MarkdownDocument(parsing: answer).blocks
            #expect(finished.count == 2)
            for prefix in everyPrefix(answer) where prefix.contains("\n") {
                let blocks = streaming(prefix)
                #expect(blocks.first == finished.first, "\(prefix.debugDescription): the prose stays as it will be")
                #expect(blocks.count <= 2, "\(prefix.debugDescription)")
                if blocks.count == 2 {
                    guard case .table = blocks[1] else {
                        Issue.record("\(prefix.debugDescription): the header shown as \(blocks[1])")
                        continue
                    }
                }
                for problem in rawMarkers(in: blocks) {
                    Issue.record("\(prefix.debugDescription): \(problem)")
                }
            }
        }
    }

    @Test("a finished answer never renders a written line as nothing: a bare fence opener at its end shows as written")
    func finishedAtAFenceOpener() {
        let intro = MarkdownBlock.paragraph([plain("Intro")])
        let cases: [(String, [MarkdownBlock])] = [
            ("```python", [.paragraph([plain("```python")])]),
            ("```python\n", [.paragraph([plain("```python")])]),
            ("Intro\n\n```python", [intro, .paragraph([plain("```python")])]),
            ("Intro\n```python\n", [intro, .paragraph([plain("```python")])]),
            ("Intro\n\n~~~", [intro, .paragraph([plain("~~~")])]),
            ("Intro\n\n  ```sh", [intro, .paragraph([plain("```sh")])]),
            // An empty block, opened and closed, or a heading's marker alone: what was written.
            ("```python\n```", [.paragraph([plain("```python\n```")])]),
            ("#", [.paragraph([plain("#")])]),
            ("## \n", [.paragraph([plain("##")])]),
            // Fences that close, or hold code, are what they were.
            ("Intro\n\n```py\nx\n```", [intro, .code(MarkdownCode(language: "py", text: "x"))]),
            ("```py\nx\n```\n", [.code(MarkdownCode(language: "py", text: "x"))]),
            ("````\nx\n```", [.code(MarkdownCode(language: nil, text: "x\n```"))]),
            ("```py\nx", [.code(MarkdownCode(language: "py", text: "x"))]),
            ("```py\n\n", [.code(MarkdownCode(language: "py", text: ""))]),
            ("<div>\n```", [.paragraph([plain("<div>\n```")])]),
        ]
        for (source, blocks) in cases {
            #expect(MarkdownDocument(parsing: source).blocks == blocks, "\(source.debugDescription)")
        }
        // Still streaming, a fence with no code yet draws nothing, as before.
        #expect(MarkdownDocument(parsing: "Intro\n\n```python", streaming: true).blocks == [intro])
        #expect(MarkdownDocument(parsing: "```python", streaming: true).blocks == [])
        #expect(MarkdownDocument(parsing: " \n ").blocks.isEmpty, "only whitespace is nothing to show")

        // A turn ended right after a recorded fence opened: its prefix, finished, ends at that line.
        var text = ""
        for piece in deltas(edgesStream) {
            text += piece
            if text.hasSuffix("```r") { break }
        }
        let blocks = MarkdownDocument(parsing: text).blocks
        #expect(blocks.last == .paragraph([plain("```r")]))
        #expect(Array(blocks.dropLast()) == Array(edgesBlocks.prefix(blocks.count - 1)))
    }

    @Test("the recorded edge-case answer: every block pinned, every line the model wrote shown, read back the same")
    func edgesAnswer() throws {
        let answer = deltas(edgesStream).joined()
        #expect(deltas(edgesStream).count == 102)
        #expect(MarkdownDocument(parsing: answer).blocks == edgesBlocks)
        let result = try #require(events(edgesStream).first { $0["type"] as? String == "result" }?["text"] as? String)
        #expect(result == answer)
        // Every line written outside a fence, a table and the heading reaches the screen as written.
        let shown = Set(shownLines(edgesBlocks))
        var fenced = false
        for line in answer.components(separatedBy: "\n") {
            if line.hasPrefix("```") { fenced.toggle(); continue }
            if fenced || line.isEmpty || line.hasPrefix("| ") || line.hasPrefix("|---") || line.hasPrefix("#") || line.contains("`") { continue }
            #expect(shown.contains(line), "\(line.debugDescription) is shown as written")
        }
        let document = try #require(try JSONSerialization.jsonObject(with: Data(recorded(edgesChat).utf8)) as? [String: Any])
        let turns = try #require(document["turns"] as? [[String: Any]])
        let answers = turns.compactMap { ($0["answer"] as? [String: Any])?["text"] as? String }
        #expect(answers == [answer], "read back by chats show, it is the live answer")
        #expect(MarkdownDocument(parsing: answers[0]).blocks == edgesBlocks)
    }

    @Test("the edge-case answer streamed in its 102 recorded pieces: settled blocks never change, no marker shows raw, and every line arrived is shown but a table's waiting first rows")
    func edgesStreamedInPieces() {
        let pieces = deltas(edgesStream)
        var text = ""
        for (index, piece) in pieces.enumerated() {
            text += piece
            let blocks = MarkdownDocument(parsing: text, streaming: true).blocks
            #expect(blocks.count <= edgesBlocks.count, "delta \(index)")
            #expect(Array(blocks.dropLast()) == Array(edgesBlocks.prefix(max(0, blocks.count - 1))), "delta \(index): a settled block changed")
            // Pipe-led prose past its first two lines is a paragraph opening with a pipe, as written.
            for problem in rawMarkers(in: blocks) where !problem.hasPrefix("a block's marker read as words: |x| is") {
                Issue.record("delta \(index): \(problem)")
            }
            // The lines outside fences and the table, each as far as it has arrived.
            let shown = shownLines(blocks)
            let arrived = text.components(separatedBy: "\n")
            var fenced = false
            for (place, line) in arrived.enumerated() {
                if line.hasPrefix("```") { fenced.toggle(); continue }
                let words = line.trimmingCharacters(in: .whitespaces)
                if fenced || words.isEmpty || line.hasPrefix("#") || line.contains("`") || line.hasPrefix("| ") || line.hasPrefix("|---") { continue }
                // The pipe-led paragraph's first line waits only until its second begins.
                if line.hasPrefix("|x|"), place == arrived.count - 1 || (place == arrived.count - 2 && arrived[place + 1].isEmpty) { continue }
                #expect(shown.contains { $0.hasPrefix(words) || $0 == words }, "delta \(index): \(words.debugDescription) is shown")
            }
            // Each table row is shown as soon as it holds a cell's first character.
            if case .table(let table)? = blocks.last {
                let rows = arrived.filter { $0.hasPrefix("| ") && $0.count > 2 && $0 != "| Workers | Rows each | Total |" }
                #expect(table.rows.count == rows.count, "delta \(index): \(rows.count) rows arrived, \(table.rows.count) shown")
            }
        }
        #expect(MarkdownDocument(parsing: text, streaming: true).blocks == edgesBlocks, "the last delta in, every block is shown")
    }
}
