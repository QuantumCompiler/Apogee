import Foundation

/// What a run of code is, for the colour it draws in: one of the design's five
/// code colours (`code.keyword` to `code.function`), or, in a diff coloured by
/// line, what its line is.
public enum CodeKind: String, Sendable, CaseIterable {
    /// A language's reserved word, or one of its literals (`None`, `true`, `nil`).
    case keyword
    /// A quoted string: escaped, triple-quoted, raw, a template literal, a
    /// here-document, a character.
    case string
    /// A line or block comment.
    case comment
    /// A numeral: an integer or a float, hex, octal or binary, with an
    /// exponent, underscores or a type's suffix (`10`, `0.5`, `0x1F`, `10_000`, `2u64`).
    case number
    /// The name a definition introduces (after `def`, `func`, `fn`,
    /// `function`, `fun`) or a call names (just before its `(`).
    case function
    /// A diff's added line (`+`).
    case added
    /// A diff's removed line (`-`).
    case removed
    /// A diff's header: a hunk's `@@`, a file's `---` and `+++`, and the
    /// lines a tool writes around them (`diff --git`, `index`).
    case header
    /// A diff's unchanged line around a change (a leading space).
    case context
}

/// One coloured run of a code block's text: its kind, and where it lies as
/// UTF-8 offsets into the text. A span starts and ends on a character's
/// boundary, and a block's spans are in order and never overlap.
public struct CodeSpan: Equatable, Sendable {
    public let kind: CodeKind
    public let range: Range<Int>

    public init(_ kind: CodeKind, _ range: Range<Int>) {
        self.kind = kind
        self.range = range
    }
}

/// A piece of a code block's text, coloured or plain. A block's runs, joined,
/// are its text exactly: the colour is drawn over the code and never changes it.
public struct CodeRun: Equatable, Sendable {
    public let text: String
    /// `nil` for text drawn in the code's own colour.
    public let kind: CodeKind?

    public init(_ text: String, _ kind: CodeKind?) {
        self.text = text
        self.kind = kind
    }
}

/// A fenced code block's text coloured by the language its fence names: the
/// design's five code colours (keywords, strings, comments, numbers and the
/// names calls and definitions introduce), and a diff by line.
///
/// No package and no parser (the darwin layering check forbids a
/// dependency): one left-to-right pass over the text's UTF-8 bytes, under a
/// table of each language's rules (`CodeLanguage`). It finds tokens, never a
/// tree. Linear in the text, and Foundation only, so it is tested without a
/// view.
///
/// Honest by default: a fence colours only for a language it names that the
/// table knows. An unnamed fence, or a language not in the table, has no
/// spans and draws in one colour; nothing is guessed from the code.
///
/// Streaming: the pass decides each token from what comes before it and from
/// what follows on its own line only, so a line the stream has finished
/// never changes colour as more arrives. A string or block comment still open
/// at the stream's edge colours to the end of the text.
public enum CodeLexer {
    /// The language a fence's info string names: its first word, in any case
    /// (`Python`, `py`, `python title="x.py"`, `python:src/x.py`), looked up
    /// among every language's aliases; `nil` for no name or one the table does
    /// not know.
    public static func language(named info: String?) -> CodeLanguage? {
        guard let info,
            let word = info.split(whereSeparator: { $0 == " " || $0 == "\t" || $0 == ":" }).first
        else { return nil }
        return byAlias[word.lowercased()]
    }

    /// The spans of `text`, coloured as the language `info` names; none for
    /// an unnamed fence or an unknown language.
    public static func spans(_ text: String, language info: String?) -> [CodeSpan] {
        language(named: info)?.spans(text) ?? []
    }

    /// `text` cut into runs at its spans' edges, plain between them. Joined,
    /// the runs are `text`.
    public static func runs(_ text: String, language info: String?) -> [CodeRun] {
        runs(text, spans(text, language: info))
    }

    /// `text` cut at `spans`' edges.
    static func runs(_ text: String, _ spans: [CodeSpan]) -> [CodeRun] {
        guard !spans.isEmpty else { return text.isEmpty ? [] : [CodeRun(text, nil)] }
        let bytes = Array(text.utf8)
        var runs: [CodeRun] = []
        var at = 0
        func piece(_ range: Range<Int>, _ kind: CodeKind?) {
            guard !range.isEmpty else { return }
            runs.append(CodeRun(String(decoding: bytes[range], as: UTF8.self), kind))
        }
        for span in spans {
            piece(at..<span.range.lowerBound, nil)
            piece(span.range, span.kind)
            at = span.range.upperBound
        }
        piece(at..<bytes.count, nil)
        return runs
    }

    /// Every alias of every language, to its language.
    static let byAlias: [String: CodeLanguage] = Dictionary(
        CodeLanguage.all.flatMap { language in language.aliases.map { ($0, language) } },
        uniquingKeysWith: { first, _ in first })
}

// MARK: - The rule table

/// One language the lexer knows: its names and its rules. `all` is the table.
public struct CodeLanguage: Sendable {
    /// Its name in the table (`python`).
    public let name: String
    /// The fence words that name it, lower-case, its name among them (`python`, `py`).
    public let aliases: [String]
    let grammar: Grammar

    /// How a language is read: as tokens under its rules, or line by line as a diff.
    enum Grammar: Sendable {
        case tokens(CompiledRules)
        case diff
    }

    /// A language read as tokens under `rules`; with none, a diff.
    init(name: String, aliases: [String], rules: TokenRules?) {
        self.name = name
        self.aliases = aliases
        grammar = rules.map { .tokens(CompiledRules($0)) } ?? .diff
    }

    /// `text`'s spans in this language.
    public func spans(_ text: String) -> [CodeSpan] {
        let scan = { (bytes: UnsafeBufferPointer<UInt8>) -> [CodeSpan] in
            switch grammar {
            case .diff: return Self.diffSpans(bytes)
            case .tokens(let rules):
                var pass = CodePass(bytes, rules)
                pass.run()
                return pass.spans
            }
        }
        if let spans = text.utf8.withContiguousStorageIfAvailable(scan) {
            return spans
        }
        return Array(text.utf8).withUnsafeBufferPointer(scan)
    }

