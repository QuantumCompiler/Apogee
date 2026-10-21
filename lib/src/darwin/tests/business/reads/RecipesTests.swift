import BusinessReads
import DataProtocol
import Foundation
import Testing

private func document(_ name: String) -> ReadDocument {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name).json")
    return ReadDocument(text: try! String(contentsOf: url, encoding: .utf8))!
}

/// Recorded from the installed binary over a scratch home: the three seeded
/// recipes, and `digest-folder` shown whole.
@Suite("Recipes")
struct RecipesTests {
    @Test("the list: each recipe's input, checks, tools, suggested grants and rounds, as the CLI says them")
    func list() throws {
        let recipes = TaskRecipe.list(from: document("task-recipes"))
        #expect(recipes.map(\.name) == ["capture-decisions", "changelog-draft", "digest-folder"])
        let capture = try #require(recipes.first)
        #expect(capture.allow == ["run_command", "write_file"])
        #expect(capture.require == ["Captured:"])
        #expect(capture.tools && capture.takesInput && capture.source == "seeded")
        #expect(capture.goal == nil, "the list carries no goal; show does")
        let changelog = recipes[1]
        #expect(!changelog.tools && changelog.allow.isEmpty && changelog.require == ["## Unreleased"])
        #expect(recipes[2].requireFiles == ["DIGEST.md"] && recipes[2].rounds == 8)
    }

    @Test("show: the goal's template, rendered with the input as the CLI renders it")
    func show() throws {
        let recipe = try #require(TaskRecipe(document: document("task-recipe-show")))
        #expect(recipe.name == "digest-folder")
        let goal = try #require(recipe.goal)
        #expect(goal.hasPrefix("Read the folder {{input}}: list it"))
        let rendered = try #require(recipe.renderedGoal(input: "docs"))
        // The very goal `task run --recipe digest-folder --input docs` wrote to
        // its ledger, read back from the binary (2026-10-20).
        #expect(rendered == "Read the folder docs: list it, then read every file in it. Then write DIGEST.md in the current folder, in Markdown: a short paragraph saying what the folder holds and what it is for, then one line per file -- its path and, in a few words, what it contains. Change nothing else, and say the task is done once DIGEST.md is written.\n")
        #expect(TaskRecipe(document: document("task-recipes")) == nil, "a list is no recipe")
    }

    @Test("rendering: every {{...}} is the input, braces in the input stay text")
    func rendering() throws {
        let value = JSONValue.parse(#"{"name":"r","input":"x","goal":"A {{input}} and {{ input }}."}"#)!
        let recipe = try #require(TaskRecipe(value))
        #expect(recipe.renderedGoal(input: "{{b}}") == "A {{b}} and {{b}}.")
    }
}
