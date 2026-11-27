import BusinessReads
import Foundation
import Testing

/// The argv rule (GUI 4g): a person's text after `--`, every option before it
/// -- and since GUI M21 every name too, through `Argv.line`.
@Suite("Argv")
struct ArgvTests {
    @Test("text goes after the end of options; an option added later lands before it, or at the end when there is none")
    func textAndOptions() {
        let line = ["voice", "say", "-o", "/m/a.wav"] + Argv.text("--draft")
        #expect(line == ["voice", "say", "-o", "/m/a.wav", "--", "--draft"])
        #expect(Argv.adding(["--output-format", "stream-json"], to: line)
            == ["voice", "say", "-o", "/m/a.wav", "--output-format", "stream-json", "--", "--draft"])
        #expect(Argv.adding(["--dry-run"], to: ["chats", "list"]) == ["chats", "list", "--dry-run"])
        #expect(["chats", "title", "c1"] + Argv.text("--", "x") == ["chats", "title", "c1", "--", "--", "x"], "a text that is itself -- stays text")
        #expect(Argv.adding(["--json"], to: ["chats", "title", "c1", "--", "--", "x"]) == ["chats", "title", "c1", "--json", "--", "--", "x"],
                "added before the first --, the end of options")
    }

    @Test("the options are the words before the end of options: a text reading like a flag is not one")
    func options() {
        let line = ["video", "compose", "-o", "/m/a.mp4", "--", "--dry-run"]
        #expect(!Argv.options(of: line).contains("--dry-run"))
        #expect(Argv.options(of: Argv.adding(["--dry-run"], to: line)).contains("--dry-run"))
        #expect(Array(Argv.options(of: ["check", "--fix"])) == ["check", "--fix"])
    }

    // MARK: - The line builder (GUI M21)

    @Test("a line is the command, then its options, then -- and the names: one --, and none without names")
    func lineOrder() {
        #expect(Argv.line(["embed", "ingest"], options: [.value("--retriever", "lexical"), .flag("--graph")], names: ["-x", "/Users/someone/notes"])
            == ["embed", "ingest", "--retriever", "lexical", "--graph", "--", "-x", "/Users/someone/notes"])
        #expect(Argv.line(["embed", "info"], names: ["notes"]) == ["embed", "info", "--", "notes"])
        #expect(Argv.line(["graph", "build"], options: [.value("--source", "/s"), .value("--graph", "-g")])
            == ["graph", "build", "--source", "/s", "--graph", "-g"], "no names, no --: an option's value is its own")
        #expect(Argv.line(["embed", "query"], names: ["-x", "--", "-how"]) == ["embed", "query", "--", "-x", "--", "-how"],
                "a name that is itself -- stays a name, after the one end of options")
        #expect(Argv.line(["models", "list"]) == ["models", "list"])
    }

    @Test("an option that takes the -- after it as its own end is never left directly before it: it moves earlier, the rest in order")
    func eaterMovesEarlier() {
        #expect(Argv.line(["graph", "build"], options: [.flag("--quiet"), .value("--source", "/a"), .value("--source", "/b")], names: ["-x"])
            == ["graph", "build", "--source", "/a", "--source", "/b", "--quiet", "--", "-x"])
        #expect(Argv.line(["review", "targets", "add"], options: [.value("--repo", "o/r"), .value("--skills", "security")], names: ["-x"])
            == ["review", "targets", "add", "--skills", "security", "--repo", "o/r", "--", "-x"])
        #expect(Argv.line(["embed", "delete"], options: [.value("--source", "b.txt")], names: ["-x"])
            == ["embed", "delete", "--source", "b.txt", "--", "-x"], "embed delete's --source takes one value: no eater there")
        #expect(Argv.line(["graph", "build"], options: [.flag("--quiet"), .value("--source", "/a")])
            == ["graph", "build", "--quiet", "--source", "/a"], "with no -- to eat, the order is the caller's")
        #expect(Argv.line(["graph", "build"], options: [.value("--lang", "c"), .value("--source", "/a")], names: ["-x"])
            == ["graph", "build", "--lang", "c", "--source", "/a", "--", "-x"],
                "a line whose only options eat -- has no order that saves it: the caller names the thing another way (New graph's --graph)")
    }

    @Test("an option whose value is itself -- is one word, so the line keeps one end of options")
    func valueThatIsTheEnd() {
        let line = Argv.line(["graph", "report"], options: [.value("--graph", "--")])
        #expect(line == ["graph", "report", "--graph=--"])
        #expect(Argv.adding(["--output-format", "json"], to: line) == ["graph", "report", "--graph=--", "--output-format", "json"])
        #expect(Argv.line(["graph", "query"], options: [.value("--graph", "--")], names: ["fox"]) == ["graph", "query", "--graph=--", "--", "fox"])
        #expect(Argv.line(["graph", "query"], options: [.value("--graph", "-x")], names: ["fox"]) == ["graph", "query", "--graph", "-x", "--", "fox"],
                "any other value is the next word: the binary takes an option's one value whole")
    }

    @Test("the positionals are the words after the end of options; a line without one has none")
    func positionals() {
        #expect(Array(Argv.positionals(of: ["embed", "delete", "--yes", "--", "-x"])) == ["-x"])
        #expect(Array(Argv.positionals(of: ["embed", "query", "--", "-x", "--", "-how"])) == ["-x", "--", "-how"])
        #expect(Argv.positionals(of: ["embed", "list"]).isEmpty)
    }

    @Test("a line as a person types it: -- only before a name that begins with a dash, a word a shell would split or lose quoted")
    func shown() {
        #expect(Argv.shown(["embed", "delete", "--yes", "--", "notes"]) == "embed delete --yes notes")
        #expect(Argv.shown(["embed", "delete", "--yes", "--", "-x"]) == "embed delete --yes -- -x")
        #expect(Argv.shown(["embed", "ingest", "--retriever", "lexical", "--", " sp", "/Users/someone/My Notes"])
            == "embed ingest --retriever lexical ' sp' '/Users/someone/My Notes'")
        #expect(Argv.shown(["config", "set-backend", "--adapters", "", "--", "q4"]) == "config set-backend --adapters \"\" q4")
        #expect(Argv.shown(["datasets", "delete", "--yes", "--", "it's"]) == "datasets delete --yes 'it'\\''s'")
        #expect(Argv.shown(["models", "list"]) == "models list")
        #expect(Argv.shown(["embed", "query", "--", "notes", "-how"]) == "embed query -- notes -how", "any dash-led name keeps the --")
        #expect(Argv.shown(["embed", "info", "--", "café", "a@b"]) == "embed info café a@b")
        #expect(Argv.shown(["embed", "info", "--", "=x", "a=b", "^x"]) == "embed info '=x' a=b '^x'",
                "zsh expands a word that begins with = as a command's path, and ^ under EXTENDED_GLOB")
    }

    /// The binary's verdicts (`Fixtures/argv-end-of-options-verdicts.txt`,
    /// recorded from the built CLI over a sandbox): `help`, or `exit <n>:
    /// <first line>`, then a tab and the line. A parse refusal is CLI11's,
    /// before the verb ran: exit 106 (`… is required`), 109 (`… not
    /// expected`), or the verb's help.
    static let verdicts: [(verdict: String, line: [String])] = {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/argv-end-of-options-verdicts.txt")
        return ((try? String(contentsOf: url, encoding: .utf8)) ?? "").split(separator: "\n").compactMap { row in
            let parts = row.split(separator: "\t", maxSplits: 1).map(String.init)
            return parts.count == 2 ? (parts[0], parts[1].split(separator: " ").map(String.init)) : nil
        }
    }()

    static func refusedByTheParse(_ verdict: String) -> Bool {
        verdict == "help" || verdict.hasPrefix("exit 106:") || verdict.hasPrefix("exit 109:")
    }

    @Test("the options that eat -- are the binary's: each refused directly before it and reaching the verb earlier, every other option probed reaching it")
    func eatersAreTheBinarys() {
        #expect(Self.verdicts.count == 61, "recorded: \(Self.verdicts.count)")
        var eaters: [String: Set<String>] = [:]
        var reached: [String: Set<String>] = [:]
        for (verdict, words) in Self.verdicts {
            guard let end = words.firstIndex(of: Argv.endOfOptions), end >= 1 else { continue }
            let path = words.prefix { !$0.hasPrefix("-") }.joined(separator: " ")
            let before: String? = words[end - 1].hasPrefix("-") ? words[end - 1]
                : end >= 2 && words[end - 2].hasPrefix("-") ? words[end - 2] : nil
            let options = Set(words[..<end].filter { $0.hasPrefix("-") })
            if let before, Self.refusedByTheParse(verdict) {
                eaters[path, default: []].insert(before)
            } else if !Self.refusedByTheParse(verdict) {
                reached[path, default: []].formUnion(options)
            }
        }
        #expect(eaters == Argv.endOfOptionsEaters, "recorded eaters: \(eaters)")
        for (path, options) in eaters {
            #expect(options.isSubset(of: reached[path] ?? []), "\(path): each eater reaches the verb when not last before --")
        }
        #expect(reached["agents create"]?.contains("--mcp") == true, "--mcp takes no -- though its help says ...")
        #expect(reached["config set-backend"]?.isSuperset(of: ["--server-tools", "--mcp-serve", "--draft", "--adapters"]) == true)
    }

    @Test("today's shape is refused before the verb runs; the builder's reaches it")
    func todaysShapeRefused() {
        let verdict = Dictionary(Self.verdicts.map { ($0.line.joined(separator: " "), $0.verdict) }, uniquingKeysWith: { first, _ in first })
        #expect(verdict["embed info -x --output-format json"] == "exit 106: collection is required")
        #expect(verdict["datasets delete -h --yes"] == "help")
        let built = Argv.adding(["--output-format", "json"], to: Argv.line(["embed", "info"], names: ["-x"])).joined(separator: " ")
        #expect(verdict[built].map(Self.refusedByTheParse) == false, "\(built): \(verdict[built] ?? "never recorded")")
    }
}
