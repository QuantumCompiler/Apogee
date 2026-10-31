import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationModels

private func recorded(_ name: String, _ ext: String = "json") -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name).\(ext)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// The Backends tab's Delete with its weights says what the CLI says (GUI
/// 4e), as Stored weights' delete does: over the store StoreDeleteTests reads,
/// each Q4_K_M's backend deleted by its name. The branch's binary answered
/// every plan, delete and list by a backend's name byte for byte as by the
/// set's handle, so the `delete-*` recordings stand for both; `models
/// status` and the rename completion are this tab's own (`backends-*`).
@Suite("BackendsDelete", .timeLimit(.minutes(1)))
@MainActor
struct BackendsDeleteTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let llama = "Llama3.2-1B-Instruct-Q4KM"
    static let gemma = "Gemma3-1B-Instruct-Q4KM"

    func models() async -> (ModelsViewModel, ScriptedRunner) {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("delete-list"))
        runner.reading(["models", "status"], recorded("backends-status"))
        runner.commanding(["__complete", "models", "rename"], .done(stdout: recorded("backends-complete-rename", "txt")))
        // The plans first, then the deletes, which a longer prefix picks out.
        runner.commanding(["models", "delete", Self.llama], .done(stdout: recorded("delete-plan-backend", "txt")))
        runner.commanding(["models", "delete", Self.gemma], .done(stdout: recorded("delete-plan-gemma", "txt")))
        runner.commanding(["models", "delete", Self.gemma, "--yes"], .done(stdout: recorded("delete-kept", "txt")))
        runner.commanding(["models", "delete", Self.llama, "--yes", "--and-backends"], .done(stdout: recorded("delete-and-backends", "txt")))
        let model = ModelsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        return (model, runner)
    }

    @Test("Delete with its weights reads the CLI's own plan before asking, and deletes nothing until the yes")
    func readsThePlanFirst() async throws {
        let (model, runner) = await models()
        #expect(model.groups.flatMap(\.rows).map(\.backend) == [Self.gemma, Self.llama])
        let row = try #require(model.rows.first { $0.backend == Self.llama })
        await model.askToDelete(row)
        #expect(runner.calls.contains("run models delete \(Self.llama)"), "models delete without --yes: the CLI's plan")
        #expect(!runner.calls.contains { $0.hasPrefix("run models delete") && $0.contains("--yes") }, "nothing deleted before the yes")
        #expect(model.pendingDelete?.row == row)
    }

    @Test("the confirm names the backends the CLI's plan says use its files, and offers to take them")
    func confirmNamesTheBackends() async throws {
        let (model, runner) = await models()
        let row = try #require(model.rows.first { $0.backend == Self.llama })
        await model.askToDelete(row)
        let plan = try #require(model.pendingDelete?.plan)
        #expect(plan.backends == [Self.llama])
        #expect(plan.lines == [
            "will remove:",
            "  /Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/5703795782eb",
            "",
            "backend 'Llama3.2-1B-Instruct-Q4KM' points into this and will stop working until it is repointed.",
            "the backend entry stays unless you say -- --and-backends removes it with the weights.",
        ])
        #expect(model.offersAndBackends)
        #expect(plan.question(running: "apogee models delete \(Self.llama) --yes").hasSuffix(
            "removes it with the weights.\n\napogee models delete Llama3.2-1B-Instruct-Q4KM --yes -- this can't be undone."))
        #expect(!runner.calls.contains { $0.hasPrefix("run models delete") && $0.contains("--yes") })
        // A plan naming none -- no and-the-backends choice -- and a refused
        // plan are the shared reading's, pinned by ModelsViewModelTests'
        // delete and StoreDeleteTests over recordings of their own.
    }

    @Test("the CLI's words after the delete are kept, on both paths: the backend kept with its doors, or removed")
    func narrationKept() async throws {
        let (model, runner) = await models()
        var said: [String] = []
        model.say = { said.append($0) }
        let gemma = try #require(model.rows.first { $0.backend == Self.gemma })
        await model.askToDelete(gemma)
        runner.reading(["models", "list"], recorded("delete-list-after-kept"))
        await model.confirmDelete(andBackends: false)
        #expect(runner.calls.contains("run models delete \(Self.gemma) --yes"))
        #expect(said == ["Deleted \(Self.gemma)"])
        let kept = try #require(model.narration, "the CLI's warning and doors, kept after the delete")
        #expect(kept.title == "Deleted \(Self.gemma)")
        #expect(kept.lines.joined(separator: "\n") == recorded("delete-kept", "txt").trimmingCharacters(in: .newlines))
        #expect(kept.lines.contains(
            "kept backend 'Gemma3-1B-Instruct-Q4KM', which points at nothing now -- repoint it: apogee config set-backend Gemma3-1B-Instruct-Q4KM --model-path <file>; or remove it: apogee config delete-backend Gemma3-1B-Instruct-Q4KM"))

        let llama = try #require(model.rows.first { $0.backend == Self.llama })
        await model.askToDelete(llama)
        runner.reading(["models", "list"], recorded("delete-list-after-removed"))
        await model.confirmDelete(andBackends: true)
        #expect(runner.calls.contains("run models delete \(Self.llama) --yes --and-backends"))
        let removed = try #require(model.narration)
        #expect(removed.title == "Deleted \(Self.llama)")
        #expect(removed.lines.contains("removed backend 'Llama3.2-1B-Instruct-Q4KM' from /Users/someone/.apogee/config/config.json"))
        #expect(!model.rows.contains { $0.backend == Self.llama }, "read back")
        model.dismissNarration()
        #expect(model.narration == nil)
    }
}
