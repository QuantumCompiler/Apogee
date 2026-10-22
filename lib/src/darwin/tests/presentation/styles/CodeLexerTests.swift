import AppKit
import Foundation
@testable import PresentationStyles
import SwiftUI
import Testing

/// A recorded fixture, by its path under `tests/`.
private func recorded(_ path: String) -> String {
    let tests = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
    return (try? String(contentsOf: tests.appendingPathComponent(path), encoding: .utf8)) ?? ""
}

/// The `answer_delta` texts of a recorded stream, in order.
private func deltas(_ path: String) -> [String] {
    recorded(path).split(separator: "\n").compactMap { line in
        guard let event = try? JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any],
            event["type"] as? String == "answer_delta"
        else { return nil }
        return event["text"] as? String
    }
}

/// The scripted mock's answer with fences in Python, Swift, TypeScript, Rust,
/// shell and JSON, a diff, an unnamed fence and an Elixir one, recorded from
/// the branch's binary driven as the app drives a chat (tests/data/protocol/
/// Fixtures/code-answer.jsonl), and that chat's `chats show`
/// (tests/business/reads/Fixtures/chats-show-code.json).
private let codeStream = "data/protocol/Fixtures/code-answer.jsonl"
private let codeChat = "business/reads/Fixtures/chats-show-code.json"

/// The code blocks of a document's top level, in order.
private func codeBlocks(_ blocks: [MarkdownBlock]) -> [MarkdownCode] {
    blocks.compactMap { if case .code(let code) = $0 { code } else { nil } }
}

/// The recorded answer's code blocks.
private let recordedCode = codeBlocks(MarkdownDocument(parsing: deltas(codeStream).joined()).blocks)

/// A span as a test reads it: its kind and its text.
private struct Token: Equatable, CustomStringConvertible {
    let kind: CodeKind
    let text: String
    var description: String { "\(kind)(\(text.debugDescription))" }
}

private func k(_ text: String) -> Token { Token(kind: .keyword, text: text) }
private func s(_ text: String) -> Token { Token(kind: .string, text: text) }
private func c(_ text: String) -> Token { Token(kind: .comment, text: text) }
private func n(_ text: String) -> Token { Token(kind: .number, text: text) }
private func f(_ text: String) -> Token { Token(kind: .function, text: text) }

/// `text`'s spans in `language`, as tokens.
private func tokens(_ text: String, _ language: String?) -> [Token] {
    let bytes = Array(text.utf8)
    return CodeLexer.spans(text, language: language).map { Token(kind: $0.kind, text: String(decoding: bytes[$0.range], as: UTF8.self)) }
}

/// The five code kinds.
private let codeKinds: Set<CodeKind> = [.keyword, .string, .comment, .number, .function]

/// The kinds each language has: every one of the five, but where the
/// language writes no comment or names no call.
private func kinds(of language: String) -> Set<CodeKind> {
    switch language {
    case "json": [.keyword, .string, .number]
    case "yaml", "toml": [.keyword, .string, .comment, .number]
    case "diff": [.header, .context, .removed, .added]
    default: codeKinds
    }
}

