import BusinessReads
import Foundation
import Testing

@testable import PresentationModels

private func recorded(_ name: String, _ ext: String = "json") -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name).\(ext)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// Stored weights over a store recorded from the installed binary: a pulled
/// Llama SafeTensors set converted to an F16 GGUF (not registered) and
/// quantized to a Q4_K_M one (registered as `llama3.2-1b-Q4KM`); a Gemma
/// SafeTensors set and the MLX model converted from it (not registered); a
/// voice model. The GGUFs are tiny headers, nothing converted for real.
@Suite("StoreViewModel", .timeLimit(.minutes(1)))
@MainActor
struct StoreViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let q4Path = "/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/8d51e6c04a27/Llama3.2-1B-Instruct-Q4KM.gguf"

    func store(_ runner: ScriptedRunner = ScriptedRunner()) async -> (StoreViewModel, ScriptedRunner) {
        runner.reading(["models", "list"], recorded("store-list"))
        runner.reading(["models", "list", "--all"], recorded("store-list-all"))
        runner.reading(["models", "info", "llama3.2-1b-Q4KM"], recorded("store-info-backend"))
        runner.reading(["models", "info", Self.q4Path], recorded("store-info-q4"))
        runner.reading(["models", "info", "Llama3.2-1B-Instruct/gguf/8d51e6c04a27"], recorded("store-info-q4"))
        runner.reading(["models", "info", "Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41"], recorded("store-info-f16"))
        runner.reading(["models", "info", "Llama3.2-1B-Instruct/safetensors/edaba290ce7f"], recorded("store-info-snapshot"))
        runner.reading(["models", "info", "Gemma4-26B-Instruct/mlx/a88b9f386aff"], recorded("store-info-mlx"))
        runner.commanding(["config", "scan"], .done(stdout: recorded("config-scan", "txt")))
        let store = StoreViewModel(runner: runner)
        await store.connect(binary: Self.binary)
        await store.refresh()
        return (store, runner)
    }

    @Test("every set by its handle, grouped by model: the registered one found through its backend's own documents, the consumed folded and said")
    func sets() async {
        let (store, runner) = await store()
        #expect(store.groups.map(\.model) == ["Gemma4-26B-Instruct", "Kokoro-82M", "Llama3.2-1B-Instruct"])
        #expect(store.sets.map(\.handle) == [
            "Gemma4-26B-Instruct/mlx/a88b9f386aff", "Kokoro-82M/voice/5f3a2c91d0e4",
            "Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41", "Llama3.2-1B-Instruct/gguf/8d51e6c04a27",
        ])
        #expect(runner.calls.contains("read models info \(Self.q4Path)"), "the registered set's handle is the CLI's answer for its path")
        let byHandle = Dictionary(uniqueKeysWithValues: store.sets.map { ($0.handle, $0) })
        #expect(byHandle["Llama3.2-1B-Instruct/gguf/8d51e6c04a27"]?.state == "backend llama3.2-1b-Q4KM")
        #expect(byHandle["Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41"]?.state == "not registered")
        #expect(byHandle["Gemma4-26B-Instruct/mlx/a88b9f386aff"]?.state == "not registered")
        #expect(byHandle["Gemma4-26B-Instruct/mlx/a88b9f386aff"]?.precision == "mixed 4/8-bit (group 64)")
        #expect(byHandle["Kokoro-82M/voice/5f3a2c91d0e4"]?.state == "voice model")
        #expect(store.summary == "3 models, 4 sets, in the model store")
        #expect(store.madeFromLine(byHandle["Llama3.2-1B-Instruct/gguf/8d51e6c04a27"]!) == "quantized from Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41")
        #expect(store.summary(of: store.groups.last!.sets) == "2 sets · 3.0 GiB", "a group's size only where every set states one")
        #expect(store.foldedText == "2 snapshots consumed by conversions are folded")
        #expect(store.unregistered == 2)
        #expect(store.bytes(byHandle["Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41"]!) == 2_479_596_089, "a size only where a document states it")
    }

    @Test("--all shows the consumed sets, marked; SafeTensors say they are weights only")
    func showingAll() async {
        let (store, _) = await store()
        store.showingAll = true
        #expect(await eventually { store.sets.count == 6 })
        let consumed = store.sets.filter(\.consumed).map(\.handle)
        #expect(consumed == ["Gemma4-26B-Instruct/safetensors/13de95725073", "Llama3.2-1B-Instruct/safetensors/edaba290ce7f"])
        #expect(store.sets.first { $0.consumed }?.state == "used by a conversion")
        #expect(store.foldedText == nil)
    }

    @Test("a set's details: made from, linked where the store lists the parent; made from it as the CLI names it")
    func lineage() async {
        let (store, _) = await store()
        store.showingAll = true
        #expect(await eventually { store.sets.count == 6 })
        await store.select("Llama3.2-1B-Instruct/gguf/8d51e6c04a27")
        #expect(store.madeFrom.map(\.kind) == ["quantized", "converted", "pulled"])
        #expect(store.madeFrom.first?.from == "Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41")
        #expect(store.isListed("Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41"))
        #expect(!store.isListed("meta-llama/Llama-3.2-1B-Instruct"), "a pull's ref is words, not a link")
        await store.select("Llama3.2-1B-Instruct/safetensors/edaba290ce7f")
        #expect(store.madeFromIt == ["Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41", "Llama3.2-1B-Instruct/gguf/8d51e6c04a27"])
        #expect(store.path == "/Users/someone/.apogee/models/Llama3.2-1B-Instruct/safetensors/edaba290ce7f")
    }

    @Test("register: the name config scan lists it under, the flags left out unless changed, run as typed and read back")
    func register() async throws {
        let (store, runner) = await store()
        let mlx = try #require(store.sets.first { $0.format == "mlx" })
        await store.askToRegister(mlx)
        var form = try #require(store.register)
        #expect(form.name == "Gemma4-26B-Instruct-mixed_4_8")
        #expect(form.arguments == ["config", "add-backend", "Gemma4-26B-Instruct-mixed_4_8", "--type", "mlx", "--model-path", "/Users/someone/.apogee/models/Gemma4-26B-Instruct/mlx/a88b9f386aff"])
        form.cache = "q4_0"
        #expect(form.arguments?.contains("--cache-type") == false, "an MLX backend takes no attention cache type")
        let f16 = try #require(store.sets.first { $0.handle.hasSuffix("3f0c2a9b7e41") })
        await store.askToRegister(f16)
        form = try #require(store.register)
        #expect(form.name == "Llama3.2-1B-Instruct-F16" && form.type == "llamacpp")
        store.register?.window = "16384"
        store.register?.cache = "f16"
        #expect(store.register?.arguments == [
            "config", "add-backend", "Llama3.2-1B-Instruct-F16", "--type", "llamacpp", "--model-path",
            "/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41/Llama3.2-1B-Instruct-F16.gguf",
            "--context-size", "16384", "--cache-type", "f16",
        ])
        await store.registerIt()
        #expect(runner.calls.contains { $0.hasPrefix("run config add-backend Llama3.2-1B-Instruct-F16 --type llamacpp") })
        #expect(runner.calls.filter { $0 == "read models list --all" }.count == 2, "read back")
    }

    @Test("register all shows config scan's own report first, then runs --register")
    func registerAll() async {
        let (store, runner) = await store()
        await store.askToRegisterAll()
        guard case .registerAll(let report)? = store.pending else {
            Issue.record("not asked")
            return
        }
        #expect(report.contains("Gemma4-26B-Instruct-mixed_4_8  not registered"))
        #expect(!runner.calls.contains("run config scan --register"))
        await store.confirm()
        #expect(runner.calls.contains("run config scan --register"))
    }

    @Test("deletes and removals confirm; --and-backends only when chosen; a model renamed by its folder")
    func removing() async throws {
        let (store, runner) = await store()
        let q4 = try #require(store.sets.first { $0.backend != nil })
        store.askToDelete(q4)
        await store.confirm()
        #expect(runner.calls.contains("run models delete Llama3.2-1B-Instruct/gguf/8d51e6c04a27 --yes"))
        store.askToDeleteModel("Llama3.2-1B-Instruct")
        store.andBackends = true
        await store.confirm()
        #expect(runner.calls.contains("run models delete Llama3.2-1B-Instruct --yes --and-backends"))
        store.askToRemoveBackend("llama3.2-1b-Q4KM")
        #expect(!runner.calls.contains("run config delete-backend llama3.2-1b-Q4KM"), "asked first")
        await store.confirm()
        #expect(runner.calls.contains("run config delete-backend llama3.2-1b-Q4KM"))
        await store.rename("Llama3.2-1B-Instruct", to: "Llama3.2-1B")
        #expect(runner.calls.contains("run models rename Llama3.2-1B-Instruct Llama3.2-1B"))
        #expect(store.handle(forBackend: "llama3.2-1b-Q4KM") == "Llama3.2-1B-Instruct/gguf/8d51e6c04a27", "Show its weights")
    }

    @Test("config scan's name for a path: the line above it")
    func scanName() {
        let report = recorded("config-scan", "txt")
        #expect(StoreViewModel.scanName(for: Self.q4Path, in: report) == "Llama3.2-1B-Instruct-Q4KM")
        #expect(StoreViewModel.scanName(for: "/nowhere", in: report) == nil)
    }

    @Test("sizes in the CLI's own units, truncated as it truncates them")
    func sizes() {
        #expect(StoreViewModel.size(1028) == "1 KiB")
        #expect(StoreViewModel.size(807_694_464) == "770 MiB")
        #expect(StoreViewModel.size(14_232_576_040) == "13.2 GiB", "the MLX model's note says 13.2 GiB")
        #expect(StoreViewModel.size(512) == "512 B")
    }
}
