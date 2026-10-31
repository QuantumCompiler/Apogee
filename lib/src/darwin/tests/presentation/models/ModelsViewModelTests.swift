import BusinessReads
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationModels

@Suite("ModelsViewModel", .timeLimit(.minutes(1)))
@MainActor
struct ModelsViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let list = #"""
    {"object":"list","data":[
     {"type":"model","backend":"claude","backend_type":"anthropic","model":"claude-sonnet-5","roles":"","source":"remote","architecture":"","profile":"","state":"ready","verified":"","section":"cloud","group":"anthropic"},
     {"type":"model","backend":"qwen3-8b-Q4KM","backend_type":"llamacpp","model":"/store/gguf/qwen3-8b-Q4_K_M.gguf","roles":"chat, embedding","source":"store","architecture":"qwen3","profile":"","state":"ready","verified":"sha256 ok","section":"local","group":"gguf","format":"gguf","quant":"Q4_K_M"},
     {"type":"model","backend":"(not configured)","backend_type":"-","model":"Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3","roles":"","source":"huggingface","architecture":"WanPipeline","profile":"-","state":"pipeline","verified":"","section":"local","group":"pipeline","format":"pipeline","components":5,"bytes":28000000000},
     {"type":"model","backend":"qwen-mlx","backend_type":"mlx","model":"Qwen/Qwen3-8B","roles":"","source":"store","architecture":"qwen3","profile":"","state":"ready","verified":"","section":"local","group":"mlx","format":"mlx","window":"32768-token window (the default; trained for 131072)"},
     {"type":"model","backend":"claude-code","backend_type":"claude-cli","model":"","roles":"","source":"","architecture":"","profile":"","state":"ready","verified":"","section":"cloud","group":"claude-cli"}
    ]}
    """#
    static let status = #"{"fields":[{"field":"chat","value":"qwen3-8b-Q4KM"},{"field":"embedding","value":"qwen3-8b-Q4KM   (via models.default)"},{"field":"vision","value":"(unset -- the chat's own backend)"}]}"#

    func connected(_ runner: ScriptedRunner) async -> ModelsViewModel {
        runner.reading(["models", "list"], Self.list)
        runner.reading(["models", "status"], Self.status)
        let model = ModelsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        return model
    }

    @Test("Backends lists the backends alone under the CLI's groups, local first; stored weights are the store's (2h)")
    func listAndGroups() async {
        let model = await connected(ScriptedRunner())
        #expect(model.listing == .read)
        #expect(ModelsViewModel.Tab.allCases.map(\.title) == ["Backends", "Stored weights", "Jobs", "Roles"])
        #expect(model.tab == .backends)
        #expect(model.groups.map(\.title) == ["GGUF", "MLX", "Anthropic", "Claude Code"])
        #expect(model.count(.backends) == 4 && model.count(.store) == nil)
        #expect(model.groups.flatMap(\.rows).map(\.name) == ["qwen3-8b-Q4KM", "qwen-mlx", "claude", "claude-code"])
        #expect(!model.groups.flatMap(\.rows).contains { $0.isStored }, "a pipeline lives under Stored weights")
        #expect(model.groups.flatMap(\.rows).map(\.kind).suffix(2) == [.api, .subscription])
        let pipeline = model.rows.first { $0.format == "pipeline" }!
        #expect(pipeline.components == 5 && pipeline.bytes == 28_000_000_000)
        #expect(pipeline.kind == nil, "a stored pipeline has no backend type: no kind guessed")
    }

    @Test("details are models info of the row's name: the record before 52e, the typed facts with it")
    func details() async {
        let runner = ScriptedRunner()
        let model = await connected(runner)
        runner.reading(["models", "info", "qwen-mlx"], #"{"name":"qwen-mlx","fields":[{"field":"backend","value":"qwen-mlx"},{"field":"path","value":"/Users/someone/.apogee/models/mlx/qwen"}]}"#)
        await model.select(model.rows.first { $0.backend == "qwen-mlx" }!)
        // Then its adapters, an mlx entry taking them (GUI 4i): none listed,
        // so the doctor is not asked.
        #expect(runner.calls.suffix(2) == ["read models info qwen-mlx", "run config get backends.qwen-mlx.adapters"])
        #expect(model.facts.map(\.label) == ["backend", "path"])
        #expect(model.typed.isEmpty, "no typed data: the record stands alone, as the board's fallback")
        #expect(model.path == "/Users/someone/.apogee/models/mlx/qwen")
        runner.reading(["models", "info", "qwen3-8b-Q4KM"], #"""
        {"name":"qwen3-8b-Q4KM","fields":[{"field":"backend","value":"qwen3-8b-Q4KM"}],
         "data":{"format":"gguf","handle":"qwen3-8b/gguf/6d0e12aa","bytes":5000000000,"architecture":"qwen3",
          "window":{"tokens":32768,"source":"default","trained":131072},
          "cache":{"bytes":1000000000,"type":"f16","sliding_positions":0},
          "lineage":[{"kind":"quantized","from":"qwen3-8b/gguf/F16","detail":"Q4_K_M","inferred":false,"missing":false},{"kind":"pulled","from":"Qwen/Qwen3-8B","detail":null,"inferred":true,"missing":false}]}}
        """#)
        await model.select(model.rows.first { $0.backend == "qwen3-8b-Q4KM" }!)
        #expect(model.info?.memory == 6_000_000_000)
        #expect(model.typed.map(\.label) == ["Format", "On disk", "Window", "Cache", "Memory", "Handle"])
        #expect(model.typed[2].value == "32,768 tokens (the default); trained for 131,072")
        #expect(model.lineage.map(\.kind) == ["quantized", "pulled"])
        #expect(model.lineage[1].inferred)
    }

    @Test("a stored set with no cache stated says its memory is not known, never estimates it")
    func memoryUnknown() async {
        let runner = ScriptedRunner()
        let model = await connected(runner)
        runner.reading(["models", "info", "Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3"], #"{"name":"Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3","fields":[],"data":{"format":"pipeline","handle":"Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3","bytes":28000000000,"architecture":"WanPipeline","window":null,"cache":null,"lineage":[]}}"#)
        await model.select(model.rows.first { $0.format == "pipeline" }!)
        #expect(model.info?.memory == nil)
        #expect(model.typed.contains(Field("Memory", "not known: the files state no cache")))
    }

    @Test("delete asks after the CLI's plan, then runs with --yes, --and-backends only when chosen and the plan names a backend, and reads back")
    func delete() async {
        let runner = ScriptedRunner()
        var said: [String] = []
        let model = await connected(runner)
        model.say = { said.append($0) }
        let row = model.rows.first { $0.backend == "qwen-mlx" }!
        await model.select(row)
        await model.askToDelete(row)
        #expect(model.pendingDelete?.row == row)
        #expect(
            runner.calls.filter { $0.hasPrefix("run") && !$0.hasPrefix("run __complete") && !$0.hasPrefix("run config get") }
                == ["run models delete qwen-mlx"],
            "asked first: only the plan read -- and the adapters' list, a read -- nothing changed")
        await model.confirmDelete(andBackends: false)
        let ran = runner.calls.firstIndex(of: "run models delete qwen-mlx --yes")!
        #expect(runner.calls[ran + 1 ... ran + 2] == ["read models list", "read models status"], "read back")
        #expect(said == ["Deleted qwen-mlx"])
        // The scripted plan says nothing, so it names no backend: the choice
        // is not offered, and not passed (BackendsDeleteTests has the CLI's
        // own plans).
        await model.askToDelete(row)
        #expect(!model.offersAndBackends)
        await model.confirmDelete(andBackends: true)
        #expect(!runner.calls.contains { $0.contains("--and-backends") })
    }

    @Test("a local backend's entry is removed on asking, its weights kept: config delete-backend, read back")
    func removeBackend() async {
        let runner = ScriptedRunner()
        let model = await connected(runner)
        let row = model.rows.first { $0.backend == "qwen3-8b-Q4KM" }!
        await model.select(row)
        model.askToRemove(row)
        #expect(model.removing == "qwen3-8b-Q4KM")
        #expect(!runner.calls.contains("run config delete-backend qwen3-8b-Q4KM"), "asked first")
        await model.confirmRemove()
        #expect(runner.calls.contains("run config delete-backend qwen3-8b-Q4KM"))
        #expect(!runner.calls.contains { $0.hasPrefix("run models delete") }, "the weights stay")
        #expect(runner.calls.filter { $0 == "read models list" }.count == 2, "read back")
    }

    @Test("a model renames whole by the name the binary completes, any backend as a backend; repair says what the CLI said")
    func renameAndRepair() async {
        let runner = ScriptedRunner()
        runner.commanding(["__complete", "models", "rename"], .done(stdout: "qwen3-8b\n"))
        var said: [String] = []
        let model = await connected(runner)
        model.say = { said.append($0) }
        let gguf = model.rows.first { $0.backend == "qwen3-8b-Q4KM" }!
        #expect(gguf.renameName == "qwen3-8b" && model.renamesAsModel(gguf))
        model.askToRename(gguf, .model)
        await model.rename(model.renaming!, to: "qwen3-8b-chat")
        #expect(runner.calls.contains("run models rename qwen3-8b qwen3-8b-chat"))
        let mlx = model.rows.first { $0.backend == "qwen-mlx" }!
        #expect(!model.renamesAsModel(mlx), "no name of its the binary completes: renamed as a backend (M2)")
        model.askToRename(mlx, .backend)
        await model.rename(model.renaming!, to: "qwen-mlx-8b")
        #expect(runner.calls.contains("run config rename-backend qwen-mlx qwen-mlx-8b"))
        var checked: [String] = []
        model.checkFiles = { checked.append($0) }
        model.repair(gguf)
        #expect(checked == ["qwen3-8b-Q4KM"], "a check is a job on the Jobs tab, never a one-shot that holds the place")
        #expect(!runner.calls.contains { $0.hasPrefix("run models repair") })
        #expect(said.isEmpty || !said.contains { $0.contains("verified") })
        runner.commanding(["models", "rename"], .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail("apogee models: no model named 'x'")))
        model.askToRename(gguf, .model)
        await model.rename(model.renaming!, to: "other")
        #expect(model.failure == "apogee models: no model named 'x' (exited with status 1)")
    }

    @Test("roles: the CLI's words, the backend each names, moved through its verb and read back")
    func roles() async {
        let runner = ScriptedRunner()
        let model = await connected(runner)
        #expect(model.roles.map(\.role) == ["chat", "embedding", "vision"])
        #expect(model.roles.map(\.backend) == ["qwen3-8b-Q4KM", "qwen3-8b-Q4KM", nil])
        #expect(model.roles.map(\.isSet) == [true, false, false])
        await model.assign(model.roles[2], to: "claude")
        #expect(runner.calls.suffix(4) == ["run config set-default-vision claude", "read models list", "read models status", "run __complete models rename "],
                "read back, and the names models rename takes asked again (M2)")
        await model.assign(model.roles[0], to: "claude")
        #expect(runner.calls.contains("run config set-default claude"), "the chat role's verb is set-default")
        let before = runner.calls.count
        await model.assign(model.roles[0], to: "qwen3-8b-Q4KM")
        #expect(runner.calls.count == before, "already set there: nothing runs")
    }

    @Test("reads again on coming into view; a deleted selection closes; a missing binary and a failed read are said")
    func readBackAndStates() async {
        let runner = ScriptedRunner()
        let model = await connected(runner)
        await model.select(model.rows.first { $0.backend == "claude" }!)
        runner.reading(["models", "list"], #"{"object":"list","data":[]}"#)
        await model.appeared()
        #expect(model.selected == nil)
        #expect(model.emptyText == "Nothing here yet.")
        let missing = ModelsViewModel(runner: ScriptedRunner())
        await missing.connect(binary: nil)
        #expect(missing.emptyText == "Apogee isn't installed, so there are no models to list.")
        let failing = ScriptedRunner()
        failing.reading(["models", "status"], Self.status)
        let failed = ModelsViewModel(runner: failing)
        await failed.connect(binary: Self.binary)
        #expect(failed.emptyText == "The models could not be read: no scripted read for [\"models\", \"list\"]")
    }

    /// The recorded store (tests/business/reads/Fixtures): stored weights of
    /// every kind, one set folded, a configured mock backend.
    static func recorded(_ name: String) -> String {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../../business/reads/Fixtures/models-stored-\(name).json")
        return try! String(contentsOf: url, encoding: .utf8)
    }

    func recordedLibrary(_ runner: ScriptedRunner) async -> ModelsViewModel {
        runner.reading(["models", "list"], Self.recorded("list"))
        runner.reading(["models", "list", "--all"], Self.recorded("list-all"))
        runner.reading(["models", "status"], Self.status)
        let model = ModelsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        return model
    }

    @Test("stored weights are read, each named by its handle, and drawn under Stored weights, never among the backends")
    func storedWeights() async {
        let runner = ScriptedRunner()
        let model = await recordedLibrary(runner)
        #expect(model.rows.filter(\.isStored).map(\.name) == [
            "Llama3.2-1B-Instruct/safetensors/edaba290ce7f", "Qwen3-VL-8B-Instruct/safetensors/63f2634a2019",
            "Gemma4-26B-Instruct/mlx/a88b9f386aff", "Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6", "Kokoro-82M/voice/5f3a2c91d0e4",
        ], "every set its own identity")
        #expect(model.groups.map(\.title) == ["Other"] && model.groups[0].rows.map(\.name) == ["mock-chat"], "Backends draws the backends alone")
        #expect(model.backends == ["mock-chat"], "a role points at a backend, never at stored weights")
        #expect(!runner.calls.contains("read models list --all"), "the fold is the store's to show")
    }

    @Test("stored weights' details, delete and repair go by their handle, rename by their directory; the placeholder is never a name")
    func storedVerbs() async {
        let runner = ScriptedRunner()
        let model = await recordedLibrary(runner)
        runner.reading(["models", "info"], Self.recorded("info-set"))
        let set = model.rows[0]
        await model.select(set)
        #expect(runner.calls.contains("read models info Llama3.2-1B-Instruct/safetensors/edaba290ce7f"))
        #expect(model.selectedRow == set && model.info != nil)
        #expect(model.lineage.map(\.kind) == ["pulled"], "52e's typed data, read by the handle")
        await model.askToDelete(set)
        await model.confirmDelete(andBackends: false)
        #expect(runner.calls.contains("run models delete Llama3.2-1B-Instruct/safetensors/edaba290ce7f --yes"))
        var checked: [String] = []
        model.checkFiles = { checked.append($0) }
        model.repair(model.rows[1])
        #expect(checked == ["Qwen3-VL-8B-Instruct/safetensors/63f2634a2019"], "by its handle")
        await model.rename(.init(row: model.rows[1], kind: .model), to: "qwen-vl")
        #expect(runner.calls.contains("run models rename Qwen3-VL-8B-Instruct qwen-vl"))
        #expect(!runner.calls.contains { $0.contains(ModelRow.unconfigured) })
    }

    @Test("Roles offers an mlx entry over an embedding model to the embedding role, as the CLI's role verbs take any backend; no session opens on it")
    func embeddingEntryForTheRole() async throws {
        // Recorded from the binary built with the CLI's 53e-53m, once `config
        // scan --register` registered a chat, a vision and an embedding set.
        let fixtures = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../business/reads/Fixtures")
        func kinds(_ name: String) -> String {
            (try? String(contentsOf: fixtures.appendingPathComponent("\(name).json"), encoding: .utf8)) ?? ""
        }
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], kinds("store-list-kinds-registered"))
        runner.reading(["models", "status"], kinds("models-status-kinds-registered"))
        let model = ModelsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        #expect(model.backends.sorted() == ["EmbeddingGemma-300M-bf16", "Gemma4-E4B-4bit", "Qwen3-8B-4bit"])
        let role = try #require(model.roles.first { $0.role == "embedding" })
        await model.assign(role, to: "EmbeddingGemma-300M-bf16")
        #expect(runner.calls.contains("run config set-default-embedding EmbeddingGemma-300M-bf16"))

        let embedding = try #require(model.rows.first { $0.backend == "EmbeddingGemma-300M-bf16" })
        #expect(!model.opensSessions(embedding), "an embedding model answers no chat")
        #expect(model.opensSessions(try #require(model.rows.first { $0.backend == "Gemma4-E4B-4bit" })))
        #expect(model.opensSessions(try #require(model.rows.first { $0.backend == "Qwen3-8B-4bit" })))
        #expect(!model.rows.filter(\.isStored).contains { model.opensSessions($0) }, "stored weights open none")
        // A binary before 53e: every MLX backend opens a session, as before.
        let older = await connected(ScriptedRunner())
        #expect(older.rows.filter { $0.group == "mlx" }.allSatisfy { older.opensSessions($0) && $0.mlxKind == nil })
    }
}
