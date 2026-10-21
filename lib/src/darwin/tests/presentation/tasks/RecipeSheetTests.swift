import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import Testing

@testable import PresentationTasks

/// The recipes as the binary printed them (tests/business/reads/Fixtures).
private func recorded(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        .appendingPathComponent("business/reads/Fixtures/\(name).json")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// The new-task sheet's recipes: listed from their documents, the one chosen
/// read whole, launched as `task run --recipe`, and its expansion -- the
/// hand-typed run it is -- shown before it runs.
@Suite("RecipeSheet", .timeLimit(.minutes(1)))
@MainActor
struct RecipeSheetTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let goal = "Read the folder docs: list it, then read every file in it. Then write DIGEST.md in the current folder, in Markdown: a short paragraph saying what the folder holds and what it is for, then one line per file -- its path and, in a few words, what it contains. Change nothing else, and say the task is done once DIGEST.md is written.\n"

    func model(_ jobs: ScriptedJobs = ScriptedJobs([])) async -> (TasksViewModel, ScriptedRunner) {
        let runner = ScriptedRunner()
        runner.reading(["task", "list"], #"{"object":"list","data":[],"total":0}"#)
        runner.reading(["task", "recipes"], recorded("task-recipes"))
        runner.reading(["task", "recipes", "show", "digest-folder"], recorded("task-recipe-show"))
        let model = TasksViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        return (model, runner)
    }

    @Test("the sheet lists the recipes; one chosen is read whole, its rounds the sheet's, none of its grants given")
    func choose() async throws {
        let (model, runner) = await model()
        await model.readRecipes()
        #expect(model.recipes.map(\.name) == ["capture-decisions", "changelog-draft", "digest-folder"])
        await model.choose(recipe: "digest-folder")
        #expect(runner.calls.contains("read task recipes show digest-folder"))
        let recipe = try #require(model.form.recipe)
        #expect(recipe.goal != nil)
        #expect(model.form.rounds == 8)
        #expect(model.form.grants.isEmpty)
        #expect(model.form.arguments == nil, "it reads an input, and none is given")
        await model.choose(recipe: nil)
        #expect(model.form.recipe == nil)
    }

    @Test("launched as --recipe with its input; a suggested grant only when ticked; --rounds only when moved")
    func launchArgv() async throws {
        let (model, _) = await model()
        await model.choose(recipe: "digest-folder")
        model.form.input = " docs "
        #expect(model.form.arguments == ["task", "run", "--recipe", "digest-folder", "--input", "docs", "--output-format", "stream-json"])
        #expect(model.form.ungivenGrants == ["write_file"], "suggested, said, not given")
        model.form.grants.insert("write_file")
        model.form.rounds = 4
        model.form.requires = "done"
        model.form.model = "local"
        #expect(model.form.arguments == [
            "task", "run", "--recipe", "digest-folder", "--input", "docs", "--require", "done", "--rounds", "4",
            "--allow", "write_file", "-m", "local", "--output-format", "stream-json",
        ])
        #expect(model.form.ungivenGrants.isEmpty)
    }

    @Test("the expansion is the hand-typed twin, byte for byte: the goal rendered, its checks first, --tools, its rounds, the grants given")
    func expansion() async throws {
        let (model, _) = await model()
        await model.choose(recipe: "digest-folder")
        #expect(model.form.expansion == nil, "nothing to expand until it has its input")
        model.form.input = "docs"
        #expect(model.form.expansion == ["task", "run", Self.goal, "--require-file", "DIGEST.md", "--tools", "--rounds", "8"])
        model.form.grants.insert("write_file")
        model.form.requireFiles = "NOTES.md"
        #expect(model.form.expansion == [
            "task", "run", Self.goal, "--require-file", "DIGEST.md", "--require-file", "NOTES.md", "--tools", "--rounds", "8",
            "--allow", "write_file",
        ])
        #expect(model.form.hasChecks)
    }

    @Test("starting runs the --recipe line on the jobs core, followed live, and the sheet starts fresh")
    func start() async throws {
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let (model, _) = await model(jobs)
        await model.choose(recipe: "digest-folder")
        model.form.input = "docs"
        model.start()
        #expect(jobs.starts.first?.arguments == ["task", "run", "--recipe", "digest-folder", "--input", "docs", "--output-format", "stream-json"])
        #expect(jobs.starts.first?.streaming == true)
        #expect(model.form.recipe == nil)
        job.end()
    }

    @Test("a line as a shell takes it: plain words bare, the rest single-quoted")
    func typed() {
        #expect(NewTaskForm.typed(["task", "run", "--recipe", "digest-folder", "--input", "my docs", "--require", "it's done"])
            == "apogee task run --recipe digest-folder --input 'my docs' --require 'it'\\''s done'")
    }
}