    /// A diff coloured by line, as the design draws a patch: an added line,
    /// a removed one, a header -- a hunk's `@@`, a file's `---` and `+++`
    /// (never read as removed or added), or any other line a tool writes
    /// around them -- and the context, a line opening with a space. An empty
    /// line has no span.
    static func diffSpans(_ bytes: UnsafeBufferPointer<UInt8>) -> [CodeSpan] {
        var spans: [CodeSpan] = []
        var start = 0
        while start < bytes.count {
            var end = start
            while end < bytes.count, bytes[end] != Byte.newline { end += 1 }
            if end > start {
                let first = bytes[start]
                let three = end - start >= 3 && bytes[start + 1] == first && bytes[start + 2] == first
                let kind: CodeKind =
                    if (first == Byte.plus || first == Byte.minus) && three { .header }
                    else if first == Byte.at && end - start >= 2 && bytes[start + 1] == Byte.at { .header }
                    else if first == Byte.plus { .added }
                    else if first == Byte.minus { .removed }
                    else if first == Byte.space || first == Byte.tab { .context }
                    else { .header }
                spans.append(CodeSpan(kind, start..<end))
            }
            start = end + 1
        }
        return spans
    }
}

/// A token language's rules: its keywords, comments, strings, the words that
/// introduce a definition, and how its words are spelled.
struct TokenRules: Sendable {
    /// Reserved words and literals.
    var keywords: Set<String> = []
    /// Keywords matched in any case (SQL), written lower-case.
    var anyCase = false
    /// A word just after a `.` is a member, never a keyword (`promise.catch`,
    /// `.default`) -- except these (Rust's `.await`, Swift's `.self`).
    var memberKeywords: Set<String> = []
    /// What opens a comment to the end of its line (`//`, `#`, `--`).
    var lineComments: [String] = []
    /// A line comment opens only where a word could start, after a space or
    /// the line's start (`#` in a shell or YAML; `$#` and `a#b` are not comments).
    var commentAtWordStart = false
    /// A block comment's markers (`/*`, `*/`).
    var blockComment: (open: String, close: String)?
    /// Block comments nest (Swift, Rust, Kotlin).
    var nestedComments = false
    /// The language's strings, tried in order (a triple quote before a single).
    var strings: [StringRule] = []
    /// Words after which the next word is a definition's name (`def`, `func`).
    var definers: Set<String> = []
    /// A word just before `(` is a call's name.
    var calls = true
    /// A word just before `!(`, `![` or `!{` is a macro's name (Rust).
    var macros = false
    /// What may start a word besides letters, `_` and non-ASCII: `$` (JavaScript),
    /// `#` (directives: `#if`, `#include`), `@` (Objective-C's `@interface`).
    var wordStarts = ""
    /// What may continue a word besides letters, digits, `_` and non-ASCII:
    /// `$` (JavaScript), `-` between letters (a shell's `--max-time`, YAML keys).
    var wordInners = ""
    /// A word may end in `?` or `!` (Ruby's `valid?`, `save!`).
    var wordMarks = false
    /// A keyword used as an argument's label -- after `(` or `,`, just before
    /// `:` -- is a plain word (Swift's `sleep(for: .seconds(1))`, `dict[key, default: 0]`).
    var labels = false
}

/// One way a language writes a string.
struct StringRule: Sendable {
    enum Form: Sendable {
        /// Between `open` and `close`.
        case quoted(open: String, close: String)
        /// Raw, its quote fenced by a count of `#` that closes it too: Swift's
        /// `#"…"#` (at least one) and Rust's `r#"…"#` (any number).
        case hashed(quote: String, least: Int)
        /// C++'s raw string, `R"delimiter(…)delimiter"`.
        case delimited
        /// A character: one character or one escape between single quotes,
        /// else not a string at all (Rust's lifetime `'a`).
        case character
        /// A here-document: `<<EOF` (`<<-`, `<<~`, the word quoted or not),
        /// its body the lines after it through a line that is the word.
        /// `capital`: the word starts with a capital, or the marker is
        /// `<<-`/`<<~` (Ruby, where `<<` is also append).
        case heredoc(capital: Bool)
    }

    var form: Form
    /// What may lead the opening, at a word's start: Python's `r`, `b` and
    /// `f`, Rust's `b`, C++'s `u8` and `L`, C#'s `@` and `$`.
    var prefixes: [String] = [""]
    /// A backslash escapes the next character.
    var escapes = true
    /// The string may run past the end of its line.
    var multiline = false
    /// The closing quote written twice is a quote in the string (SQL's `''`).
    var doubles = false
    /// What opens an interpolation inside it (`${`, `\(`, `#{`, `$(`): read to
    /// its matching bracket, quotes inside skipped, all of it string.
    var interpolations: [String] = []
    /// It may open straight after a word (a JavaScript tagged template, a
    /// here-document); otherwise only at a word's start, so an apostrophe in
    /// `don't` opens nothing.
    var afterWord = false

    /// `"…"` and its kin.
    static func quoted(_ quote: String, close: String? = nil, prefixes: [String] = [""], escapes: Bool = true,
                       multiline: Bool = false, doubles: Bool = false, interpolations: [String] = [], afterWord: Bool = false) -> StringRule {
        StringRule(form: .quoted(open: quote, close: close ?? quote), prefixes: prefixes, escapes: escapes, multiline: multiline,
                   doubles: doubles, interpolations: interpolations, afterWord: afterWord)
    }
}

extension CodeLanguage {
    /// Every language the lexer knows, with its aliases. Any other fence draws
    /// in one colour until it is added here.
    public static let all: [CodeLanguage] = [
        python, swift, javascript, typescript, c, cpp, objc, rust, go, java, kotlin, csharp, ruby, shell, sql, json, yaml, toml,
        CodeLanguage(name: "diff", aliases: ["diff", "patch"], rules: nil),
    ]

