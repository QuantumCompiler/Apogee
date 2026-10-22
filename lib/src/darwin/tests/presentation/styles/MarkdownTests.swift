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
}
