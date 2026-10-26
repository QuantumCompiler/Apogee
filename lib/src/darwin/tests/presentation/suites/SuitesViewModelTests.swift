import BusinessReads
import DataProtocol
import Foundation
import Testing

@testable import PresentationSuites

@Suite("SuitesViewModel", .timeLimit(.minutes(1)))
@MainActor
struct SuitesViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func scripted() -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.saying(["config", "get", "suites"], "coding\nresearch\n")
        runner.saying(["config", "get", "suites.coding"], "chat: qwen\nutility: small (window 4096)\n")
        runner.saying(["config", "get", "suites.research"], "chat: claude\nembedding: mlx-embed\n")
        runner.reading(["models", "status"], #"{"fields":[{"field":"suite","value":"coding"},{"field":"chat","value":"qwen   (via suite coding)"}]}"#)
        runner.reading(["models", "list"], #"{"object":"list","data":[{"backend":"qwen","backend_type":"llamacpp"},{"backend":"small","backend_type":"llamacpp"},{"backend":"claude","backend_type":"anthropic"},{"backend":"mlx-embed","backend_type":"mlx"}]}"#)
        runner.reading(["system"], #"{"gpu":{"model_budget_bytes":8589934592}}"#)
        runner.reading(["models", "info", "qwen"], #"{"name":"qwen","fields":[{"field":"size","value":"4800 MiB"},{"field":"cache","value":"4608 MiB at f16 (the default)"}]}"#)
        runner.reading(["models", "info", "small"], #"{"name":"small","fields":[{"field":"size","value":"600 MiB"},{"field":"cache","value":"none -- no attention layers"}]}"#)
        runner.reading(["models", "info", "mlx-embed"], #"{"name":"mlx-embed","fields":[{"field":"format","value":"MLX"}]}"#)
        return runner
    }

    @Test("the suites and their members, read through config get; the default from models status")
    func reads() async {
        let suites = SuitesViewModel(runner: scripted())
        await suites.connect(binary: Self.binary)
        #expect(suites.suites.map(\.name) == ["coding", "research"])
        #expect(suites.suites[0].members == [
            Suite.Member(role: "chat", backend: "qwen", window: nil), Suite.Member(role: "utility", backend: "small", window: 4096),
        ])
        #expect(suites.defaultSuite == "coding")
        #expect(suites.selected == "coding")
    }

    @Test("the fit: every member's size stated sums against the budget; any unstated is said unknown, never guessed")
    func fit() async {
        let suites = SuitesViewModel(runner: scripted())
        await suites.connect(binary: Self.binary)
        let coding = suites.suites[0]
        #expect(suites.members(coding).map(\.memory) == [
            .known(weights: 4800 << 20, cache: 4608 << 20), .known(weights: 600 << 20, cache: 0),
        ])
        #expect(suites.fit(coding) == .doesNotFit(total: (4800 + 4608 + 600) << 20, budget: 8_589_934_592))
        let research = suites.suites[1]
        #expect(suites.members(research).map(\.memory) == [.none, .unknown], "a cloud API holds nothing here; an MLX record states no size")
        #expect(suites.fit(research) == .unknown(members: ["mlx-embed"]))
        #expect(SuitesViewModel.words(suites.fit(research)) == "unknown: no size stated for mlx-embed")
    }

    @Test("a backend in two roles is held once, and a suite within the budget fits")
    func heldOnce() async {
        let runner = scripted()
        runner.saying(["config", "get", "suites"], "duo\n")
        runner.saying(["config", "get", "suites.duo"], "chat: small\nutility: small\n")
        let suites = SuitesViewModel(runner: runner)
        await suites.connect(binary: Self.binary)
        #expect(suites.fit(suites.suites[0]) == .fits(total: 600 << 20, budget: 8_589_934_592))
    }

    @Test("the typed facts (52e) are taken first; MiB is the only unit read")
    func memoryParsing() throws {
        let typed = ModelInfo(document: try #require(ReadDocument(text: #"{"name":"q","fields":[],"data":{"bytes":100,"cache":{"bytes":50}}}"#)))
        #expect(SuitesViewModel.memory(in: typed) == .known(weights: 100, cache: 50))
        #expect(SuitesViewModel.mib("4608 MiB at f16") == 4608 << 20)
        #expect(SuitesViewModel.mib("4.5 GiB") == nil)
        #expect(SuitesViewModel.mib(nil) == nil)
    }

    @Test("making a suite the default is its verb, and off clears it; read back")
    func makeDefault() async {
        let runner = scripted()
        let suites = SuitesViewModel(runner: runner)
        await suites.connect(binary: Self.binary)
        await suites.makeDefault(suites.suites[1])
        #expect(runner.calls.contains("run config set-default-suite research"))
        await suites.makeDefault(nil)
        #expect(runner.calls.contains("run config set-default-suite off"))
        let after = runner.calls.drop { $0 != "run config set-default-suite off" }
        #expect(after.contains("run config get suites") && after.contains("read models status"), "read back")
    }

    @Test("no suites is said plainly; no binary is said")
    func empty() async {
        let runner = ScriptedRunner()
        runner.saying(["config", "get", "suites"], "")
        let suites = SuitesViewModel(runner: runner)
        await suites.connect(binary: Self.binary)
        #expect(suites.suites.isEmpty && suites.failure == nil)
        let missing = SuitesViewModel(runner: ScriptedRunner())
        await missing.connect(binary: nil)
        #expect(missing.failure == "Apogee isn't installed, so there are no suites to read.")
    }
}