/// The recorded fences' spans, every one pinned. The Python is coloured as
/// the design colours its own (`main.html`'s `.k`, `.s`, `.c`, `.n`, `.f`).
private let recordedTokens: [(language: String, tokens: [Token])] = [
    ("python", [
        k("async"), k("def"), f("fetch"), n("3"), k("None"), s(#""""Fetch url, retrying a timeout.""""#), k("for"), k("in"),
        f("range"), n("1"), k("try"), k("async"), k("with"), f("timeout"), n("10.5"), k("return"), k("await"), f("get"),
        k("except"), f("warning"), s(#""attempt %d timed out: %s""#), c("# try again"), k("return"), k("None"),
    ]),
    ("swift", [
        c("/// Asks again after a pause, up to `attempts` times."), k("func"), f("retry"), n("3"), k("async"), k("throws"),
        k("async"), k("throws"), k("for"), k("in"), n("1"), k("do"), k("return"), k("try"), k("await"), f("body"), k("catch"),
        f("print"), s(#""attempt \(attempt) failed: \(error)""#), k("try"), k("await"), f("sleep"), f("milliseconds"), n("250"),
        k("return"), k("try"), k("await"), f("body"),
    ]),
    ("ts", [
        c("// Back off between attempts."), k("export"), k("async"), k("function"), f("fetchWithRetry"), n("3"), k("for"),
        k("let"), n("0"), k("const"), k("await"), f("fetch"), f("timeout"), n("10_000"), k("if"), k("return"), f("warn"),
        s("`attempt ${attempt} failed: ${response.status}`"), k("throw"), k("new"), f("Error"), s("'gave up after '"),
        s("' retries'"),
    ]),
    ("rust", [
        k("use"), c("/* Doubles the pause after each failed attempt. */"), k("fn"), f("backoff"), k("let"), n("0x10u64"),
        c("// 16 ms"), f("from_millis"), n("2u64"), f("pow"), k("fn"), f("main"), k("for"), k("in"), n("0"), n("3"),
        f("println"), s(#""attempt {attempt}: wait {:?}""#), f("backoff"),
    ]),
    ("bash", [
        c("# Three tries, two seconds apart."), f("retry"), k("for"), k("in"), n("1"), n("2"), n("3"), k("do"), s(#""$@""#),
        k("return"), n("0"), s(#""attempt $i failed""#), n("2"), n("2"), k("done"), k("return"), n("1"), n("10"),
        s("'https://example.com/slow'"),
    ]),
    ("json", [
        s(#""retries""#), n("3"), s(#""timeout""#), n("10.5"), s(#""backoff_ms""#), n("250"), n("500"), n("1000"),
        s(#""hosts""#), s(#""example.com""#), s(#""proxy""#), k("null"), s(#""verbose""#), k("false"),
    ]),
]

/// A sample for each language the recording does not carry, every span
/// kind the language has in it, its spans pinned.
private let samples: [(language: String, code: String, tokens: [Token])] = [
    ("javascript", "// Retry once.\nfunction retry(task) {\n  return task().catch(() => task(`again ${1}`, 'x', 0x1F));\n}", [
        c("// Retry once."), k("function"), f("retry"), k("return"), f("task"), f("catch"), f("task"), s("`again ${1}`"),
        s("'x'"), n("0x1F"),
    ]),
    ("c", "#include <stdio.h>\n/* Says how many. */\nint main(void) {\n    printf(\"%d tries\\n\", 3); // done\n    return 0;\n}", [
        k("#include"), c("/* Says how many. */"), k("int"), f("main"), k("void"), f("printf"), s(#""%d tries\n""#), n("3"),
        c("// done"), k("return"), n("0"),
    ]),
    ("cpp", "// A raw string.\nauto pattern = R\"(a \"quoted\" (thing))\";\nstd::vector<int> sizes{1, 2};\ntemplate <typename T> T twice(T x) { return x * 2.5f; }", [
        c("// A raw string."), k("auto"), s(#"R"(a "quoted" (thing))""#), k("int"), n("1"), n("2"), k("template"),
        k("typename"), f("twice"), k("return"), n("2.5f"),
    ]),
    ("objc", "#import <Foundation/Foundation.h>\n@interface Fetcher : NSObject\n- (void)fetch:(NSString *)url; // later\n@end\nNSLog(@\"tries: %d\", 3);", [
        k("#import"), k("@interface"), k("void"), c("// later"), k("@end"), f("NSLog"), s(#"@"tries: %d""#), n("3"),
    ]),
    ("go", "// Backoff doubles the wait.\nfunc Backoff(n int) time.Duration {\n\treturn time.Duration(1<<n) * time.Millisecond * 250\n}\nvar raw = `C:\\path`", [
        c("// Backoff doubles the wait."), k("func"), f("Backoff"), k("return"), f("Duration"), n("1"), n("250"), k("var"),
        s(#"`C:\path`"#),
    ]),
    ("java", "/** Retries a task. */\npublic static int retry(Runnable task) {\n    char mark = '\\n';\n    System.out.println(\"tries: \" + 3L);\n    return 0;\n}", [
        c("/** Retries a task. */"), k("public"), k("static"), k("int"), f("retry"), k("char"), s(#"'\n'"#), f("println"),
        s(#""tries: ""#), n("3L"), k("return"), n("0"),
    ]),
    ("kotlin", "// Waits, then asks again.\nsuspend fun Fetcher.retry(times: Int = 3): String {\n    val delayMs = 250L\n    return \"tried $times times, ${delayMs * times} ms\"\n}", [
        c("// Waits, then asks again."), k("suspend"), k("fun"), f("retry"), n("3"), k("val"), n("250L"), k("return"),
        s(#""tried $times times, ${delayMs * times} ms""#),
    ]),
    ("csharp", "// Retries the call.\npublic async Task<int> RetryAsync(int times = 3) {\n    var path = @\"C:\\logs\\\"\"retry\"\".txt\";\n    await Task.Delay(250);\n    return times;\n}", [
        c("// Retries the call."), k("public"), k("async"), k("int"), f("RetryAsync"), k("int"), n("3"), k("var"),
        s(#"@"C:\logs\""retry"".txt""#), k("await"), f("Delay"), n("250"), k("return"),
    ]),
    ("ruby", "# Retries a block.\ndef self.retry!(times = 3)\n  puts \"attempt #{times}\"\n  yield rescue nil\nend", [
        c("# Retries a block."), k("def"), k("self"), f("retry!"), n("3"), s(#""attempt #{times}""#), k("yield"), k("rescue"),
        k("nil"), k("end"),
    ]),
    ("sql", "-- Slow hosts this week.\nSELECT host, COUNT(*) AS tries\nFROM fetches\nWHERE status = 'timed out' AND seconds > 10.5\nGROUP BY host;", [
        c("-- Slow hosts this week."), k("SELECT"), f("COUNT"), k("AS"), k("FROM"), k("WHERE"), s("'timed out'"), k("AND"),
        n("10.5"), k("GROUP"), k("BY"),
    ]),
    ("yaml", "# Retry settings.\nretries: 3\ntimeout: 10.5\nproxy: null\nhosts: [\"example.com\", 'it''s']\nnote: don't guess # trailing", [
        c("# Retry settings."), n("3"), n("10.5"), k("null"), s(#""example.com""#), s("'it''s'"), c("# trailing"),
    ]),
    ("toml", "# Retry settings.\n[fetch]\nretries = 3\nbackoff-ms = [250, 500]\nproxy = 'none'\nverbose = false\nnote = \"\"\"\ntwo lines\"\"\"", [
        c("# Retry settings."), n("3"), n("250"), n("500"), s("'none'"), k("false"), s("\"\"\"\ntwo lines\"\"\""),
    ]),
]

/// The spans' parts before `end`.
private func clipped(_ spans: [CodeSpan], to end: Int) -> [CodeSpan] {
    spans.compactMap { span in
        let upper = min(span.range.upperBound, end)
        return span.range.lowerBound < upper ? CodeSpan(span.kind, span.range.lowerBound..<upper) : nil
    }
}

/// Where the lines a piece of text has finished end: just past its last newline.
private func settled(_ bytes: some Collection<UInt8>) -> Int {
    var end = 0
    for (offset, byte) in bytes.enumerated() where byte == UInt8(ascii: "\n") { end = offset + 1 }
    return end
}

/// Every cut of `text` at a character's boundary, as a stream's edge would
/// cut it: the lines the cut has finished colour exactly as the whole text
/// colours them, and a string or comment open at the cut colours to it.
/// Returns how many cuts fell inside an open string or comment.
private func checkEveryCut(_ text: String, _ language: String, sourceLocation: SourceLocation = #_sourceLocation) -> Int {
    let bytes = Array(text.utf8)
    let whole = CodeLexer.spans(text, language: language)
    var open = 0
    for cut in 0...bytes.count where cut == bytes.count || bytes[cut] & 0xC0 != 0x80 {
        let prefix = String(decoding: bytes[..<cut], as: UTF8.self)
        let spans = CodeLexer.spans(prefix, language: language)
        let end = settled(bytes[..<cut])
        #expect(clipped(spans, to: end) == clipped(whole, to: end), "\(language), cut at \(cut): a settled line changed", sourceLocation: sourceLocation)
        for span in whole where (span.kind == .string || span.kind == .comment) && span.range.lowerBound < cut && cut < span.range.upperBound {
            guard let edge = spans.first(where: { $0.range.lowerBound == span.range.lowerBound && $0.kind == span.kind }) else { continue }
            open += 1
            #expect(edge.range.upperBound == cut, "\(language), cut at \(cut): an open \(span.kind) stops short of the edge", sourceLocation: sourceLocation)
        }
    }
    return open
}

/// The distinct colours an image of `view` draws in its opaque pixels, as 0xRRGGBB.
@MainActor
private func drawnColours(_ view: some View) -> Set<UInt32> {
    let renderer = ImageRenderer(content: view)
    renderer.scale = 2
    guard let image = renderer.cgImage, let space = CGColorSpace(name: CGColorSpace.sRGB) else { return [] }
    var data = [UInt8](repeating: 0, count: image.width * image.height * 4)
    guard let context = CGContext(data: &data, width: image.width, height: image.height, bitsPerComponent: 8, bytesPerRow: image.width * 4,
                                  space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
    else { return [] }
    context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
    var colours: Set<UInt32> = []
    for pixel in stride(from: 0, to: data.count, by: 4) where data[pixel + 3] == 255 {
        colours.insert(UInt32(data[pixel]) << 16 | UInt32(data[pixel + 1]) << 8 | UInt32(data[pixel + 2]))
    }
    return colours
}

@Suite("Code lexer")
struct CodeLexerTests {
    @Test("the design's own code -- M8's recorded Python, main.html's answer -- coloured as the design marks it, nothing else")
    func designsAnswer() throws {
        let python = try #require(codeBlocks(MarkdownDocument(parsing: deltas("data/protocol/Fixtures/markdown-answer.jsonl").joined()).blocks).first)
        #expect(python.language == "python")
        #expect(tokens(python.text, python.language) == [
            k("async"), k("def"), f("_fetch"), k("None"), k("try"), k("async"), k("with"), f("timeout"), n("10"), k("return"),
            k("await"), f("get"), k("except"), f("warning"), s(#""timed out: %s""#), c("# the batch keeps going"), k("return"),
            k("None"),
        ])
    }

    @Test("the recorded answer's fences: Python, Swift, TypeScript, Rust, shell and JSON each pinned, every kind its language has; the diff by line; the unnamed and Elixir fences none")
    func recordedAnswer() throws {
        #expect(deltas(codeStream).count == 373)
        #expect(recordedCode.map(\.language) == ["python", "swift", "ts", "rust", "bash", "json", "diff", nil, "elixir"])
        for (index, expected) in recordedTokens.enumerated() {
            let code = recordedCode[index]
            #expect(code.language == expected.language)
            let found = tokens(code.text, code.language)
            #expect(found == expected.tokens, "\(expected.language)")
            let language = try #require(CodeLexer.language(named: code.language))
            #expect(Set(found.map(\.kind)) == kinds(of: language.name), "\(expected.language) colours every kind it has")
        }
        let diff = recordedCode[6]
        #expect(CodeLexer.spans(diff.text, language: diff.language).map(\.kind) == [
            .header, .header, .header, .context, .removed, .removed, .added, .added, .added, .added, .added,
        ])
        for code in recordedCode[7...] {
            #expect(CodeLexer.spans(code.text, language: code.language).isEmpty, "\(code.caption)")
        }
    }

    @Test("every language in the table colours a sample with every kind it has, its spans pinned")
    func everyLanguage() throws {
        for sample in samples {
            let found = tokens(sample.code, sample.language)
            #expect(found == sample.tokens, "\(sample.language)")
            #expect(Set(found.map(\.kind)) == kinds(of: sample.language), "\(sample.language) colours every kind it has")
        }
        let covered = Set(samples.map(\.language) + recordedTokens.compactMap { CodeLexer.language(named: $0.language)?.name } + ["diff"])
        #expect(covered == Set(CodeLanguage.all.map(\.name)), "a sample for every language in the table")
    }

    @Test("the languages and their aliases, as the open call took them: any case, the info string's first word")
    func aliases() {
        let table: [String: [String]] = [
            "python": ["python", "py", "python3", "py3"], "swift": ["swift"],
            "javascript": ["javascript", "js", "jsx", "mjs", "cjs"], "typescript": ["typescript", "ts", "tsx", "mts", "cts"],
            "c": ["c", "h"], "cpp": ["cpp", "c++", "cc", "cxx", "hpp", "hxx", "hh"],
            "objc": ["objc", "objective-c", "objectivec", "obj-c", "objc++", "objective-c++", "mm"],
            "rust": ["rust", "rs"], "go": ["go", "golang"], "java": ["java"], "kotlin": ["kotlin", "kt", "kts"],
            "csharp": ["csharp", "cs", "c#"], "ruby": ["ruby", "rb"], "shell": ["shell", "sh", "bash", "zsh"], "sql": ["sql"],
            "json": ["json"], "yaml": ["yaml", "yml"], "toml": ["toml"], "diff": ["diff", "patch"],
        ]
        #expect(Dictionary(uniqueKeysWithValues: CodeLanguage.all.map { ($0.name, $0.aliases) }) == table)
        let aliases = CodeLanguage.all.flatMap(\.aliases)
        #expect(Set(aliases).count == aliases.count, "no alias names two languages")
        for (name, aliases) in table {
            for alias in aliases {
                #expect(CodeLexer.language(named: alias)?.name == name, "\(alias)")
                #expect(CodeLexer.language(named: alias.uppercased())?.name == name, "\(alias.uppercased())")
            }
        }
        for alias in ["py", "js", "ts", "jsx", "tsx", "c++", "cpp", "objc", "rs", "kt", "cs", "rb", "yml", "sh", "bash", "zsh"] {
            #expect(CodeLexer.language(named: alias) != nil, "the open call's \(alias)")
        }
        #expect(CodeLexer.language(named: "Python")?.name == "python")
        #expect(CodeLexer.language(named: #"python title="runner.py""#)?.name == "python")
        #expect(CodeLexer.language(named: "python:src/ingest/runner.py")?.name == "python")
        #expect(CodeLexer.language(named: "C++")?.name == "cpp")
        #expect(CodeLexer.language(named: "C#")?.name == "csharp")
    }

    @Test("honest by default: an unnamed fence, or a language the lexer does not know, has no spans and draws exactly as before -- one Text, verbatim")
    func unnamedOrUnknown() {
        let code = "def f():\n    return 'x'  # 10"
        for info in [nil, "", " ", "elixir", "text", "plaintext", "txt", "console", "shell-session", "html", "css", "markdown", "lua", "pythonic"] as [String?] {
            #expect(CodeLexer.language(named: info) == nil, "\(info.debugDescription)")
            #expect(CodeLexer.spans(code, language: info).isEmpty, "\(info.debugDescription)")
            #expect(CodeLexer.runs(code, language: info) == [CodeRun(code, nil)], "\(info.debugDescription)")
            // Compared outside #expect: a failure describing a coloured
            // Text resolves its colours on the main thread, which waits.
            let verbatim = MarkdownText.codeText(MarkdownCode(language: info, text: code)) == Text(verbatim: code)
            #expect(verbatim, "\(info.debugDescription)")
        }
        for block in recordedCode[7...] {
            let verbatim = MarkdownText.codeText(block) == Text(verbatim: block.text)
            #expect(verbatim, "\(block.caption)")
        }
        // A known language with nothing in it to colour draws as plain code too.
        let nothingToColour = MarkdownText.codeText(MarkdownCode(language: "python", text: "x = y")) == Text(verbatim: "x = y")
        #expect(nothingToColour)
        let coloured = MarkdownText.codeText(recordedCode[0]) != Text(verbatim: recordedCode[0].text)
        #expect(coloured, "a coloured block is not verbatim")
        #expect(CodeLexer.runs("", language: "python").isEmpty)
    }

    @Test("numbers: ints, floats, hex, octal and binary, exponents, underscores and suffixes; never inside a word")
    func numbers() {
        #expect(tokens("x = 10 + 0.5 + 0x1F + 0b1010 + 0o17 + 1_000 + 1e-9 + 2.5E+3 + 3j + .5", "python") == [
            n("10"), n("0.5"), n("0x1F"), n("0b1010"), n("0o17"), n("1_000"), n("1e-9"), n("2.5E+3"), n("3j"), n(".5"),
        ])
        #expect(tokens("v2 = x1 + _3", "python").isEmpty, "digits in a word are the word's")
        #expect(tokens("for i in 0..<10 where i != 1.5 {}", "swift") == [k("for"), k("in"), n("0"), n("10"), k("where"), n("1.5")])
        #expect(tokens("let x = 2u64.pow(3) + 1_000_i32;", "rust") == [k("let"), n("2u64"), f("pow"), n("3"), n("1_000_i32")])
        #expect(tokens("version: 1.2.3\nhost: 127.0.0.1", "yaml") == [n("1.2.3"), n("127.0.0.1")])
        #expect(tokens("head -5 file2 $1", "bash") == [n("5")], "a shell's $1 is a parameter")
        #expect(tokens("const big = 10n;", "js") == [k("const"), n("10n")])
    }

    @Test("strings: escapes, prefixes, triple quotes, raw strings, template literals and interpolation, characters, here-documents, doubled quotes")
    func strings() {
        #expect(tokens(#"s = "a \"b\" c""#, "python") == [s(#""a \"b\" c""#)])
        #expect(tokens(#"rb'\x00' + f"{x}" + R'raw'"#, "python") == [s(#"rb'\x00'"#), s(#"f"{x}""#), s("R'raw'")])
        #expect(tokens("'''one\ntwo''' + 1", "python") == [s("'''one\ntwo'''"), n("1")])
        #expect(tokens("x = 'open\ny = 1", "python") == [s("'open"), n("1")], "a one-line string stops at its line's end")
        #expect(tokens(##"let r = #"raw "q" \n"#"##, "swift") == [k("let"), s(##"#"raw "q" \n"#"##)])
        #expect(tokens(#"print("\(f("x")) y")"#, "swift") == [f("print"), s(#""\(f("x")) y""#)], "an interpolation's own quotes")
        #expect(tokens("let s = \"\"\"\nline \"one\"\n\"\"\"", "swift") == [k("let"), s("\"\"\"\nline \"one\"\n\"\"\"")])
        #expect(tokens(#"html`<b>${user.name ?? "}"}</b>`"#, "ts") == [s(#"`<b>${user.name ?? "}"}</b>`"#)], "a tagged template")
        #expect(tokens(##"let c = b'a'; let l: &'a str = r#"say "hi""#;"##, "rust") == [
            k("let"), s("b'a'"), k("let"), s(##"r#"say "hi""#"##),
        ], "a lifetime is not a character")
        #expect(tokens(#"'\n' '\u{1F600}' '\'' 'é'"#, "rust") == [s(#"'\n'"#), s(#"'\u{1F600}'"#), s(#"'\''"#), s("'é'")])
        #expect(tokens(#"R"x(a)" b)x""#, "cpp") == [s(#"R"x(a)" b)x""#)])
        #expect(tokens(#"@"a ""b"" c""#, "cs") == [s(#"@"a ""b"" c""#)])
        #expect(tokens(#"`a\` + "b""#, "go") == [s(#"`a\`"#), s(#""b""#)], "a raw string escapes nothing")
        #expect(tokens(#"echo $'it\'s' don't "$(cat "$f")""#, "bash") == [s(#"$'it\'s'"#), s(#""$(cat "$f")""#)], "an apostrophe in a word opens nothing")
        #expect(tokens("cat <<'EOF' > out\nif $x\n  EOF\necho done", "bash") == [s("<<'EOF'"), s("if $x\n  EOF"), k("done")])
        #expect(tokens("sql = <<~SQL\n  SELECT 1\nSQL\nlist << item", "ruby") == [s("<<~SQL"), s("  SELECT 1\nSQL")])
        #expect(tokens("'it''s' || 'x'", "sql") == [s("'it''s'"), s("'x'")])
        #expect(tokens("a: don't\nb: 'it''s'", "yaml") == [s("'it''s'")])
        #expect(tokens(#"{"a\"b": "c"}"#, "json") == [s(#""a\"b""#), s(#""c""#)])
    }

    @Test("comments: line and block, nested where the language nests them, a shell's and YAML's # only where a word starts")
    func comments() {
        #expect(tokens("/* a /* b */ c */ x()", "swift") == [c("/* a /* b */ c */"), f("x")])
        #expect(tokens("/* a /* b */ c */", "c") == [c("/* a /* b */")], "C's do not nest")
        #expect(tokens("echo a#b $# ${#x} # note", "bash") == [c("# note")])
        #expect(tokens("url: http://x/#y # note", "yaml") == [c("# note")])
        #expect(tokens("x = '#' # real", "python") == [s("'#'"), c("# real")])
        #expect(tokens("SELECT 1 -- why", "sql") == [k("SELECT"), n("1"), c("-- why")])
        #expect(tokens("/// doc\n//! inner", "rust") == [c("/// doc"), c("//! inner")])
    }

    @Test("names: the word a definer introduces, and a call's name just before its (; a keyword stays one, a member never is, a Swift label is a word")
    func names() {
        #expect(tokens("def run_batch(): pass", "python") == [k("def"), f("run_batch"), k("pass")])
        #expect(tokens("func create_task<T>() {}", "swift") == [k("func"), f("create_task")])
        #expect(tokens("pub fn run() {}", "rust") == [k("pub"), k("fn"), f("run")])
        #expect(tokens("async function go() {}", "js") == [k("async"), k("function"), f("go")])
        #expect(tokens("fun Int.double() = this * 2", "kotlin") == [k("fun"), f("double"), k("this"), n("2")])
        #expect(tokens("def valid?\nend", "ruby") == [k("def"), f("valid?"), k("end")])
        #expect(tokens("function deploy {\n}", "bash") == [k("function"), f("deploy")])
        #expect(tokens("CREATE FUNCTION add_one(x int)", "sql") == [k("CREATE"), k("FUNCTION"), f("add_one"), k("int")])
        #expect(tokens("def\nx = 1", "python") == [k("def"), n("1")], "a definer looks for its name on its own line only")
        #expect(tokens("print(len(x))", "python") == [f("print"), f("len")])
        #expect(tokens("if (x) while(y) sizeof(z)", "c") == [k("if"), k("while"), k("sizeof")])
        #expect(tokens("p.catch(e).finally(f); x.default", "js") == [f("catch"), f("finally")])
        #expect(tokens("fetch().await?", "rust") == [f("fetch"), k("await")])
        #expect(tokens("Foo.self; Foo.init(x)", "swift") == [k("self"), k("init")])
        #expect(tokens(#"println!("{}", vec![1]);"#, "rust") == [f("println"), s(#""{}""#), f("vec"), n("1")])
        #expect(tokens("try await Task.sleep(for: .seconds(1)); dict[key, default: 0]; switch x { default: break }", "swift") == [
            k("try"), k("await"), f("sleep"), f("seconds"), n("1"), n("0"), k("switch"), k("default"), k("break"),
        ])
    }

    @Test("a diff or patch colours by line: added, removed, hunk and file headers (never added or removed), context; an empty line nothing")
    func diffByLine() {
        let patch = "diff --git a/x b/x\nindex 83db48f..bf269f4 100644\n--- a/x\n+++ b/x\n@@ -1,2 +1,2 @@\n context\n-old\n+new\n\n\\ No newline at end of file"
        for language in ["diff", "patch", "Diff"] {
            #expect(CodeLexer.spans(patch, language: language).map(\.kind) == [
                .header, .header, .header, .header, .header, .context, .removed, .added, .header,
            ], "\(language)")
        }
        #expect(tokens("+++ b/x\n---\n+\n-", "diff").map(\.kind) == [.header, .header, .added, .removed])
    }

    @Test("an unterminated string, template, raw string, here-document or block comment at the stream's edge colours to the end")
    func openAtTheEdge() {
        let open: [(String, String, CodeKind)] = [
            ("python", "x = \"open", .string), ("python", "\"\"\"doc\nstill", .string), ("swift", "/* a /* b */ still", .comment),
            ("ts", "`a ${b} c", .string), ("ts", "`a ${b", .string), ("rust", "r#\"raw \"q\" still", .string),
            ("bash", "cat <<EOF\nbody", .string), ("c", "/* open", .comment), ("sql", "'it''s", .string), ("go", "`raw", .string),
            ("ruby", "\"a #{b", .string), ("cpp", "R\"(raw", .string), ("swift", "\"a \\(f(\"x", .string),
        ]
        for (language, text, kind) in open {
            let last = CodeLexer.spans(text, language: language).last
            #expect(last?.kind == kind && last?.range.upperBound == text.utf8.count, "\(language): \(text.debugDescription)")
        }
    }

    @Test("every cut of every sample, as a stream's edge cuts it: no finished line changes colour, and an open string or comment reaches the edge")
    func everyCut() {
        var open = 0
        for sample in samples {
            open += checkEveryCut(sample.code, sample.language)
        }
        for code in recordedCode {
            open += checkEveryCut(code.text, code.language ?? "")
        }
        #expect(open > 500, "the edge fell inside an open string or comment \(open) times")
    }

    @Test("streamed in its 373 recorded pieces: every prefix's fences colour their settled lines as the finished answer does, an open string reaching the edge")
    func streamedInPieces() throws {
        let finished = recordedCode
        var text = ""
        var openAtEdge = 0
        for (index, piece) in deltas(codeStream).enumerated() {
            text += piece
            for (place, code) in codeBlocks(MarkdownDocument(parsing: text, streaming: true).blocks).enumerated() {
                let whole = finished[place]
                #expect(code.language == whole.language, "delta \(index)")
                #expect(whole.text.hasPrefix(code.text), "delta \(index)")
                let spans = CodeLexer.spans(code.text, language: code.language)
                let wholeSpans = CodeLexer.spans(whole.text, language: whole.language)
                let end = settled(code.text.utf8)
                #expect(clipped(spans, to: end) == clipped(wholeSpans, to: end), "delta \(index), \(code.caption): a settled line changed")
                let edge = code.text.utf8.count
                if let last = spans.last, last.kind == .string, last.range.upperBound == edge,
                    wholeSpans.contains(where: { $0.range.lowerBound == last.range.lowerBound && $0.range.upperBound > edge })
                {
                    openAtEdge += 1
                }
            }
        }
        #expect(openAtEdge >= 10, "the stream's edge fell inside an open string \(openAtEdge) times, each coloured to it")
        #expect(codeBlocks(MarkdownDocument(parsing: text).blocks) == finished)
    }

    @Test("read back by chats show, the answer is the live one's text: its fences colour span for span the same")
    func readBack() throws {
        let document = try #require(try JSONSerialization.jsonObject(with: Data(recorded(codeChat).utf8)) as? [String: Any])
        let turns = try #require(document["turns"] as? [[String: Any]])
        let answers = turns.compactMap { ($0["answer"] as? [String: Any])?["text"] as? String }
        #expect(answers == [deltas(codeStream).joined()])
        let resumed = codeBlocks(MarkdownDocument(parsing: answers[0]).blocks)
        #expect(resumed == recordedCode)
        for (live, back) in zip(recordedCode, resumed) {
            #expect(CodeLexer.spans(back.text, language: back.language) == CodeLexer.spans(live.text, language: live.language))
        }
    }

    @Test("the colour is drawn over the code, never into it: runs join to the code exactly, and copy puts the plain code alone on the pasteboard")
    @MainActor
    func copyTakesThePlainCode() {
        for code in recordedCode + samples.map({ MarkdownCode(language: $0.language, text: $0.code) }) {
            #expect(CodeLexer.runs(code.text, language: code.language).map(\.text).joined() == code.text, "\(code.caption)")
        }
        let pasteboard = NSPasteboard(name: NSPasteboard.Name("apogee-code-lexer-tests-\(UUID().uuidString)"))
        defer { pasteboard.releaseGlobally() }
        for code in recordedCode {
            code.copy(to: pasteboard)
            for rich in [NSPasteboard.PasteboardType.rtf, .rtfd, .html] {
                #expect(pasteboard.data(forType: rich) == nil, "\(code.caption): text alone, no \(rich.rawValue)")
            }
            #expect(pasteboard.string(forType: .string) == code.text, "\(code.caption)")
        }
    }

    @Test("each kind's token: the design's five code colours; a diff's added line the accent, removed the error colour, headers and context tertiary")
    func colours() {
        #expect(MarkdownText.token(for: .keyword) == Palette.codeKeywordToken)
        #expect(MarkdownText.token(for: .string) == Palette.codeStringToken)
        #expect(MarkdownText.token(for: .comment) == Palette.codeCommentToken)
        #expect(MarkdownText.token(for: .number) == Palette.codeNumberToken)
        #expect(MarkdownText.token(for: .function) == Palette.codeFunctionToken)
        #expect(MarkdownText.token(for: .added) == Palette.accentToken)
        #expect(MarkdownText.token(for: .removed) == Palette.errorToken)
        #expect(MarkdownText.token(for: .header) == Palette.textTertiaryToken)
        #expect(MarkdownText.token(for: .context) == Palette.textTertiaryToken)
    }

    @Test("drawn: a coloured block's text draws each span in its token's colour over the code's own; a diff its line colours; an unnamed block none of them")
    @MainActor
    func drawnInItsColours() {
        // As the code block styles it, larger so every glyph has solid pixels.
        func drawn(_ code: MarkdownCode) -> Set<UInt32> {
            drawnColours(MarkdownText.codeText(code).font(.system(size: 24, design: .monospaced)).foregroundStyle(Palette.textPrimary))
        }
        // Any theme's value: another suite may switch the theme meanwhile.
        func shows(_ colours: Set<UInt32>, _ kind: CodeKind) -> Bool {
            let token = MarkdownText.token(for: kind)
            return colours.contains(token.light) || colours.contains(token.dark) || colours.contains(token.contrast)
        }
        let python = drawn(recordedCode[0])
        for kind in codeKinds {
            #expect(shows(python, kind), "\(kind) drawn in its colour")
        }
        let diff = drawn(recordedCode[6])
        #expect(shows(diff, .added) && shows(diff, .removed) && shows(diff, .header))
        for block in recordedCode[7...] {
            let plain = drawn(block)
            #expect(!plain.isEmpty, "\(block.caption) was drawn")
            for kind in [CodeKind.keyword, .string, .number, .function] {
                #expect(!shows(plain, kind), "\(block.caption) draws no \(kind) colour")
            }
        }
    }
}
