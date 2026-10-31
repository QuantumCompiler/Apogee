import BusinessJobs
import BusinessReads
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationSettings

/// GUI 4k, the second, third and seventh gaps in Settings: one scanned
/// provider registered from its row, each vendor backend's `server_tools`
/// consent shown beside it, and no sentence asserting what the binary can't
/// do before it has said.
@Suite("Settings reaches its remaining verbs (GUI 4k)", .timeLimit(.minutes(1)))
@MainActor
struct SettingsParityTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func model(_ runner: RuleRunner) -> SettingsViewModel {
        SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
    }

    /// The branch's binary over a sandbox with one claude-cli, one anthropic
    /// and one openai entry, the anthropic one consenting to both tools.
    func recorded() -> RuleRunner {
        let runner = RuleRunner()
        runner.reading(["models", "list"], recordedText("models-list-server-tools.json"))
        runner.reading(["providers", "scan"], recordedText("providers-scan.json"))
        runner.when(["config", "get", "backends.claude-api.server_tools"], .done(stdout: recordedText("config-get-server-tools.txt")))
        runner.when(["config", "get", "backends.gpt.server_tools"], .done(stdout: recordedText("config-get-server-tools-none.txt")))
        return runner
    }

    @Test("the scan's rows: those it says are not registered offer Register, of their type; the rest don't")
    func registerable() async {
        let model = model(recorded())
        await model.connect(binary: Self.binary, version: "0.2.0")
        #expect(model.providers.filter(\.registers).map(\.provider) == ["claude", "codex", "gemini", "ollama"])
        #expect(model.providers.first { $0.provider == "claude" }?.type == "claude-cli")
        #expect(model.providers.first { $0.provider == "ollama" }?.needsModel == true)
    }

    @Test("Register is config add-backend <provider> --type <type> -- the entry the TUI's r writes -- read back by the scan, the CLI's words said")
    func registerOne() async throws {
        let runner = recorded()
        runner.whenStarting(["config", "add-backend"], .done(stdout: recordedText("providers-register.txt")))
        var said: [String] = []
        let model = model(runner)
        model.say = { said.append($0) }
        await model.connect(binary: Self.binary, version: "0.2.0")
        let claude = try #require(model.providers.first { $0.provider == "claude" })
        model.registering = claude
        runner.reading(["providers", "scan"], recordedText("providers-scan-registered.json"))
        await model.register(claude)
        #expect(model.registering == nil)
        #expect(runner.calls.contains("run config add-backend claude --type claude-cli"))
        #expect(runner.calls.last { $0.hasPrefix("read providers scan") } != nil)
        #expect(model.providers.first { $0.provider == "claude" }?.backend == "backend: claude", "read back")
        #expect(model.providers.first { $0.provider == "claude" }?.registers == false)
        #expect(said.last == "added backend 'claude' to /Users/someone/.apogee/config/config.json")
    }

    @Test("Ollama registers only with a model named; a refusal is the CLI's words")
    func modelAndRefusal() async throws {
        let runner = recorded()
        runner.whenStarting(["config", "add-backend"], refused("providers-register-taken.err"))
        let model = model(runner)
        await model.connect(binary: Self.binary, version: "0.2.0")
        let ollama = try #require(model.providers.first { $0.provider == "ollama" })
        await model.register(ollama)
        #expect(!runner.calls.contains { $0.hasPrefix("run config add-backend") }, "no model, nothing run")
        await model.register(ollama, model: " llama3.2 ")
        #expect(runner.calls.contains("run config add-backend ollama --type ollama-cli --model llama3.2"))
        #expect(model.failure?.hasPrefix("apogee config: backend 'claude' already exists") == true)
    }

    @Test("each vendor backend's server_tools is read through config get and said beside it; a roster's rows and local types ask nothing")
    func serverToolsShown() async throws {
        let runner = recorded()
        let model = model(runner)
        await model.connect(binary: Self.binary, version: "0.2.0")
        #expect(model.backends.map(\.backend) == ["claude", "claude-api", "gpt"], "the configured entries; a roster's models are not")
        #expect(runner.calls.filter { $0.hasSuffix(".server_tools") } == [
            "run config get backends.claude-api.server_tools", "run config get backends.gpt.server_tools",
        ])
        #expect(model.serverTools["claude-api"] == .consented(["web_search", "code_execution"]))
        #expect(model.serverTools["gpt"] == .consented([]))
        let api = try #require(model.backends.first { $0.backend == "claude-api" })
        #expect(model.serverToolsLine(api) == "Vendor-run tools consented: web_search, code_execution -- the vendor runs them under your key")
        let gpt = try #require(model.backends.first { $0.backend == "gpt" })
        #expect(model.serverToolsLine(gpt) == "Vendor-run tools: none consented")
        let cli = try #require(model.backends.first { $0.backend == "claude" })
        #expect(model.serverToolsLine(cli) == nil, "a vendor CLI runs none")
    }

    @Test("a binary before 55g refuses the key: said in its words, never read as none")
    func serverToolsUnreadable() async throws {
        let runner = recorded()
        runner.when(["config", "get", "backends.claude-api.server_tools"], refused("config-get-server-tools-older.err"))
        let model = model(runner)
        await model.connect(binary: Self.binary, version: "0.1.9")
        let api = try #require(model.backends.first { $0.backend == "claude-api" })
        #expect(model.serverToolsLine(api)?.hasPrefix("Vendor-run tools: this apogee can't read them -- apogee config: no such config key: 'backends.claude-api.server_tools'") == true)
    }

    @Test("Web search and Recall: settable on a capable binary; 'no config set' only from one that said so; unknown otherwise")
    func wording() async {
        let model = model(RuleRunner())
        model.hasTrack52 = { true }
        model.capabilitiesKnown = { true }
        #expect(model.configSetHint == nil)
        model.hasTrack52 = { false }
        #expect(model.configSetHint == "Read here, set in the config file: this apogee has no config set.")
        model.capabilitiesKnown = { false }
        #expect(model.configSetHint == "Read here, set in the config file: this apogee hasn't said what it can do, so setting it here isn't offered.")
        #expect(model.configSetHint?.contains("has no config set") == false, "never asserted without having asked")
    }

    @Test("every read asks first whether the binary changed")
    func checksTheBinary() async {
        var asked = 0
        let model = model(RuleRunner())
        model.checkCapabilities = { asked += 1 }
        await model.connect(binary: Self.binary, version: "0.2.0")
        await model.refresh()
        #expect(asked == 2)
    }
}
