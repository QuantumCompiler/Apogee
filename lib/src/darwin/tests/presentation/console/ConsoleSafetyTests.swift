import BusinessJobs
import BusinessReads
import Foundation
import Testing

@testable import PresentationConsole

/// The console refuses what the parity table refuses, however it is typed
/// (GUI 4g): the command path is read past the binary's root options, as the
/// binary reads it, and matched against the table -- the carve-outs, the
/// doors the app already is, and the `$EDITOR` verbs. A bare `apogee` is told
/// what to type, and Tab edits the line as it was typed.
@Suite("Console safety", .timeLimit(.minutes(1)))
@MainActor
struct ConsoleSafetyTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func console(_ runner: ScriptedRunner = ScriptedRunner(), _ jobs: [ScriptedJob] = []) -> (ConsoleViewModel, ScriptedJobs) {
        let starter = ScriptedJobs(jobs)
        let model = ConsoleViewModel(runner: runner, starter: starter)
        model.connect(binary: Self.binary)
        return (model, starter)
    }

    /// Each refused command, as a person would type it, and the reason the
    /// console gives -- the parity table's words.
    static let refused: [(line: String, reason: String)] = [
        ("reset -y", "deletes the data directory the running app stands on -- run it at a prompt, with the app closed"),
        ("uninstall -y", "removes Apogee and the data directory the app drives -- the app never uninstalls the binary it runs; run it at a prompt, with the app closed"),
        ("serve --port 8080", "a daemon the app would orphan when it quits -- run it at a prompt, or under a service manager"),
        ("chat", "a conversation is a session in the app -- start one with New session (⌘N), or reopen one from Sessions"),
        ("chat -m local --resume 20261022-182803-f9f6", "a conversation is a session in the app -- start one with New session (⌘N), or reopen one from Sessions"),
        ("execute --suite pair", "an execute session is a session in the app -- open one with New execute session (⌥⌘N)"),
        ("tui", "the app is the full-screen shell's counterpart already -- run it at a prompt for the terminal's own"),
        ("agents edit reviewer", "opens your $EDITOR, which needs a terminal the console doesn't have -- run it at a prompt"),
        ("symphonies edit digest", "opens your $EDITOR, which needs a terminal the console doesn't have -- run it at a prompt"),
    ]

    /// Every root option the binary takes before its command (`apogee
    /// --help`): those without a value, the two valued ones with their value
    /// spelled both ways, `--` (the binary still finds a command after it),
    /// and two together.
    static let rootOptions: [String] = [
        "",
        "--release ",
        "--dev ",
        "--test ",
        "--config /Users/someone/apogee.json ",
        "--config=/Users/someone/apogee.json ",
        "--custom /Users/someone/scratch/config/config.json ",
        "--custom=/Users/someone/scratch/config/config.json ",
        "-h ",
        "--help ",
        "-V ",
        "--version ",
        "-- ",
        "--dev --config /Users/someone/apogee.json ",
        #"--config "/Users/someone/my apogee.json" "#,
    ]

    @Test("no carved-out, door or editor command starts, whatever root options precede it; each refusal says its reason")
    func refusalMatrix() {
        var refusals = 0
        for root in Self.rootOptions {
            for (line, reason) in Self.refused {
                for lead in ["", "apogee "] {
                    let typed = lead + root + line
                    let (model, starter) = console()
                    model.input = typed
                    model.run()
                    #expect(starter.starts.isEmpty, "\(typed) started")
                    #expect(model.lines.map(\.kind) == [.command, .problem], "\(typed)")
                    #expect(model.lines.last?.text == "Not run here: \(reason).", "\(typed)")
                    #expect(model.lines.first?.text == "apogee " + root + line, "the refused line is shown as typed")
                    refusals += 1
                }
            }
        }
        #expect(refusals == Self.rootOptions.count * Self.refused.count * 2)
    }

    @Test("their neighbours run: another verb of the family, another family, the word as an argument, a valued option's value")
    func neighboursRun() async {
        let lines = [
            "chats list",
            "agents list",
            "symphonies show digest",
            "symphonies play digest --input edit",
            "--dev models list",
            "models info chat",
            "execute-nothing",
            "--config serve",
            "--config=reset models list",
        ]
        let jobs = lines.map { _ in ScriptedJob() }
        let (model, starter) = console(ScriptedRunner(), jobs)
        for (line, job) in zip(lines, jobs) {
            model.input = line
            model.run()
            job.end()
            #expect(await eventually { !model.isRunning }, "\(line)")
        }
        #expect(starter.starts == lines.map { ConsoleViewModel.arguments($0)! })
        #expect(model.lines.filter { $0.kind == .problem }.isEmpty)
    }

    @Test("a bare apogee is told what to type, never that a quote is open; nothing starts and the line stays")
    func bareApogee() {
        let (model, starter) = console()
        for typed in ["apogee", "apogee ", "  apogee  "] {
            model.input = typed
            model.run()
            #expect(starter.starts.isEmpty)
            #expect(model.lines.last?.text == "Nothing to run: type a command after apogee -- Tab offers them, and --help lists them all.")
            #expect(model.lines.last?.kind == .note)
            #expect(model.input == typed, "the line is left to finish")
        }
        #expect(!model.lines.contains { $0.text == "A quote is left open." })
        #expect(model.history.isEmpty, "nothing was run")
    }

    @Test("Tab replaces the word being typed as it was typed: a quoted or escaped partial is replaced whole and quoted again")
    func completionEditsWhatWasTyped() async {
        let runner = ScriptedRunner()
        runner.answering(["__complete", "models", "info"], .done(stdout: "My Model\n"))
        let (model, _) = console(runner)
        let cases: [(typed: String, completed: String)] = [
            (#"models info "My Mo""#, #"models info "My Model" "#),
            (#"models info My\ Mo"#, #"models info "My Model" "#),
            (#"models info 'My Mo'"#, #"models info "My Model" "#),
            (#"models info "My "Mo"#, #"models info "My Model" "#),
            (#"apogee models info "My Mo""#, #"apogee models info "My Model" "#),
            ("models info My", #"models info "My Model" "#),
        ]
        for (typed, completed) in cases {
            model.input = typed
            await model.complete()
            #expect(runner.calls.last == "run __complete|models|info|" + (typed.hasSuffix("My") ? "My" : "My Mo"), "\(typed)")
            #expect(model.input == completed, "\(typed)")
            #expect(ConsoleViewModel.arguments(model.input)?.suffix(1) == ["My Model"], "\(typed): it runs with the candidate whole")
        }
    }

    @Test("a candidate is quoted as the line splits it: a quote or backslash in it escaped, a plain one bare; several are offered, the one taken replaces the word")
    func candidatesQuoted() async {
        let runner = ScriptedRunner()
        runner.answering(["__complete", "chats", "show"], .done(stdout: "Bob's \"draft\"\nBob\\notes\n"))
        let (model, _) = console(runner)
        model.input = #"chats show "Bob"#
        await model.complete()
        #expect(model.suggestions.isEmpty, "an open quote asks nothing")
        model.input = #"chats show Bo\b"#
        await model.complete()
        #expect(runner.calls.last == "run __complete|chats|show|Bob")
        #expect(model.suggestions == [#"Bob's "draft""#, #"Bob\notes"#])
        model.accept(#"Bob's "draft""#)
        #expect(model.input == #"chats show "Bob's \"draft\"" "#)
        #expect(ConsoleViewModel.arguments(model.input) == ["chats", "show", #"Bob's "draft""#])
        model.input = "chats show 'Bo'"
        model.accept(#"Bob\notes"#)
        #expect(model.input == #"chats show "Bob\\notes" "#)
        #expect(ConsoleViewModel.arguments(model.input) == ["chats", "show", #"Bob\notes"#])
        model.input = "chats show Bo"
        model.accept("Bobby")
        #expect(model.input == "chats show Bobby ")
    }
}
