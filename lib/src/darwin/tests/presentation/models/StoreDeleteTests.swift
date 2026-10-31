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

/// Stored weights' delete says what the CLI says (GUI 4e): the backends its
/// own plan names before asking, and its warnings and doors after -- over a
/// store recorded from the branch's binary (`delete-*` in
/// tests/business/reads/Fixtures): Llama's and Gemma's F16 GGUFs, each
/// quantized, each Q4_K_M registered as `<model>-Q4KM`; each plan
/// `models delete` printed without `--yes`, stdin closed; and each delete.
@Suite("StoreDelete", .timeLimit(.minutes(1)))
@MainActor
struct StoreDeleteTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let llamaF16 = "Llama3.2-1B-Instruct/gguf/dfed3eebdfdb"
    static let gemmaF16 = "Gemma3-1B-Instruct/gguf/cce9d2f99787"
    static let llamaQ4 = "Llama3.2-1B-Instruct/gguf/5703795782eb"
    static let gemmaQ4 = "Gemma3-1B-Instruct/gguf/09f55af982ed"
    static let llamaPath = "/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/5703795782eb/Llama3.2-1B-Instruct-Q4_K_M.gguf"
    static let gemmaPath = "/Users/someone/.apogee/models/Gemma3-1B-Instruct/gguf/09f55af982ed/Gemma3-1B-Instruct-Q4_K_M.gguf"

    func store() async -> (StoreViewModel, ScriptedRunner) {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("delete-list"))
        runner.reading(["models", "list", "--all"], recorded("delete-list-all"))
        runner.reading(["models", "info", "Llama3.2-1B-Instruct-Q4KM"], recorded("delete-info-backend-llama"))
        runner.reading(["models", "info", "Gemma3-1B-Instruct-Q4KM"], recorded("delete-info-backend-gemma"))
        runner.reading(["models", "info", Self.llamaPath], recorded("delete-info-path-llama"))
        runner.reading(["models", "info", Self.gemmaPath], recorded("delete-info-path-gemma"))
        runner.reading(["models", "info", Self.llamaQ4], recorded("delete-info-llama-q4"))
        runner.reading(["models", "info", Self.gemmaQ4], recorded("delete-info-gemma-q4"))
        runner.reading(["models", "info", Self.llamaF16], recorded("delete-info-llama-f16"))
        runner.reading(["models", "info", Self.gemmaF16], recorded("delete-info-gemma-f16"))
        // The plans first, then the deletes, which a longer prefix picks out.
        runner.commanding(["models", "delete", Self.llamaQ4], .done(stdout: recorded("delete-plan-backend", "txt")))
        runner.commanding(["models", "delete", Self.gemmaQ4], .done(stdout: recorded("delete-plan-gemma", "txt")))
        runner.commanding(["models", "delete", Self.llamaF16], .done(stdout: recorded("delete-plan-none", "txt")))
        runner.commanding(["models", "delete", "Llama3.2-1B-Instruct"], .done(stdout: recorded("delete-plan-model", "txt")))
        runner.commanding(["models", "delete", Self.gemmaQ4, "--yes"], .done(stdout: recorded("delete-kept", "txt")))
        runner.commanding(["models", "delete", Self.llamaQ4, "--yes", "--and-backends"], .done(stdout: recorded("delete-and-backends", "txt")))
        runner.commanding(["models", "delete", Self.llamaF16, "--yes"], .done(stdout: recorded("delete-none", "txt")))
        let store = StoreViewModel(runner: runner)
        await store.connect(binary: Self.binary)
        await store.refresh()
        return (store, runner)
    }

    @Test("a delete reads the CLI's own plan before asking, and deletes nothing until the yes")
    func deleteReadsThePlanFirst() async throws {
        let (store, runner) = await store()
        #expect(store.sets.map(\.handle).sorted() == [Self.gemmaF16, Self.gemmaQ4, Self.llamaF16, Self.llamaQ4].sorted())
        let q4 = try #require(store.sets.first { $0.handle == Self.llamaQ4 })
        #expect(q4.backend == "Llama3.2-1B-Instruct-Q4KM")
        await store.askToDelete(q4)
        #expect(runner.calls.contains("run models delete \(Self.llamaQ4)"), "models delete without --yes: the CLI's plan")
        #expect(!runner.calls.contains { $0.hasPrefix("run models delete") && $0.contains("--yes") }, "nothing deleted before the yes")
        #expect(store.pending != nil)
    }

    @Test("the confirm names the backends the CLI's plan says point in; the and-the-backends choice only where one does")
    func confirmNamesTheBackends() async throws {
        let (store, runner) = await store()
        let q4 = try #require(store.sets.first { $0.handle == Self.llamaQ4 })
        await store.askToDelete(q4)
        guard case .deleteSet(let asked, let plan)? = store.pending else {
            Issue.record("not asked: \(String(describing: store.pending))")
            return
        }
        #expect(asked.handle == Self.llamaQ4)
        #expect(plan.backends == ["Llama3.2-1B-Instruct-Q4KM"])
        #expect(plan.lines == [
            "will remove:",
            "  /Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/5703795782eb",
            "",
            "backend 'Llama3.2-1B-Instruct-Q4KM' points into this and will stop working until it is repointed.",
            "the backend entry stays unless you say -- --and-backends removes it with the weights.",
        ], "the plan as the CLI printed it, its re-run line left to the buttons")
        #expect(store.offersAndBackends)
        store.dismissPending()

        let f16 = try #require(store.sets.first { $0.handle == Self.llamaF16 })
        await store.askToDelete(f16)
        guard case .deleteSet(_, let none)? = store.pending else {
            Issue.record("not asked")
            return
        }
        #expect(none.backends.isEmpty && !store.offersAndBackends, "nothing points in: no and-the-backends choice")
        store.dismissPending()

        await store.askToDeleteModel("Llama3.2-1B-Instruct")
        guard case .deleteModel(let model, let whole)? = store.pending else {
            Issue.record("not asked")
            return
        }
        #expect(model == "Llama3.2-1B-Instruct" && whole.backends == ["Llama3.2-1B-Instruct-Q4KM"] && store.offersAndBackends)
        #expect(whole.lines.filter { $0.hasPrefix("  /Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/") }.count == 2, "both its sets")
        store.dismissPending()
        #expect(!runner.calls.contains { $0.hasPrefix("run models delete") && $0.contains("--yes") }, "nothing deleted by asking")

        // A plan the CLI refuses: its words, and nothing asked.
        let missing = StoredSet(
            handle: "Nothing-Here/gguf/000000000000", format: "gguf", precision: nil, cliState: "ok", backend: nil,
            consumed: false, note: nil, kind: nil)
        runner.commanding(
            ["models", "delete", missing.handle],
            .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail(recorded("delete-plan-missing", "err"))))
        await store.askToDelete(missing)
        #expect(store.pending == nil)
        #expect(store.failure?.hasPrefix("apogee models: nothing at /Users/someone/.apogee/models/Nothing-Here/gguf/000000000000") == true)
    }

    @Test("a delete's warnings and doors are drawn after it, on both paths; one that strands nothing says only its toast")
    func narrationDrawn() async throws {
        let (store, runner) = await store()
        var said: [String] = []
        store.say = { said.append($0) }
        let gemma = try #require(store.sets.first { $0.handle == Self.gemmaQ4 })
        await store.askToDelete(gemma)
        // The store as the binary read it back after the delete: the backend
        // kept, pointing at nothing. (`models list` before `--all`: the
        // longer prefix is answered first.)
        runner.reading(["models", "list"], recorded("delete-list-after-kept"))
        runner.reading(["models", "list", "--all"], recorded("delete-list-all-after-kept"))
        runner.reading(["models", "info", "Gemma3-1B-Instruct-Q4KM"], recorded("delete-info-backend-gemma-after-kept"))
        runner.reading(
            ["models", "info", Self.gemmaPath],
            outcome: .failed(
                exit: ChildExit(reason: .exited, status: 1),
                stderrTail: tail("apogee models: no backend or stored weights named '\(Self.gemmaPath)'")))
        await store.confirm()
        #expect(runner.calls.contains("run models delete \(Self.gemmaQ4) --yes"))
        #expect(said == ["Deleted \(Self.gemmaQ4)"])
        let kept = try #require(store.narration, "the CLI's words, kept after the delete")
        #expect(kept.title == "Deleted \(Self.gemmaQ4)")
        #expect(kept.lines.joined(separator: "\n") == recorded("delete-kept", "txt").trimmingCharacters(in: .newlines), "every line, as printed")
        #expect(kept.lines.contains(
            "kept backend 'Gemma3-1B-Instruct-Q4KM', which points at nothing now -- repoint it: apogee config set-backend Gemma3-1B-Instruct-Q4KM --model-path <file>; or remove it: apogee config delete-backend Gemma3-1B-Instruct-Q4KM"),
            "the two doors")
        #expect(!store.sets.contains { $0.handle == Self.gemmaQ4 }, "read back")

        // With its backends: the removal, as the CLI said it.
        let llama = try #require(store.sets.first { $0.handle == Self.llamaQ4 })
        await store.askToDelete(llama)
        store.andBackends = true
        runner.reading(["models", "list"], recorded("delete-list-after-removed"))
        runner.reading(["models", "list", "--all"], recorded("delete-list-all-after-removed"))
        await store.confirm()
        #expect(runner.calls.contains("run models delete \(Self.llamaQ4) --yes --and-backends"))
        let removed = try #require(store.narration)
        #expect(removed.title == "Deleted \(Self.llamaQ4)")
        #expect(removed.lines.contains("--and-backends: its entry goes too, through the config editor."))
        #expect(removed.lines.contains("removed backend 'Llama3.2-1B-Instruct-Q4KM' from /Users/someone/.apogee/config/config.json"))
        store.dismissNarration()
        #expect(store.narration == nil)

        // Nothing pointed in: the toast alone.
        let f16 = try #require(store.sets.first { $0.handle == Self.llamaF16 })
        await store.askToDelete(f16)
        await store.confirm()
        #expect(runner.calls.contains("run models delete \(Self.llamaF16) --yes"))
        #expect(store.narration == nil, "it said no more than what it removed")
        #expect(said.last == "Deleted \(Self.llamaF16)")
    }
}
