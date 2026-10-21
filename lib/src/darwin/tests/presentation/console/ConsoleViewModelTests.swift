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
        #expect(starter.starts.isEmpty && model.lines.map(\.text) == ["A quote is left open."])
        let missing = ConsoleViewModel(runner: ScriptedRunner(), starter: ScriptedJobs([]))
        missing.input = "models list"
        missing.run()
        #expect(missing.lines.last?.text == "Apogee isn't installed, so there is nothing to run.")
        #expect(missing.recall(1) == "models list" && missing.recall(2) == nil)
    }
}