    static let python = CodeLanguage(name: "python", aliases: ["python", "py", "python3", "py3"], rules: TokenRules(
        keywords: words("""
            False None True and as assert async await break class continue def del elif else except finally for from \
            global if import in is lambda nonlocal not or pass raise return try while with yield
            """),
        lineComments: ["#"],
        strings: [
            .quoted(tripleQuote, prefixes: anyCase(["", "r", "u", "b", "f", "t", "br", "rb", "fr", "rf", "tr", "rt"]), multiline: true),
            .quoted("'''", prefixes: anyCase(["", "r", "u", "b", "f", "t", "br", "rb", "fr", "rf", "tr", "rt"]), multiline: true),
            .quoted("\"", prefixes: anyCase(["", "r", "u", "b", "f", "t", "br", "rb", "fr", "rf", "tr", "rt"])),
            .quoted("'", prefixes: anyCase(["", "r", "u", "b", "f", "t", "br", "rb", "fr", "rf", "tr", "rt"])),
        ],
        definers: ["def"]))

    static let swift = CodeLanguage(name: "swift", aliases: ["swift"], rules: TokenRules(
        keywords: words("""
            associatedtype class deinit enum extension fileprivate func import init inout internal let operator private \
            precedencegroup protocol public rethrows static struct subscript typealias var break case catch continue default \
            defer do else fallthrough for guard if in repeat return throw switch where while as false is nil self Self super \
            throws true try await async actor nonisolated isolated some any convenience final indirect lazy mutating \
            nonmutating override required unowned weak willSet didSet macro #if #else #elseif #endif #available \
            #unavailable #selector #keyPath #sourceLocation #warning #error
            """),
        memberKeywords: ["self", "init", "Type"],
        lineComments: ["//"], blockComment: ("/*", "*/"), nestedComments: true,
        strings: [
            StringRule(form: .hashed(quote: tripleQuote, least: 1), escapes: false, multiline: true),
            StringRule(form: .hashed(quote: "\"", least: 1), escapes: false),
            .quoted(tripleQuote, multiline: true, interpolations: [#"\("#]),
            .quoted("\"", interpolations: [#"\("#]),
        ],
        definers: ["func"], wordStarts: "#", labels: true))

    static let javascriptKeywords = words("""
        break case catch class const continue debugger default delete do else export extends finally for function if \
        import in instanceof let new return super switch this throw try typeof var void while with yield async await of \
        static from as true false null undefined
        """)

    static let javascriptStrings: [StringRule] = [
        .quoted("`", multiline: true, interpolations: ["${"], afterWord: true),
        .quoted("\""),
        .quoted("'"),
    ]

    static let javascript = CodeLanguage(name: "javascript", aliases: ["javascript", "js", "jsx", "mjs", "cjs"], rules: TokenRules(
        keywords: javascriptKeywords,
        lineComments: ["//"], blockComment: ("/*", "*/"),
        strings: javascriptStrings,
        definers: ["function"], wordStarts: "$", wordInners: "$"))

    static let typescript = CodeLanguage(name: "typescript", aliases: ["typescript", "ts", "tsx", "mts", "cts"], rules: TokenRules(
        keywords: javascriptKeywords.union(words("""
            interface type enum implements private protected public readonly declare namespace abstract keyof infer is \
            satisfies override
            """)),
        lineComments: ["//"], blockComment: ("/*", "*/"),
        strings: javascriptStrings,
        definers: ["function"], wordStarts: "$", wordInners: "$"))

    static let cKeywords = words("""
        auto break case char const continue default do double else enum extern float for goto if inline int long register \
        restrict return short signed sizeof static struct switch typedef union unsigned void volatile while _Bool _Atomic \
        _Alignas _Alignof _Generic _Noreturn _Static_assert _Thread_local bool true false NULL #include #define #undef #if \
        #ifdef #ifndef #elif #else #endif #pragma #error #warning #line
        """)

    static let cStrings: [StringRule] = [
        .quoted("\"", prefixes: ["", "u8", "u", "U", "L"]),
        StringRule(form: .character, prefixes: ["", "u8", "u", "U", "L"]),
    ]

    static func cFamily(_ name: String, _ aliases: [String], keywords: Set<String>, strings: [StringRule], wordStarts: String) -> CodeLanguage {
        CodeLanguage(name: name, aliases: aliases, rules: TokenRules(
            keywords: keywords,
            lineComments: ["//"], blockComment: ("/*", "*/"),
            strings: strings,
            wordStarts: wordStarts))
    }

    static let c = cFamily("c", ["c", "h"], keywords: cKeywords, strings: cStrings, wordStarts: "#")

    static let cpp = cFamily("cpp", ["cpp", "c++", "cc", "cxx", "hpp", "hxx", "hh"], keywords: cKeywords.union(words("""
        alignas alignof asm catch class concept consteval constexpr constinit const_cast co_await co_return co_yield \
        decltype delete dynamic_cast explicit export friend mutable namespace new noexcept nullptr operator private \
        protected public reinterpret_cast requires static_assert static_cast template this thread_local throw try typeid \
        typename using virtual override final wchar_t char8_t char16_t char32_t
        """)), strings: [StringRule(form: .delimited, prefixes: ["R", "u8R", "uR", "UR", "LR"], escapes: false, multiline: true)] + cStrings,
        wordStarts: "#")

    static let objc = cFamily("objc", ["objc", "objective-c", "objectivec", "obj-c", "objc++", "objective-c++", "mm"],
        keywords: cKeywords.union(words("""
            #import @interface @implementation @end @property @protocol @class @selector @synthesize @dynamic \
            @autoreleasepool @try @catch @finally @throw @optional @required @public @private @protected @package @import \
            @encode @YES @NO self super nil Nil YES NO id instancetype BOOL nonatomic strong weak readonly readwrite \
            nullable nonnull __weak __strong __block
            """)),
        strings: [.quoted("\"", prefixes: ["", "@", "u8", "u", "U", "L"]), StringRule(form: .character, prefixes: ["", "u8", "u", "U", "L"])],
        wordStarts: "#@")

    static let rust = CodeLanguage(name: "rust", aliases: ["rust", "rs"], rules: TokenRules(
        keywords: words("""
            as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move \
            mut pub ref return self Self static struct super trait true type unsafe use where while union macro_rules
            """),
        memberKeywords: ["await"],
        lineComments: ["//"], blockComment: ("/*", "*/"), nestedComments: true,
        strings: [
            StringRule(form: .hashed(quote: "\"", least: 0), prefixes: ["r", "br", "cr"], escapes: false, multiline: true),
            .quoted("\"", prefixes: ["", "b", "c"], multiline: true),
            StringRule(form: .character, prefixes: ["", "b"]),
        ],
        definers: ["fn"], macros: true))

    static let go = CodeLanguage(name: "go", aliases: ["go", "golang"], rules: TokenRules(
        keywords: words("""
            break case chan const continue default defer else fallthrough for func go goto if import interface map package \
            range return select struct switch type var true false nil iota
            """),
        lineComments: ["//"], blockComment: ("/*", "*/"),
        strings: [.quoted("\""), .quoted("`", escapes: false, multiline: true), StringRule(form: .character)],
        definers: ["func"]))

    static let java = CodeLanguage(name: "java", aliases: ["java"], rules: TokenRules(
        keywords: words("""
            abstract assert boolean break byte case catch char class const continue default do double else enum extends \
            final finally float for goto if implements import instanceof int interface long native new package private \
            protected public return short static strictfp super switch synchronized this throw throws transient try void \
            volatile while var record yield sealed permits true false null
            """),
        lineComments: ["//"], blockComment: ("/*", "*/"),
        strings: [.quoted(tripleQuote, multiline: true), .quoted("\""), StringRule(form: .character)]))

    static let kotlin = CodeLanguage(name: "kotlin", aliases: ["kotlin", "kt", "kts"], rules: TokenRules(
        keywords: words("""
            as break class continue do else false for fun if in interface is null object package return super this throw \
            true try typealias typeof val var when while by catch constructor finally get import init set where abstract \
            annotation companion const crossinline data enum expect actual external final infix inline inner internal \
            lateinit noinline open operator out override private protected public reified sealed suspend tailrec vararg
            """),
        lineComments: ["//"], blockComment: ("/*", "*/"), nestedComments: true,
        strings: [
            .quoted(tripleQuote, escapes: false, multiline: true, interpolations: ["${"]),
            .quoted("\"", interpolations: ["${"]),
            StringRule(form: .character),
        ],
        definers: ["fun"]))

    static let csharp = CodeLanguage(name: "csharp", aliases: ["csharp", "cs", "c#"], rules: TokenRules(
        keywords: words("""
            abstract as base bool break byte case catch char checked class const continue decimal default delegate do \
            double else enum event explicit extern false finally fixed float for foreach goto if implicit in int interface \
            internal is lock long namespace new null object operator out override params private protected public readonly \
            ref return sbyte sealed short sizeof stackalloc static string struct switch this throw true try typeof uint \
            ulong unchecked unsafe ushort using virtual void volatile while async await var dynamic get set init record \
            yield partial where when nameof global #if #else #elif #endif #region #endregion #define #pragma #nullable
            """),
        lineComments: ["//"], blockComment: ("/*", "*/"),
        strings: [
            .quoted(tripleQuote, prefixes: ["", "$", "$$"], escapes: false, multiline: true),
            .quoted("\"", prefixes: ["@", "$@", "@$"], escapes: false, multiline: true, doubles: true),
            .quoted("\"", prefixes: ["", "$"]),
            StringRule(form: .character),
        ],
        wordStarts: "#"))

    static let ruby = CodeLanguage(name: "ruby", aliases: ["ruby", "rb"], rules: TokenRules(
        keywords: words("""
            BEGIN END alias and begin break case class def defined? do else elsif end ensure false for if in module next \
            nil not or redo rescue retry return self super then true undef unless until when while yield __FILE__ __LINE__
            """),
        lineComments: ["#"],
        strings: [
            StringRule(form: .heredoc(capital: true), afterWord: true),
            .quoted("\"", multiline: true, interpolations: ["#{"]),
            .quoted("'", multiline: true),
        ],
        definers: ["def"], wordMarks: true))

    static let shell = CodeLanguage(name: "shell", aliases: ["shell", "sh", "bash", "zsh"], rules: TokenRules(
        keywords: words("""
            if then else elif fi case esac for select while until do done in function time return break continue local \
            export readonly declare unset exit
            """),
        lineComments: ["#"], commentAtWordStart: true,
        strings: [
            StringRule(form: .heredoc(capital: false), afterWord: true),
            .quoted("\"", multiline: true, interpolations: ["$(", "${"]),
            .quoted("'", prefixes: ["$"], multiline: true),
            .quoted("'", escapes: false, multiline: true),
            .quoted("`", multiline: true),
        ],
        definers: ["function"], wordInners: "-"))

    static let sql = CodeLanguage(name: "sql", aliases: ["sql"], rules: TokenRules(
        keywords: words("""
            select from where and or not insert into values update set delete create table drop alter add column index \
            primary key foreign references join inner left right outer full cross natural on using as group by order having \
            limit offset distinct union all exists in is null like ilike between case when then else end begin commit \
            rollback transaction with returning default unique check constraint view trigger function procedure if replace \
            asc desc true false cast database schema grant revoke over partition window int integer bigint smallint varchar \
            char text boolean bool date time timestamp timestamptz interval numeric decimal real float double serial \
            bigserial uuid json jsonb blob
            """),
        anyCase: true,
        lineComments: ["--"], blockComment: ("/*", "*/"),
        strings: [.quoted("'", escapes: false, multiline: true, doubles: true)],
        definers: ["function", "procedure"]))

    static let json = CodeLanguage(name: "json", aliases: ["json"], rules: TokenRules(
        keywords: ["true", "false", "null"],
        strings: [.quoted("\"")],
        calls: false))

    static let yaml = CodeLanguage(name: "yaml", aliases: ["yaml", "yml"], rules: TokenRules(
        keywords: words("true false null True False TRUE FALSE Null NULL"),
        lineComments: ["#"], commentAtWordStart: true,
        strings: [.quoted("\"", multiline: true), .quoted("'", escapes: false, multiline: true, doubles: true)],
        calls: false, wordInners: "-"))

    static let toml = CodeLanguage(name: "toml", aliases: ["toml"], rules: TokenRules(
        keywords: ["true", "false", "inf", "nan"],
        lineComments: ["#"],
        strings: [
            .quoted(tripleQuote, multiline: true),
            .quoted("'''", escapes: false, multiline: true),
            .quoted("\""),
            .quoted("'", escapes: false),
        ],
        calls: false, wordInners: "-"))

    /// Three double quotes: Python's, Swift's, Kotlin's long strings.
    static let tripleQuote = "\"\"\""

    /// A list of words, split at spaces.
    static func words(_ list: String) -> Set<String> {
        Set(list.split(separator: " ").map(String.init))
    }

    /// Each of `prefixes` in every mix of upper and lower case (Python's `Rb`, `bR`).
    static func anyCase(_ prefixes: [String]) -> [String] {
        prefixes.flatMap { prefix in
            prefix.reduce([""]) { spellings, letter in
                spellings.flatMap { [$0 + letter.lowercased(), $0 + letter.uppercased()] }
            }
        }
        .reduce(into: [String]()) { if !$0.contains($1) { $0.append($1) } }
    }
}

// MARK: - The pass

/// The bytes the pass looks for.
enum Byte {
    static let newline = UInt8(ascii: "\n")
    static let tab = UInt8(ascii: "\t")
    static let space = UInt8(ascii: " ")
    static let carriageReturn = UInt8(ascii: "\r")
    static let plus = UInt8(ascii: "+")
    static let minus = UInt8(ascii: "-")
    static let at = UInt8(ascii: "@")
    static let dot = UInt8(ascii: ".")
    static let backslash = UInt8(ascii: "\\")
    static let quote = UInt8(ascii: "\"")
    static let apostrophe = UInt8(ascii: "'")
    static let backtick = UInt8(ascii: "`")
    static let hash = UInt8(ascii: "#")
    static let underscore = UInt8(ascii: "_")
    static let open = UInt8(ascii: "(")
    static let close = UInt8(ascii: ")")
    static let less = UInt8(ascii: "<")
    static let bang = UInt8(ascii: "!")
    static let question = UInt8(ascii: "?")
    static let equals = UInt8(ascii: "=")
    static let dollar = UInt8(ascii: "$")

    static func isDigit(_ b: UInt8) -> Bool { b >= 0x30 && b <= 0x39 }
    static func isLetter(_ b: UInt8) -> Bool { (b | 0x20) >= 0x61 && (b | 0x20) <= 0x7A }
    static func isHex(_ b: UInt8) -> Bool { isDigit(b) || ((b | 0x20) >= 0x61 && (b | 0x20) <= 0x66) }
    static func isAlphanumeric(_ b: UInt8) -> Bool { isLetter(b) || isDigit(b) || b == underscore }
    static func isBlank(_ b: UInt8) -> Bool { b == space || b == tab || b == carriageReturn }
}

/// A set of bytes, as four words of bits.
struct ByteSet: Sendable {
    private var words: (UInt64, UInt64, UInt64, UInt64) = (0, 0, 0, 0)

    init(_ bytes: some Sequence<UInt8>) {
        for byte in bytes { insert(byte) }
    }

    mutating func insert(_ byte: UInt8) {
        let bit = UInt64(1) << UInt64(byte & 63)
        switch byte >> 6 {
        case 0: words.0 |= bit
        case 1: words.1 |= bit
        case 2: words.2 |= bit
        default: words.3 |= bit
        }
    }

    func contains(_ byte: UInt8) -> Bool {
        let bit = UInt64(1) << UInt64(byte & 63)
        switch byte >> 6 {
        case 0: return words.0 & bit != 0
        case 1: return words.1 & bit != 0
        case 2: return words.2 & bit != 0
        default: return words.3 & bit != 0
        }
    }
}


/// A language's rules as the pass reads them: every string of the table in
/// bytes, once per language rather than once per token.
struct CompiledRules: Sendable {
    /// One string rule in bytes.
    struct StringForm: Sendable {
        let rule: StringRule
        let prefixes: [[UInt8]]
        /// A quoted string's opening, or a hashed one's quote.
        let open: [UInt8]
        /// A quoted string's closing.
        let close: [UInt8]
        let interpolations: [[UInt8]]
    }

    let rules: TokenRules
    let wordStart: ByteSet
    let wordInner: ByteSet
    /// A `-` may join a word's characters.
    let dashes: Bool
    let lineComments: [[UInt8]]
    let blockComment: (open: [UInt8], close: [UInt8])?
    let strings: [StringForm]
    /// The first byte of every way a string opens, its prefix included: a
    /// byte not among them opens none.
    let stringStarts: ByteSet
    let longestKeyword: Int

    init(_ rules: TokenRules) {
        self.rules = rules
        let letters = (UInt8(ascii: "a")...UInt8(ascii: "z")).map { $0 } + (UInt8(ascii: "A")...UInt8(ascii: "Z")).map { $0 }
        let starts = letters + [Byte.underscore] + (0x80...0xFF).map { UInt8($0) }
        let digits = (UInt8(ascii: "0")...UInt8(ascii: "9")).map { $0 }
        wordStart = ByteSet(starts + Array(rules.wordStarts.utf8))
        wordInner = ByteSet(starts + digits + rules.wordInners.utf8.filter { $0 != Byte.minus })
        dashes = rules.wordInners.utf8.contains(Byte.minus)
        lineComments = rules.lineComments.map { Array($0.utf8) }
        blockComment = rules.blockComment.map { (Array($0.open.utf8), Array($0.close.utf8)) }
        strings = rules.strings.map { rule in
            let (open, close): (String, String) =
                switch rule.form {
                case .quoted(let open, let close): (open, close)
                case .hashed(let quote, _): (quote, quote)
                case .delimited: ("\"", "")
                case .character: ("'", "'")
                case .heredoc: ("<<", "")
                }
            return StringForm(rule: rule, prefixes: rule.prefixes.map { Array($0.utf8) }, open: Array(open.utf8),
                              close: Array(close.utf8), interpolations: rule.interpolations.map { Array($0.utf8) })
        }
        var stringStarts = ByteSet([])
        for form in strings {
            for prefix in form.prefixes {
                if let first = prefix.first {
                    stringStarts.insert(first)
                } else if case .hashed(_, let least) = form.rule.form, least > 0 {
                    stringStarts.insert(Byte.hash)
                } else if let first = form.open.first {
                    stringStarts.insert(first)
                }
            }
        }
        self.stringStarts = stringStarts
        longestKeyword = rules.keywords.map(\.utf8.count).max() ?? 0
    }
}

/// One pass over a token language's text: each step at `at` reads one token
/// -- a blank, a comment, a string, a number or a word -- or skips one byte,
/// and notes the coloured ones. Every step moves forward, and none looks
/// further ahead than the end of its own line, except an open string or
/// comment, which runs on to its close or the end of the text.
struct CodePass {
    let bytes: UnsafeBufferPointer<UInt8>
    let language: CompiledRules
    let count: Int
    private(set) var spans: [CodeSpan] = []
    private var at = 0
    /// Here-documents whose bodies start on the next line: each its word.
    private var heredocs: [[UInt8]] = []

    /// What a `#` must follow to open a shell's or YAML's comment.
    private static let commentBreaks = ByteSet("\t\n\r ;|&()".utf8)
    private static let heredocOpen = Array("<<".utf8)

    init(_ bytes: UnsafeBufferPointer<UInt8>, _ language: CompiledRules) {
        self.bytes = bytes
        self.language = language
        count = bytes.count
    }

    private var rules: TokenRules { language.rules }

    mutating func run() {
        while at < count {
            let byte = bytes[at]
            if byte == Byte.newline {
                at += 1
                if !heredocs.isEmpty { heredocBodies() }
            } else if Byte.isBlank(byte) {
                at += 1
            } else if !(lineComment() || blockComment() || (language.stringStarts.contains(byte) && string()) || number() || word()) {
                at += 1
            }
        }
    }

    // MARK: Helpers

    private mutating func note(_ kind: CodeKind, _ range: Range<Int>) {
        guard !range.isEmpty else { return }
        spans.append(CodeSpan(kind, range))
    }

    private func matches(_ literal: [UInt8], at index: Int) -> Bool {
        guard index >= 0, index + literal.count <= count else { return false }
        var offset = 0
        while offset < literal.count {
            if bytes[index + offset] != literal[offset] { return false }
            offset += 1
        }
        return true
    }

    private func endOfLine(from index: Int) -> Int {
        var end = index
        while end < count, bytes[end] != Byte.newline { end += 1 }
        return end
    }

    private func text(_ range: Range<Int>) -> String {
        String(decoding: UnsafeBufferPointer(rebasing: bytes[range]), as: UTF8.self)
    }

    /// Whether `index` is where a word could start: the text's start, or not
    /// just after a word's character.
    private func atWordStart(_ index: Int) -> Bool {
        index == 0 || !(language.wordInner.contains(bytes[index - 1]) || language.wordStart.contains(bytes[index - 1]))
    }

    /// The end of the word starting at `index`: its characters, a `-` between
    /// them where the language allows one, and a closing `?` or `!` in Ruby.
    private func wordEnd(from index: Int) -> Int {
        var end = index + 1
        while end < count {
            let byte = bytes[end]
            if language.wordInner.contains(byte) {
                end += 1
            } else if language.dashes, byte == Byte.minus, end + 1 < count, Byte.isAlphanumeric(bytes[end + 1]) {
                end += 1
            } else {
                break
            }
        }
        if rules.wordMarks, end < count, bytes[end] == Byte.question || bytes[end] == Byte.bang,
            end + 1 >= count || bytes[end + 1] != Byte.equals
        {
            end += 1
        }
        return end
    }

    private func isKeyword(_ range: Range<Int>) -> Bool {
        guard range.count <= language.longestKeyword else { return false }
        let word = text(range)
        return rules.keywords.contains(rules.anyCase ? word.lowercased() : word)
    }

    // MARK: Comments

    private mutating func lineComment() -> Bool {
        for marker in language.lineComments where matches(marker, at: at) {
            if rules.commentAtWordStart, at > 0, !Self.commentBreaks.contains(bytes[at - 1]) { continue }
            let end = endOfLine(from: at)
            note(.comment, at..<end)
            at = end
            return true
        }
        return false
    }

    private mutating func blockComment() -> Bool {
        guard let comment = language.blockComment, matches(comment.open, at: at) else { return false }
        var depth = 1
        var index = at + comment.open.count
        while index < count, depth > 0 {
            if matches(comment.close, at: index) {
                depth -= 1
                index += comment.close.count
            } else if rules.nestedComments, matches(comment.open, at: index) {
                depth += 1
                index += comment.open.count
            } else {
                index += 1
            }
        }
        note(.comment, at..<min(index, count))
        at = index
        return true
    }

    // MARK: Strings

    private mutating func string() -> Bool {
        let wordStartHere = atWordStart(at)
        for form in language.strings where wordStartHere || form.rule.afterWord {
            for prefix in form.prefixes where matches(prefix, at: at) {
                if let end = stringEnd(form, from: at + prefix.count) {
                    note(.string, at..<end)
                    at = end
                    return true
                }
            }
        }
        return false
    }

    /// Where a string of `form` opening at `index` ends -- past its closing
    /// quote, at the end of its line where it may not run on, or at the end
    /// of the text -- or `nil` when no such string opens there.
    private mutating func stringEnd(_ form: CompiledRules.StringForm, from index: Int) -> Int? {
        switch form.rule.form {
        case .quoted:
            guard matches(form.open, at: index) else { return nil }
            return quotedEnd(form, from: index + form.open.count)
        case .hashed(_, let least):
            var index = index
            var hashes = 0
            while index < count, bytes[index] == Byte.hash {
                hashes += 1
                index += 1
            }
            guard hashes >= least, matches(form.open, at: index) else { return nil }
            let close = form.open + [UInt8](repeating: Byte.hash, count: hashes)
            return rawEnd(close: close, from: index + form.open.count, multiline: form.rule.multiline)
        case .delimited:
            guard index < count, bytes[index] == Byte.quote else { return nil }
            var end = index + 1
            while end < count, end - index <= 17, bytes[end] != Byte.open {
                let byte = bytes[end]
                if Byte.isBlank(byte) || byte == Byte.newline || byte == Byte.backslash || byte == Byte.close { return nil }
                end += 1
            }
            guard end < count, bytes[end] == Byte.open else { return nil }
            let close = [Byte.close] + Array(bytes[(index + 1)..<end]) + [Byte.quote]
            return rawEnd(close: close, from: end + 1, multiline: true)
        case .character:
            return characterEnd(from: index)
        case .heredoc(let capital):
            return heredocMarker(from: index, capital: capital)
        }
    }

    /// A quoted string's end: escapes and interpolations skipped, the quote
    /// written twice kept inside where the rule says so.
    private func quotedEnd(_ form: CompiledRules.StringForm, from start: Int) -> Int {
        let rule = form.rule
        var index = start
        while index < count {
            let byte = bytes[index]
            if byte == Byte.newline, !rule.multiline { return index }
            if let opener = form.interpolations.first(where: { matches($0, at: index) }), let bracket = opener.last {
                index = interpolationEnd(from: index + opener.count, bracket: bracket, multiline: rule.multiline)
                continue
            }
            if rule.escapes, byte == Byte.backslash {
                // The escaped character is skipped -- a newline too, which
                // carries the string on to the next line, as C and Python read it.
                index += 2
                continue
            }
            if matches(form.close, at: index) {
                if rule.doubles, matches(form.close, at: index + form.close.count) {
                    index += form.close.count * 2
                    continue
                }
                return index + form.close.count
            }
            index += 1
        }
        return count
    }

    /// Past an interpolation opened by `bracket`: to the bracket that matches
    /// it, brackets inside counted and quoted strings inside skipped, stopping
    /// at the line's end where the string may not run on.
    private func interpolationEnd(from start: Int, bracket: UInt8, multiline: Bool) -> Int {
        let closer = bracket == Byte.open ? Byte.close : UInt8(ascii: "}")
        var depth = 1
        var index = start
        while index < count {
            let byte = bytes[index]
            if byte == Byte.newline, !multiline { return index }
            if byte == bracket {
                depth += 1
            } else if byte == closer {
                depth -= 1
                if depth == 0 { return index + 1 }
            } else if byte == Byte.quote || byte == Byte.apostrophe || byte == Byte.backtick {
                var inner = index + 1
                while inner < count, bytes[inner] != byte {
                    if bytes[inner] == Byte.newline, !multiline { return inner }
                    inner += bytes[inner] == Byte.backslash ? 2 : 1
                }
                index = min(inner + 1, count)
                continue
            }
            index += 1
        }
        return count
    }

    /// A raw string's end: its `close` found, nothing escaped.
    private func rawEnd(close: [UInt8], from start: Int, multiline: Bool) -> Int {
        var index = start
        while index < count {
            if bytes[index] == Byte.newline, !multiline { return index }
            if matches(close, at: index) { return index + close.count }
            index += 1
        }
        return count
    }

    /// A character literal's end: one character, or one escape, then the
    /// closing quote on the same line; `nil` when it is not one (Rust's
    /// lifetime `'a`).
    private func characterEnd(from start: Int) -> Int? {
        guard start < count, bytes[start] == Byte.apostrophe else { return nil }
        var index = start + 1
        guard index < count, bytes[index] != Byte.apostrophe, bytes[index] != Byte.newline else { return nil }
        if bytes[index] == Byte.backslash {
            index += 1
            guard index < count, bytes[index] != Byte.newline else { return nil }
            let kind = bytes[index]
            index += 1
            if kind == UInt8(ascii: "u"), index < count, bytes[index] == UInt8(ascii: "{") {
                while index < count, index - start < 14, bytes[index] != UInt8(ascii: "}"), bytes[index] != Byte.newline { index += 1 }
                index += 1
            } else {
                let digits =
                    switch kind {
                    case UInt8(ascii: "x"): 2
                    case UInt8(ascii: "u"): 4
                    case UInt8(ascii: "U"): 8
                    default: Byte.isDigit(kind) ? 2 : 0
                    }
                var taken = 0
                while taken < digits, index < count, Byte.isHex(bytes[index]) {
                    index += 1
                    taken += 1
                }
            }
        } else {
            // One character: its UTF-8 sequence's length, from its lead byte.
            let lead = bytes[index]
            index += lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2
        }
        guard index < count, bytes[index] == Byte.apostrophe else { return nil }
        return index + 1
    }

    /// A here-document's marker, `<<EOF`: its end, its word noted for the
    /// body that starts on the next line; `nil` when it is not one (`<<<`,
    /// `1 << 2`, Ruby's `list << item`).
    private mutating func heredocMarker(from start: Int, capital: Bool) -> Int? {
        guard matches(Self.heredocOpen, at: start) else { return nil }
        var index = start + 2
        guard index < count, bytes[index] != Byte.less else { return nil }
        var dashed = false
        if bytes[index] == Byte.minus || bytes[index] == UInt8(ascii: "~") {
            dashed = true
            index += 1
        }
        var quote: UInt8?
        if index < count, bytes[index] == Byte.apostrophe || bytes[index] == Byte.quote {
            quote = bytes[index]
            index += 1
        }
        let word = index
        guard index < count, Byte.isLetter(bytes[index]) || bytes[index] == Byte.underscore else { return nil }
        if capital, !dashed, quote == nil, !(bytes[index] >= UInt8(ascii: "A") && bytes[index] <= UInt8(ascii: "Z")) { return nil }
        while index < count, Byte.isAlphanumeric(bytes[index]) { index += 1 }
        let name = Array(bytes[word..<index])
        if let quote {
            guard index < count, bytes[index] == quote else { return nil }
            index += 1
        }
        heredocs.append(name)
        return index
    }

    /// The bodies of the here-documents the line just ended opened, `at` the
    /// start of the next: each a string through the line that is its word
    /// alone (indented or not), or to the end of the text. `at` is left on
    /// the newline that ends the last of them.
    private mutating func heredocBodies() {
        let pending = heredocs
        heredocs = []
        for (place, name) in pending.enumerated() {
            let body = at
            var line = at
            var end = count
            while line < count {
                let lineEnd = endOfLine(from: line)
                var first = line
                while first < lineEnd, Byte.isBlank(bytes[first]) { first += 1 }
                var last = lineEnd
                while last > first, bytes[last - 1] == Byte.carriageReturn { last -= 1 }
                if last - first == name.count, matches(name, at: first) {
                    end = lineEnd
                    break
                }
                line = lineEnd + 1
            }
            note(.string, body..<end)
            at = end
            if at < count, place < pending.count - 1 { at += 1 }
        }
    }

    // MARK: Numbers

    private mutating func number() -> Bool {
        let byte = bytes[at]
        let leadingDot = byte == Byte.dot && at + 1 < count && Byte.isDigit(bytes[at + 1])
        guard Byte.isDigit(byte) || leadingDot else { return false }
        if at > 0 {
            let before = bytes[at - 1]
            if language.wordStart.contains(before) || Byte.isDigit(before) || before == Byte.dollar { return false }
            if leadingDot, before == Byte.close || before == UInt8(ascii: "]") || before == Byte.dot { return false }
        }
        var end = at
        while end < count, Byte.isDigit(bytes[end]) || bytes[end] == Byte.underscore { end += 1 }
        // A fraction, and the further groups of a version or an address (1.2.3).
        while end + 1 < count, bytes[end] == Byte.dot, Byte.isDigit(bytes[end + 1]) {
            end += 1
            while end < count, Byte.isDigit(bytes[end]) || bytes[end] == Byte.underscore { end += 1 }
        }
        if end < count, bytes[end] | 0x20 == UInt8(ascii: "e") {
            var exponent = end + 1
            if exponent < count, bytes[exponent] == Byte.plus || bytes[exponent] == Byte.minus { exponent += 1 }
            if exponent < count, Byte.isDigit(bytes[exponent]) {
                end = exponent
                while end < count, Byte.isDigit(bytes[end]) || bytes[end] == Byte.underscore { end += 1 }
            }
        }
        // A radix's letters and digits (0x1F, 0b1010), or a type's suffix (2u64, 1.5f, 10n).
        while end < count, Byte.isAlphanumeric(bytes[end]) { end += 1 }
        note(.number, at..<end)
        at = end
        return true
    }

    // MARK: Words

    private mutating func word() -> Bool {
        guard language.wordStart.contains(bytes[at]) else { return false }
        let start = at
        let end = wordEnd(from: start)
        at = end
        let member = start > 0 && bytes[start - 1] == Byte.dot && !(start > 1 && bytes[start - 2] == Byte.dot)
        if isKeyword(start..<end), !member || rules.memberKeywords.contains(text(start..<end)), !isLabel(start, end) {
            note(.keyword, start..<end)
            if !member, isDefiner(start..<end) { definitionName() }
        } else if isCall(after: end) {
            note(.function, start..<end)
        }
        return true
    }

    /// Whether the word at `start..<end` is an argument's label: after `(`
    /// or `,` on its line, and just before a single `:`.
    private func isLabel(_ start: Int, _ end: Int) -> Bool {
        guard rules.labels, end < count, bytes[end] == UInt8(ascii: ":"), end + 1 >= count || bytes[end + 1] != UInt8(ascii: ":")
        else { return false }
        var before = start - 1
        while before >= 0, Byte.isBlank(bytes[before]) { before -= 1 }
        return before >= 0 && (bytes[before] == Byte.open || bytes[before] == UInt8(ascii: ",") || bytes[before] == UInt8(ascii: "["))
    }

    private func isDefiner(_ range: Range<Int>) -> Bool {
        guard !rules.definers.isEmpty else { return false }
        let word = text(range)
        return rules.definers.contains(rules.anyCase ? word.lowercased() : word)
    }

    private func isCall(after end: Int) -> Bool {
        guard end < count else { return false }
        if rules.calls, bytes[end] == Byte.open { return true }
        if rules.macros, bytes[end] == Byte.bang, end + 1 < count {
            let next = bytes[end + 1]
            return next == Byte.open || next == UInt8(ascii: "[") || next == UInt8(ascii: "{")
        }
        return false
    }

    /// The name a definer introduces: the next word on its line, past any
    /// qualifiers before it (Ruby's `def self.name`, Kotlin's `fun Type.name`).
    private mutating func definitionName() {
        var index = at
        while index < count, Byte.isBlank(bytes[index]) { index += 1 }
        while index < count, language.wordStart.contains(bytes[index]) {
            let start = index
            index = wordEnd(from: start)
            if index + 1 < count, bytes[index] == Byte.dot, language.wordStart.contains(bytes[index + 1]) {
                if isKeyword(start..<index) { note(.keyword, start..<index) }
                index += 1
                at = index
                continue
            }
            note(.function, start..<index)
            at = index
            return
        }
    }
}
