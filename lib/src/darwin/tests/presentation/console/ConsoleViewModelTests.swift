import BusinessJobs
import BusinessReads
import Foundation
import Testing

@testable import PresentationConsole

@Suite("ConsoleViewModel", .timeLimit(.minutes(1)))
@MainActor
struct ConsoleViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func console(_ runner: ScriptedRunner = ScriptedRunner(), _ jobs: [ScriptedJob] = []) -> (ConsoleViewModel, ScriptedJobs) {
        let starter = ScriptedJobs(jobs)
        let model = ConsoleViewModel(runner: runner, starter: starter)
        model.connect(binary: Self.binary)
        return (model, starter)
    }

    @Test("a line splits as a shell splits it: quotes group, a backslash escapes, the typed word apart")
    func words() {
        #expect(ConsoleViewModel.words("models list")! == (["models"], "list"))
        #expect(ConsoleViewModel.words("models list ")! == (["models", "list"], ""))
        #expect(ConsoleViewModel.words(#"video compose "how a heat pump works" -o out.mp4"#)! == (["video", "compose", "how a heat pump works", "-o"], "out.mp4"))
        #expect(ConsoleViewModel.words(#"knowledge query it\'s"#)! == (["knowledge", "query"], "it's"))
        #expect(ConsoleViewModel.words(#"say 'one "two"'"#)! == (["say"], #"one "two""#))
        #expect(ConsoleViewModel.words(#"say "open"# ) == nil, "an unclosed quote is no line")
        #expect(ConsoleViewModel.arguments("apogee models list")! == ["models", "list"])
        #expect(ConsoleViewModel.arguments("check")! == ["check"])
    }

    /// The CLI's quoting table, the one splitter the exec line runs (CLI 37f,
    /// 37h), case for case: each row is `line_tokens_test.cpp`'s, cited by
    /// its line, or the splitter's own rule where its tests have no case
    /// (the app cannot link the CLI's splitter -- the machine-mode rule -- so
    /// it carries the table as its own; GUI 6e). Each row is checked through
    /// `arguments`, the words a run is started with.
    static let quotingTable: [(line: String, words: [String], cites: String)] = [
        ("", [], "line_tokens_test.cpp:23"),
        ("   ", [], "line_tokens_test.cpp:24"),
        ("train run snap --dataset d", ["train", "run", "snap", "--dataset", "d"], "line_tokens_test.cpp:25"),
        ("  a\tb  ", ["a", "b"], "line_tokens_test.cpp:26"),
        ("say 'two words'", ["say", "two words"], "line_tokens_test.cpp:27"),
        ("say \"two words\"", ["say", "two words"], "line_tokens_test.cpp:28"),
        ("a'b c'd", ["ab cd"], "line_tokens_test.cpp:29"),
        ("'' \"\"", ["", ""], "line_tokens_test.cpp:30"),
        ("x ''", ["x", ""], "line_tokens_test.cpp:31"),
        // Single quotes keep backslashes; double quotes unescape only their four.
        (#"'a\b'"#, [#"a\b"#], "line_tokens_test.cpp:33"),
        (#""a\"b" "c\\d" "e\$f" "g\h""#, [#"a"b"#, #"c\d"#, "e$f", #"g\h"#], "line_tokens_test.cpp:34"),
        // The fourth of the four: a backtick (line_tokens.cpp:54).
        (#""\`x\`" "\`""#, ["`x`", "`"], "line_tokens.cpp:54"),
        // A backslash outside quotes takes the next character as it is.
        (#"a\ b c\'d"#, ["a b", "c'd"], "line_tokens_test.cpp:36"),
        // Nothing expands.
        ("$HOME ~ *.txt `x`", ["$HOME", "~", "*.txt", "`x`"], "line_tokens_test.cpp:38"),
        // The blanks are a space, a tab, a line feed and a carriage return,
        // and only those, read byte by byte: a no-break space (⌥Space) or any
        // other Unicode space stays inside its word, CR LF is two blanks, and
        // a combining mark is never part of the blank, quote or backslash
        // before it.
        ("models\u{00A0}list", ["models\u{00A0}list"], "line_tokens.cpp:9-11"),
        ("a\u{3000}b\u{2028}c d", ["a\u{3000}b\u{2028}c", "d"], "line_tokens.cpp:9-11"),
        ("a\r\nb\r\n", ["a", "b"], "line_tokens.cpp:9-11"),
        ("a\\\r\nb", ["a\r", "b"], "line_tokens.cpp:69-76"),
        ("a \u{301}b '\u{301}' \\\u{301}", ["a", "\u{301}b", "\u{301}", "\u{301}"], "line_tokens.cpp:9-11"),
    ]

    @Test("the split is the CLI's quoting table: inside double quotes a backslash escapes only \", \\, $ and a backtick and is kept before anything else; an empty quoted word is a word; only the CLI's four blanks split")
    func quotingTable() {
        for row in Self.quotingTable {
            #expect(ConsoleViewModel.arguments(row.line) == row.words, "\(row.line) (\(row.cites))")
        }
        // An open quote, or a backslash with nothing to escape, is no line,
        // said in the CLI's words (line_tokens_test.cpp:44-47); a backslash
        // before the line's end inside double quotes leaves the quote open.
        let unsplittable: [(line: String, why: ConsoleViewModel.Unsplittable)] = [
            ("say 'open", .singleQuoteOpen),
            ("say \"open", .doubleQuoteOpen),
            ("end\\", .backslashEndsTheLine),
            (#"say "end\"#, .doubleQuoteOpen),
            (#"say "end\""#, .doubleQuoteOpen),
        ]
        for (line, why) in unsplittable {
            #expect(ConsoleViewModel.split(line) == nil, "\(line)")
            #expect(ConsoleViewModel.arguments(line) == nil, "\(line)")
            #expect(ConsoleViewModel.tokens(line) == .failure(why), "\(line)")
        }
        #expect(ConsoleViewModel.Unsplittable.singleQuoteOpen.reason == "a single quote is left open")
        #expect(ConsoleViewModel.Unsplittable.doubleQuoteOpen.reason == "a double quote is left open")
        #expect(ConsoleViewModel.Unsplittable.backslashEndsTheLine.reason == "a backslash ends the line with nothing to escape")
        // The sweep's own line (2026-10-31): a regex and a Windows path
        // reach the binary with their backslashes, as the exec line passes them.
        #expect(ConsoleViewModel.arguments(#"embed query docs -- "match \d+ in C:\temp""#) == ["embed", "query", "docs", "--", #"match \d+ in C:\temp"#])
        #expect(ConsoleViewModel.arguments(#"apogee knowledge query "C:\Users\me" --require "\w+\.md$""#) == ["knowledge", "query", #"C:\Users\me"#, "--require", #"\w+\.md$"#])
        // The word being typed starts where it was typed, its quote counted.
        #expect(ConsoleViewModel.split(#"say "g\h"#) == nil)
        #expect(ConsoleViewModel.split(#"say "g\h""#) == ConsoleViewModel.Split(words: ["say"], partial: #"g\h"#, partialStart: 4))
        #expect(ConsoleViewModel.split(#"say "a\"b"#) == nil)
        #expect(ConsoleViewModel.split(#"say x "a\"b""#) == ConsoleViewModel.Split(words: ["say", "x"], partial: #"a"b"#, partialStart: 6))
    }

    @Test("a candidate quoted for the line splits back whole: a backslash, a dollar sign and a backtick in it kept")
    func quotedRoundTrips() {
        let candidates = [#"\d"#, #"match \d+ in C:\temp"#, "$HOME", "a $b", "`x`", "a `x` b", #"C:\temp"#, #"\$"#, #"\""#, #"\`"#, #"it's "quoted""#]
        for candidate in candidates {
            let typed = "say " + ConsoleViewModel.quoted(candidate)
            #expect(ConsoleViewModel.arguments(typed) == ["say", candidate], "\(candidate) as \(typed)")
            #expect(ConsoleViewModel.split(typed)?.partial == candidate, "\(candidate) as \(typed)")
        }
    }

    @Test("a run is started with every word the exec line would pass: an empty quoted last word kept, a no-break space inside its word, an escaped last blank kept; a line that does not split says why in the CLI's words")
    func runsEveryWord() async {
        let rows: [(typed: String, starts: [String], kept: String)] = [
            ("say ''", ["say", ""], "say ''"),
            (#"config set some.key """#, ["config", "set", "some.key", ""], #"config set some.key """#),
            (#"'' """#, ["", ""], #"'' """#),
            ("models\u{00A0}list", ["models\u{00A0}list"], "models\u{00A0}list"),
            ("models list\u{00A0}", ["models", "list\u{00A0}"], "models list\u{00A0}"),
            ("apogee\u{00A0}models", ["apogee\u{00A0}models"], "apogee\u{00A0}models"),
            (" \tmodels list \r\n", ["models", "list"], "models list"),
            (#"say end\ "#, ["say", "end "], #"say end\ "#),
            (#"say end\  "#, ["say", "end "], #"say end\ "#),
        ]
        let jobs = rows.map { _ in ScriptedJob() }
        let (model, starter) = console(ScriptedRunner(), jobs)
        for (row, job) in zip(rows, jobs) {
            model.input = row.typed
            model.run()
            job.end()
            #expect(await eventually { !model.isRunning }, "\(row.typed)")
        }
        #expect(starter.starts == rows.map(\.starts))
        #expect(model.history == rows.map(\.kept), "kept and recalled as it runs: an escaped last blank stays")
        #expect(model.lines.filter { $0.kind == .command }.map(\.text) == rows.map { "apogee " + $0.kept })
        #expect(model.lines.filter { $0.kind == .problem }.isEmpty)

        let (refusing, none) = console()
        for typed in ["say 'open", #"say "open"#, #"end\"#] {
            refusing.input = typed
            refusing.run()
            #expect(refusing.input == typed, "the line is left to finish")
        }
        #expect(none.starts.isEmpty && refusing.history.isEmpty)
        #expect(refusing.lines.map(\.kind) == [.problem, .problem, .problem])
        #expect(refusing.lines.map(\.text) == [
            "A single quote is left open.",
            "A double quote is left open.",
            "A backslash ends the line with nothing to escape.",
        ])
    }

    @Test("Tab replaces the word being typed from where it starts in the line's Unicode scalars, as the CLI counts its bytes: a decomposed letter or a combining mark before it moves nothing")
    func completionCountsScalars() async {
        let runner = ScriptedRunner()
        runner.answering(["__complete", "say"], .done(stdout: "Bob\n"))
        let (model, _) = console(runner)
        let cases: [(typed: String, asked: String, completed: String)] = [
            ("say cafe\u{301} Bo", "run __complete|say|cafe\u{301}|Bo", "say cafe\u{301} Bob "),
            ("say \u{301}x", "run __complete|say|\u{301}x", "say Bob "),
            ("say ''", "run __complete|say|", "say Bob "),
        ]
        for (typed, asked, completed) in cases {
            model.input = typed
            await model.complete()
            #expect(runner.calls.last == asked, "\(typed)")
            #expect(model.input.unicodeScalars.elementsEqual(completed.unicodeScalars), "\(typed) became \(model.input)")
        }
    }

    @Test("Tab asks the binary: __complete with the words and the word being typed; one candidate completes it")
    func completion() async {
        let runner = ScriptedRunner()
        runner.answering(["__complete", "models"], .done(stdout: "list\nlist-all\n"))
        runner.answering(["__complete", "chats", "recall", "--f"], .done(stdout: "--forget\n"))
        let (model, _) = console(runner)
        model.input = "models li"
        await model.complete()
        #expect(runner.calls.last == "run __complete|models|li")
        #expect(model.suggestions == ["list", "list-all"])
        model.accept("list")
        #expect(model.input == "models list " && model.suggestions.isEmpty)
        model.input = "apogee chats recall --f"
        await model.complete()
        #expect(runner.calls.last == "run __complete|chats|recall|--f", "a leading apogee is the console's, never a word")
        #expect(model.input == "apogee chats recall --forget ")
        model.input = "models "
        await model.complete()
        #expect(runner.calls.last == "run __complete|models|", "after a space, the word being typed is empty")
    }

    @Test("a run is the command as typed on the jobs core: stdout streamed, the busy line live, stderr kept, how it ended said")
    func runs() async {
        let job = ScriptedJob()
        let (model, starter) = console(ScriptedRunner(), [job])
        model.input = #"apogee video compose "a brief" --dry-run"#
        model.run()
        #expect(starter.starts == [["video", "compose", "a brief", "--dry-run"]])
        #expect(model.isRunning && model.input.isEmpty)
        #expect(model.lines.map(\.text) == [#"apogee video compose "a brief" --dry-run"#])
        job.say(.said("planned 4 scenes"))
        job.say(.busy("planning scene 2 of 4"))
        #expect(await eventually { model.busy == "planning scene 2 of 4" })
        job.end(stderr: "note: the voice is af_heart")
        #expect(await eventually { !model.isRunning })
        #expect(model.lines.map(\.kind) == [.command, .out, .err, .note])
        #expect(model.lines.map(\.text) == [#"apogee video compose "a brief" --dry-run"#, "planned 4 scenes", "note: the voice is af_heart", "Done."])
        #expect(model.busy == nil && model.history == [#"apogee video compose "a brief" --dry-run"#])
    }

    @Test("a failing run says its status and keeps its stderr; Stop is Ctrl-C's, and says so")
    func failureAndStop() async {
        let failing = ScriptedJob()
        let stopped = ScriptedJob()
        let (model, _) = console(ScriptedRunner(), [failing, stopped])
        model.input = "models info nothing"
        model.run()
        failing.end(status: 1, stderr: "apogee models: no backend or stored weights named 'nothing'")
        #expect(await eventually { !model.isRunning })
        #expect(model.lines.suffix(2).map(\.text) == ["apogee models: no backend or stored weights named 'nothing'", "Exited with status 1."])
        #expect(model.lines.last?.kind == .problem)
        model.input = "models pull Qwen/Qwen3-8B"
        model.run()
        model.input = "check"
        model.run()
        #expect(model.lines.last?.text == "apogee models pull Qwen/Qwen3-8B", "one run at a time")
        await model.stop()
        #expect(await eventually { !model.isRunning })
        #expect(model.lines.last?.text == "Stopped.")
    }

    @Test("the parity table's carve-outs are refused with their reasons and never started")
    func refusals() {
        let (model, starter) = console()
        for line in ["reset", "apogee uninstall --yes", "serve --port 8080"] {
            model.input = line
            model.run()
        }
        #expect(starter.starts.isEmpty)
        #expect(model.lines.filter { $0.kind == .problem }.map(\.text) == [
            "Not run here: deletes the data directory the running app stands on -- run it at a prompt, with the app closed.",
            "Not run here: removes Apogee and the data directory the app drives -- the app never uninstalls the binary it runs; run it at a prompt, with the app closed.",
            "Not run here: a daemon the app would orphan when it quits -- run it at a prompt, or under a service manager.",
        ])
    }

    @Test("an empty line or an open quote runs nothing; with no binary, nothing starts; the arrows recall the history")
    func edges() {
        let (model, starter) = console()
        model.input = "   "
        model.run()
        model.input = #"say "open"#
        model.run()
        #expect(starter.starts.isEmpty && model.lines.map(\.text) == ["A double quote is left open."])
        let missing = ConsoleViewModel(runner: ScriptedRunner(), starter: ScriptedJobs([]))
        missing.input = "models list"
        missing.run()
        #expect(missing.lines.last?.text == "Apogee isn't installed, so there is nothing to run.")
        #expect(missing.recall(1) == "models list" && missing.recall(2) == nil)
    }
}
