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

/// GUI M16: the lines the CLI's M33 made its config verbs say -- `models.default
/// set to '<name>' (none was set)` when an add sets the default, and
/// `models.<field> cleared: it named '<name>'` when a delete clears a role --
/// are shown in the Models place's narrations (4e's card), never dropped for
/// the app's own toast. Recorded from the branch binary (schema 2026-10-26)
/// over M5's tiny fake MLX directories, the home written `/Users/someone`;
/// the `-older` words from the installed binary (schema 2026-10-25) over
/// homes of their own:
/// - `add-*`: the store with nothing registered and no default; Qwen3
///   registered from Stored weights, then the E4B (`add-backend-second`);
/// - `register-all-default`: `config scan --register` over the same store;
/// - `remove-*`: every base registered, the 12B the default and Qwen3 the
///   utility role, then `config delete-backend Gemma4-12B-4bit`;
/// - `delete-roles-*`: Qwen3 the default and the utility role, its weights
///   deleted with `--and-backends`;
/// - `role-refused`: the embedding role pointed at Qwen3, a chat model.
@Suite("Role lines narrated (GUI M16)", .timeLimit(.minutes(1)))
@MainActor
struct RoleLinesNarratedTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let qwenPath = "/Users/someone/.apogee/models/Qwen3-8B/mlx/1a2b3c4d5e6f"

    func lines(_ text: String) -> [String] {
        text.split(separator: "\n", omittingEmptySubsequences: false).map(String.init).reversed()
            .drop { $0.isEmpty }.reversed()
    }

    func backends(list: String, status: String) async -> (ModelsViewModel, ScriptedRunner) {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded(list))
        runner.reading(["models", "status"], recorded(status))
        let model = ModelsViewModel(runner: runner)
        model.vocabulary = { "2026-10-26" }
        await model.connect(binary: Self.binary)
        return (model, runner)
    }

    // MARK: - The Backends tab

    @Test("removing the backend the default named says the role it cleared: the CLI's two lines kept in the narration, the toast as before")
    func removeSaysTheRoleCleared() async throws {
        let (model, runner) = await backends(list: "remove-list", status: "remove-status")
        var said: [String] = []
        model.say = { said.append($0) }
        let row = try #require(model.rows.first { $0.backend == "Gemma4-12B-4bit" })
        model.askToRemove(row)
        runner.commanding(["config", "delete-backend"], .done(stdout: recorded("delete-backend-clears", "txt")))
        runner.reading(["models", "list"], recorded("remove-list-after"))
        runner.reading(["models", "status"], recorded("remove-status-after"))
        await model.confirmRemove()
        #expect(runner.calls.contains("run config delete-backend Gemma4-12B-4bit"))
        let narration = try #require(model.narration, "the cleared role, shown")
        #expect(narration.title == "Removed the backend Gemma4-12B-4bit")
        #expect(narration.lines == [
            "removed backend 'Gemma4-12B-4bit' from /Users/someone/.apogee/config/config.json",
            "models.default cleared: it named 'Gemma4-12B-4bit'",
        ])
        #expect(said == ["Removed the backend Gemma4-12B-4bit"])
        #expect(model.roles.first { $0.role == "chat" }?.isSet == false, "read back: the chat role now unset")
    }

    @Test("an older binary's removal says only what it removed: no narration, the toast as today")
    func olderRemoveAsToday() async throws {
        let (model, runner) = await backends(list: "remove-list", status: "remove-status")
        var said: [String] = []
        model.say = { said.append($0) }
        model.askToRemove(try #require(model.rows.first { $0.backend == "Gemma4-12B-4bit" }))
        runner.commanding(["config", "delete-backend"], .done(stdout: recorded("delete-backend-older", "txt")))
        await model.confirmRemove()
        #expect(model.narration == nil)
        #expect(said == ["Removed the backend Gemma4-12B-4bit"])
    }

    @Test("Delete with its weights and its backend says every role it cleared, in 4e's narration")
    func deleteWithBackendsSaysTheRoles() async throws {
        let (model, runner) = await backends(list: "delete-roles-list", status: "delete-roles-status")
        runner.commanding(["models", "delete", "Qwen3-8B-4bit"], .done(stdout: recorded("delete-roles-plan", "txt")))
        runner.commanding(["models", "delete", "Qwen3-8B-4bit", "--yes"], .done(stdout: recorded("delete-roles-and-backends", "txt")))
        await model.askToDelete(try #require(model.rows.first { $0.backend == "Qwen3-8B-4bit" }))
        #expect(model.offersAndBackends, "the plan names the backend")
        await model.confirmDelete(andBackends: true)
        #expect(runner.calls.contains("run models delete Qwen3-8B-4bit --yes --and-backends"))
        let narration = try #require(model.narration)
        #expect(narration.lines == lines(recorded("delete-roles-and-backends", "txt")))
        #expect(narration.lines.suffix(2) == [
            "models.default cleared: it named 'Qwen3-8B-4bit'", "models.default_utility cleared: it named 'Qwen3-8B-4bit'",
        ])
    }

    @Test("a role the CLI refuses an entry of another kind is said in its words")
    func roleRefusedInItsWords() async throws {
        let (model, runner) = await backends(list: "rows-drafts-list", status: "rows-drafts-status")
        var refused = BoundedTail()
        refused.append(Data(recorded("role-refused", "err").utf8))
        runner.commanding(["config", "set-default-embedding"], .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: refused))
        let embedding = try #require(model.roles.first { $0.role == "embedding" })
        await model.assign(embedding, to: "Qwen3-8B-4bit")
        #expect(runner.calls.contains("run config set-default-embedding Qwen3-8B-4bit"))
        #expect(model.failure?.hasPrefix("apogee config: backend 'Qwen3-8B-4bit' cannot take a role:") == true)
        #expect(model.failure?.contains("point the chat role at it ('apogee config set-default Qwen3-8B-4bit') and the embedding role at a backend that embeds") == true)
    }

    // MARK: - Stored weights

    func store(after: Bool = false) async -> (StoreViewModel, ScriptedRunner) {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("add-list"))
        runner.reading(["models", "list", "--all"], recorded("add-list-all"))
        for (model, handle) in [
            ("Qwen3-8B", "Qwen3-8B/mlx/1a2b3c4d5e6f"), ("Gemma4-E4B", "Gemma4-E4B/mlx/2b3c4d5e6f70"),
            ("Gemma4-12B", "Gemma4-12B/mlx/7a8b9c0d1e2f"), ("Gemma4-E4B-Assistant", "Gemma4-E4B-Assistant/mlx/4d5e6f708192"),
            ("Gemma4-12B-Assistant", "Gemma4-12B-Assistant/mlx/8192a3b4c5d6"),
        ] {
            runner.reading(["models", "info", handle], recorded("add-info-\(model)"))
        }
        runner.commanding(["config", "scan"], .done(stdout: recorded("add-scan", "txt")))
        let store = StoreViewModel(runner: runner)
        await store.connect(binary: Self.binary)
        await store.refresh()
        return (store, runner)
    }

    /// The store as the binary read it after Qwen3 was registered.
    func registered(_ runner: ScriptedRunner) {
        runner.reading(["models", "list"], recorded("add-list-after"))
        runner.reading(["models", "list", "--all"], recorded("add-list-all-after"))
        runner.reading(["models", "info", "Qwen3-8B-4bit"], recorded("add-info-backend-after"))
        runner.reading(["models", "info", Self.qwenPath], recorded("add-info-path-after"))
    }

    @Test("Register on a store with no default says the default it set: the CLI's two lines kept, the toast as before")
    func registerSaysTheDefaultSet() async throws {
        let (store, runner) = await store()
        var said: [String] = []
        store.say = { said.append($0) }
        let qwen = try #require(store.sets.first { $0.handle == "Qwen3-8B/mlx/1a2b3c4d5e6f" })
        #expect(await eventually { store.infos[qwen.handle] != nil })
        await store.askToRegister(qwen)
        #expect(store.register?.arguments == ["config", "add-backend", "Qwen3-8B-4bit", "--type", "mlx", "--model-path", Self.qwenPath])
        runner.commanding(["config", "add-backend"], .done(stdout: recorded("add-backend-default", "txt")))
        registered(runner)
        await store.registerIt()
        let narration = try #require(store.narration, "the default it set, shown")
        #expect(narration.title == "Registered Qwen3-8B-4bit")
        #expect(narration.lines == [
            "added backend 'Qwen3-8B-4bit' to /Users/someone/.apogee/config/config.json",
            "models.default set to 'Qwen3-8B-4bit' (none was set)",
        ])
        #expect(said == ["Registered Qwen3-8B-4bit"])
    }

    @Test("Register that sets no role -- a default already set, or a binary before M33 -- says only what it added, as today")
    func registerSaysNoMore() async throws {
        for words in ["add-backend-second", "add-backend-older"] {
            let (store, runner) = await store()
            let qwen = try #require(store.sets.first { $0.handle == "Qwen3-8B/mlx/1a2b3c4d5e6f" })
            #expect(await eventually { store.infos[qwen.handle] != nil })
            await store.askToRegister(qwen)
            runner.commanding(["config", "add-backend"], .done(stdout: recorded(words, "txt")))
            await store.registerIt()
            #expect(store.narration == nil, "\(words)")
        }
    }

    @Test("Register all says the default it set among what it registered: the scan's registration kept, its report already shown in the question")
    func registerAllSaysTheDefaultSet() async throws {
        let (store, runner) = await store()
        var said: [String] = []
        store.say = { said.append($0) }
        await store.askToRegisterAll()
        runner.commanding(["config", "scan", "--register"], .done(stdout: recorded("register-all-default", "txt")))
        await store.confirm()
        let narration = try #require(store.narration, "the default it set, shown")
        #expect(narration.lines == [
            "added backend 'Gemma4-12B-4bit'",
            "models.default set to 'Gemma4-12B-4bit' (none was set)",
            "added backend 'Gemma4-E4B-4bit'",
            "added backend 'Qwen3-8B-4bit'",
            "registered 3 backend(s) in /Users/someone/.apogee/config/config.json",
        ])
        #expect(said == ["registered 3 backend(s) in /Users/someone/.apogee/config/config.json"], "the toast as before")
    }

    @Test("removing a set's backend from Stored weights says the role it cleared too")
    func storeRemoveSaysTheRoleCleared() async throws {
        let (store, runner) = await store()
        store.askToRemoveBackend("Gemma4-12B-4bit")
        runner.commanding(["config", "delete-backend"], .done(stdout: recorded("delete-backend-clears", "txt")))
        await store.confirm()
        #expect(runner.calls.contains("run config delete-backend Gemma4-12B-4bit"))
        #expect(store.narration?.lines.last == "models.default cleared: it named 'Gemma4-12B-4bit'")
    }
}
