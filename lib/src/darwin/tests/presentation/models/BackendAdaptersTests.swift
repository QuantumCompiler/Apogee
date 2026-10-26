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

/// GUI 4i: a backend's adapters on the Models place -- listed from `config
/// get` (the CLI's 55g), their pins from `check` (53c), edited whole through
/// `config set-backend --adapters` (53a) and read back -- over the documents
/// recorded from the branch's binary in a sandbox: `based` applying its
/// promoted adapter and a hand-added one at 0.5, `q4` one whose base was
/// rewritten under it, `tuned` none.
@Suite("A backend's adapters", .timeLimit(.minutes(1)))
@MainActor
struct BackendAdaptersTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func library() async -> (ModelsViewModel, ScriptedRunner) {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("models-list-adapters"))
        runner.reading(["models", "status"], #"{"fields":[]}"#)
        runner.reading(["models", "info", "based"], recorded("models-info-adapters-based"))
        runner.reading(["models", "info", "q4"], recorded("models-info-adapters-q4"))
        runner.reading(["models", "info", "tuned"], #"{"name":"tuned","fields":[]}"#)
        runner.reading(["check"], recorded("check-adapters"))
        runner.commanding(["config", "get", "backends.based.adapters"], .done(stdout: recorded("config-get-adapters", "txt")))
        runner.commanding(["config", "get", "backends.q4.adapters"], .done(stdout: recorded("config-get-adapters-q4", "txt")))
        runner.commanding(["config", "get", "backends.tuned.adapters"], .done(stdout: recorded("config-get-adapters-none", "txt")))
        let model = ModelsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        return (model, runner)
    }

    func open(_ backend: String, in model: ModelsViewModel) async {
        await model.select(model.rows.first { $0.backend == backend }!)
    }

    @Test("a backend's adapters listed with their scale, and each pin as the doctor states it")
    func listedWithPins() async throws {
        let (model, runner) = await library()
        await open("based", in: model)
        let adapters = try #require(model.adapters)
        #expect(adapters.rows.map(\.adapter.path) == [
            "/Users/someone/.apogee/training/versions/based/v1.lora.gguf", "/Users/someone/extra.lora.gguf",
        ])
        #expect(adapters.rows.map(\.scaleWords) == ["scale 1", "scale 0.5"])
        #expect(adapters.rows.map { $0.check?.status } == ["ok", "ok"])
        #expect(adapters.rows[0].check?.detail == "applies over tiny-F16.gguf at scale 1, pinned to its weights (sha256 ce5065b7f3dc)")
        #expect(adapters.checkFailure == nil)
        #expect(runner.calls.suffix(2) == ["run config get backends.based.adapters", "read check"])
    }

    @Test("an adapter whose base changed under it: the doctor's failure, and the promote that mends it, in its words")
    func failingPin() async throws {
        let (model, _) = await library()
        await open("q4", in: model)
        let row = try #require(model.adapters?.rows.first)
        #expect(row.check?.status == "fail")
        #expect(row.check?.detail.contains("than the base /Users/someone/.apogee/models/tiny/gguf/cccccccccccc/tiny-Q4_K_M.gguf holds now (b8ceabefbcbc)") == true)
        #expect(row.check?.remedy == "apogee train promote 20261026-065638 --as q4 --adapter --suite <path|name> to promote its run again over these weights")
    }

    @Test("a backend with none says so, and the doctor isn't asked; one that takes none -- a cloud API -- reads nothing")
    func noneAndNotTaking() async {
        let (model, runner) = await library()
        await open("tuned", in: model)
        #expect(model.adapters?.rows.isEmpty == true)
        #expect(!runner.calls.contains("read check"))
        #expect(ModelsViewModel.takesAdapters(model.rows.first { $0.backend == "tuned" }!))
        let cloud = ModelRow.list(from: ReadDocument(text: ModelsViewModelTests.list)!).first { $0.backend == "claude" }!
        #expect(!ModelsViewModel.takesAdapters(cloud))
    }

    @Test("edited as one list: set-backend --adapters with every path and its scale, all taken off as \"\"")
    func editArgv() async throws {
        let (model, _) = await library()
        await open("based", in: model)
        model.editAdapters(model.selectedRow!)
        var form = try #require(model.editingAdapters)
        #expect(form.entries.map(\.scale) == ["", "0.5"])
        #expect(form.arguments == [
            "config", "set-backend", "based", "--adapters",
            "/Users/someone/.apogee/training/versions/based/v1.lora.gguf,/Users/someone/extra.lora.gguf:0.5",
        ])
        form.entries[1].scale = "1.5"
        form.add("/Users/someone/third.lora.gguf")
        #expect(form.value.hasSuffix(",/Users/someone/extra.lora.gguf:1.5,/Users/someone/third.lora.gguf"))
        for entry in form.entries { form.remove(entry.id) }
        #expect(form.arguments == ["config", "set-backend", "based", "--adapters", ""], "--adapters \"\" clears it")
        var mlx = AdaptersForm(backend: "qwen-mlx", backendType: "mlx", adapters: [BackendAdapter(path: "/Users/someone/adapter")])
        mlx.entries[0].scale = "0.5"
        #expect(mlx.value == "/Users/someone/adapter", "an mlx entry applies one adapter at its trained scale")
    }

    @Test("applied, read back -- the list and the doctor again -- the CLI's own line said")
    func applyReadsBack() async {
        let (model, runner) = await library()
        await open("based", in: model)
        var said: [String] = []
        model.say = { said.append($0) }
        runner.commanding(["config", "set-backend"], .done(stdout: recorded("config-set-backend-adapters", "txt")))
        model.editAdapters(model.selectedRow!)
        let extra = model.editingAdapters!.entries[1].id
        model.editingAdapters?.remove(extra)
        let before = runner.calls.count
        await model.applyAdapters()
        let after = Array(runner.calls.dropFirst(before))
        #expect(after.first == "run config set-backend based --adapters /Users/someone/.apogee/training/versions/based/v1.lora.gguf")
        #expect(after.contains("run config get backends.based.adapters") && after.last == "read check", "read back")
        #expect(model.editingAdapters == nil)
        #expect(said == [
            "edited backend 'based' in /Users/someone/.apogee/config/config.json: set adapters; adapters pinned to /Users/someone/.apogee/models/tiny/gguf/bbbbbbbbbbbb/tiny-F16.gguf (sha256 ce5065b7f3dc)",
        ])
    }

    @Test("a value the CLI refuses is said in its words, nothing claimed")
    func refused() async {
        let (model, runner) = await library()
        await open("based", in: model)
        runner.commanding(
            ["config", "set-backend"],
            .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail(recorded("config-set-backend-adapters-refused", "err"))))
        model.editAdapters(model.selectedRow!)
        model.editingAdapters?.entries[1].scale = "3"
        await model.applyAdapters()
        #expect(model.failure?.hasPrefix("apogee config: adapters: /Users/someone/extra.lora.gguf:3: its scale 3 is outside (0, 2]") == true)
    }

    @Test("a doctor that can't be read leaves the pins unknown, said")
    func doctorUnread() async throws {
        let (model, runner) = await library()
        runner.reading(["check"], outcome: .timedOut(after: .seconds(60), stderrTail: BoundedTail()))
        await open("based", in: model)
        let adapters = try #require(model.adapters)
        #expect(adapters.rows.allSatisfy { $0.check == nil })
        #expect(adapters.checkFailure?.hasPrefix("The doctor could not be read: ") == true)
    }
}
