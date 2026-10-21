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
     {"type":"model","backend":"wan-1.3b","backend_type":"","model":"Wan-AI/Wan2.1-T2V-1.3B-Diffusers","roles":"","source":"store","architecture":"WanPipeline","profile":"","state":"pipeline","verified":"","section":"local","group":"pipeline","format":"pipeline","components":5,"bytes":28000000000},
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

    @Test("the library lists every kind under the CLI's groups, local first, and the tabs split it")
    func listAndGroups() async {
        let model = await connected(ScriptedRunner())
        #expect(model.listing == .read)
        #expect(model.groups.map(\.title) == ["GGUF", "MLX", "Pipelines", "Anthropic", "Claude Code"])
        #expect(model.count(.all) == 5 && model.count(.local) == 3 && model.count(.cloud) == 2)
        model.tab = .local
        #expect(model.groups.flatMap(\.rows).map(\.backend) == ["qwen3-8b-Q4KM", "qwen-mlx", "wan-1.3b"])
        model.tab = .cloud
        #expect(model.groups.flatMap(\.rows).map(\.kind) == [.api, .subscription])
        let pipeline = model.rows.first { $0.backend == "wan-1.3b" }!
        #expect(pipeline.components == 5 && pipeline.bytes == 28_000_000_000)
        #expect(pipeline.kind == nil, "a stored pipeline has no backend type: no kind guessed")
    }

    @Test("details are models info of the row's name: the record before 52e, the typed facts with it")
    func details() async {
        let runner = ScriptedRunner()
        let model = await connected(runner)
        runner.reading(["models", "info", "qwen-mlx"], #"{"name":"qwen-mlx","fields":[{"field":"backend","value":"qwen-mlx"},{"field":"path","value":"/Users/someone/.apogee/models/mlx/qwen"}]}"#)
        await model.select(model.rows.first { $0.backend == "qwen-mlx" }!)
        #expect(runner.calls.last == "read models info qwen-mlx")
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
        runner.reading(["models", "info", "wan-1.3b"], #"{"name":"wan-1.3b","fields":[],"data":{"format":"pipeline","handle":"wan/pipeline/1","bytes":28000000000,"architecture":"WanPipeline","window":null,"cache":null,"lineage":[]}}"#)
        await model.select(model.rows.first { $0.backend == "wan-1.3b" }!)
        #expect(model.info?.memory == nil)
        #expect(model.typed.contains(Field("Memory", "not known: the files state no cache")))
    }

    @Test("delete asks, then runs with --yes, --and-backends only when chosen, and reads back")
    func delete() async {
        let runner = ScriptedRunner()
        var said: [String] = []
        let model = await connected(runner)
        model.say = { said.append($0) }
        let row = model.rows.first { $0.backend == "qwen-mlx" }!
        await model.select(row)
        model.askToDelete(row)
        #expect(model.pendingDelete == .init(row: row))
        #expect(!runner.calls.contains { $0.hasPrefix("run") })
        await model.confirmDelete(andBackends: false)
        let ran = runner.calls.firstIndex(of: "run models delete qwen-mlx --yes")!
        #expect(runner.calls[ran + 1 ... ran + 2] == ["read models list", "read models status"], "read back")
        #expect(said == ["Deleted qwen-mlx"])
        model.askToDelete(row)
        await model.confirmDelete(andBackends: true)
        #expect(runner.calls.contains("run models delete qwen-mlx --yes --and-backends"))
    }

    @Test("rename takes the model's name without its level; repair says what the CLI said")
    func renameAndRepair() async {
        let runner = ScriptedRunner()
        var said: [String] = []
        let model = await connected(runner)
        model.say = { said.append($0) }
        let gguf = model.rows.first { $0.backend == "qwen3-8b-Q4KM" }!
        #expect(gguf.renameName == "qwen3-8b")
        await model.rename(gguf, to: "qwen3-8b-chat")
        #expect(runner.calls.contains("run models rename qwen3-8b qwen3-8b-chat"))
        #expect(model.rows.first { $0.backend == "qwen-mlx" }!.renameName == "qwen-mlx")
        runner.commanding(["models", "repair"], .done(stdout: "qwen3-8b-Q4KM: 1 file verified against its record\n"))
        await model.repair(gguf)
        #expect(said.last == "qwen3-8b-Q4KM: 1 file verified against its record")
        runner.commanding(["models", "rename"], .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail("apogee models: no model named 'x'")))
        await model.rename(gguf, to: "other")
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
        #expect(runner.calls.suffix(3) == ["run config set-default-vision claude", "read models list", "read models status"])
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
}
