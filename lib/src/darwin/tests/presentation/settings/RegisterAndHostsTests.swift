import BusinessJobs
import BusinessReads
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationSettings

/// GUI M16, Settings' side of the CLI's M33: one scanned provider registered
/// by `providers register <id>` on a binary that has it -- the default it
/// sets and the roster it fetches said, as the TUI's `r` says them -- and
/// `config add-backend` on one before; the allowed hosts read through `config
/// get tools.allowed_hosts` where the binary reads them, and written blind as
/// before where it doesn't. Recorded from the branch binary (schema
/// 2026-10-26) over sandbox homes with fake `claude`, `codex` (its `debug
/// models` answering a roster) and `ollama` CLIs on the sandbox's own PATH,
/// no key and a config with no default, the home written `/Users/someone`;
/// the `-older` words from the installed binary (schema 2026-10-25).
@Suite("Settings: Register and the allowed hosts follow the CLI (GUI M16)", .timeLimit(.minutes(1)))
@MainActor
struct RegisterAndHostsTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func model(_ runner: RuleRunner, vocabulary: String?) -> (SettingsViewModel, Toasts) {
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        model.vocabulary = { vocabulary }
        let said = Toasts()
        model.say = { said.lines.append($0) }
        return (model, said)
    }

    /// What the toast said, in order.
    final class Toasts {
        var lines: [String] = []
    }

    func scanned() -> RuleRunner {
        let runner = RuleRunner()
        runner.reading(["providers", "scan"], recordedText("providers-scan-fakes.json"))
        return runner
    }

    func provider(_ id: String, in model: SettingsViewModel) throws -> SettingsViewModel.Provider {
        try #require(model.providers.first { $0.provider == id }, "providers: \(model.providers.map(\.provider))")
    }

    // MARK: - Register

    @Test("on M33's binary Register is providers register <id>: no config add-backend, its lines -- the registration, the default set, the roster fetched -- said")
    func registerThroughProvidersRegister() async throws {
        let runner = scanned()
        runner.reading(["providers", "register", "codex"], recordedText("providers-register-codex.json"))
        let (model, said) = model(runner, vocabulary: "2026-10-26")
        await model.connect(binary: Self.binary, version: "0.2.0")
        let codex = try provider("codex", in: model)
        #expect(model.registration(for: codex) == ["providers", "register", "codex"], "the command the sheet shows")
        runner.reading(["providers", "scan"], recordedText("providers-scan-after-codex.json"))
        await model.register(codex)
        #expect(runner.calls.contains("read providers register codex"))
        #expect(!runner.calls.contains { $0.hasPrefix("run config add-backend") })
        #expect(model.registered == SettingsViewModel.Said(title: "Registered codex", lines: [
            "registered codex (codex-cli)", "models.default set to 'codex' (none was set)", "fetched codex-cli's models: 2 on the roster",
        ]))
        #expect(said.lines.last == "registered codex (codex-cli)")
        #expect(try provider("codex", in: model).backend == "backend: codex", "read back by the scan")
        #expect(model.failure == nil)
        model.dismissRegistered()
        #expect(model.registered == nil)
    }

    @Test("a registration that says one line -- claude-cli has no roster, the default already set -- is its toast alone")
    func oneLineIsAToast() async throws {
        let runner = scanned()
        runner.reading(["providers", "register", "claude"], recordedText("providers-register-claude.json"))
        let (model, said) = model(runner, vocabulary: "2026-10-26")
        await model.connect(binary: Self.binary, version: "0.2.0")
        await model.register(try provider("claude", in: model))
        #expect(model.registered == nil)
        #expect(said.lines.last == "registered claude (claude-cli)")
    }

    @Test("a refusal of providers register is the CLI's words")
    func refusedInItsWords() async throws {
        let runner = scanned()
        var tail = BoundedTail()
        tail.append(Data(recordedText("providers-register-unscanned.err").utf8))
        runner.failingRead(["providers", "register"], .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail))
        let (model, said) = model(runner, vocabulary: "2026-10-26")
        await model.connect(binary: Self.binary, version: "0.2.0")
        await model.register(try provider("codex", in: model))
        #expect(model.failure?.hasPrefix("apogee providers: this machine has not been scanned for providers yet") == true)
        #expect(said.lines.last?.hasPrefix("That didn't work: apogee providers: this machine has not been scanned") == true)
        #expect(model.registered == nil)
    }

    @Test("Ollama on M33's binary: no model named, providers register -- the CLI picks its one, or says it needs one; a model named, the add-backend the CLI names, its default said")
    func ollamaOnTheNewBinary() async throws {
        let runner = scanned()
        runner.reading(["providers", "register", "ollama"], recordedText("providers-register-ollama-none.json"))
        runner.whenStarting(["config", "add-backend", "ollama"], .done(stdout: recordedText("providers-add-backend-ollama.txt")))
        let (model, _) = model(runner, vocabulary: "2026-10-26")
        await model.connect(binary: Self.binary, version: "0.2.0")
        let ollama = try provider("ollama", in: model)
        #expect(model.registration(for: ollama) == ["providers", "register", "ollama"])
        await model.register(ollama)
        #expect(runner.calls.contains("read providers register ollama"))
        #expect(model.registered?.lines == [
            "ollama: not registered -- an ollama-cli backend needs a model; add one with: apogee config add-backend ollama --type ollama-cli --model <model>",
            "nothing written",
        ])
        #expect(model.registration(for: ollama, model: " llama3.2:3b ") == ["config", "add-backend", "ollama", "--type", "ollama-cli", "--model", "llama3.2:3b"])
        await model.register(ollama, model: " llama3.2:3b ")
        #expect(runner.calls.contains("run config add-backend ollama --type ollama-cli --model llama3.2:3b"))
        #expect(model.registered == SettingsViewModel.Said(title: "Registered ollama", lines: [
            "added backend 'ollama' to /Users/someone/.apogee/config/config.json", "models.default set to 'ollama' (none was set)",
        ]))
    }

    @Test("a binary before M33 -- or one that hasn't said -- registers by config add-backend, as today, its line the toast")
    func olderComposesAddBackend() async throws {
        for vocabulary in ["2026-10-25", nil] as [String?] {
            let runner = scanned()
            runner.whenStarting(["config", "add-backend"], .done(stdout: recordedText("providers-add-backend-older.txt")))
            // What the installed binary answers the verb it lacks, were it asked.
            var lacks = BoundedTail()
            lacks.append(Data(recordedText("providers-register-older.err").utf8))
            runner.failingRead(["providers", "register"], .failed(exit: ChildExit(reason: .exited, status: 106), stderrTail: lacks))
            let (model, said) = model(runner, vocabulary: vocabulary)
            await model.connect(binary: Self.binary, version: "0.2.0")
            let codex = try provider("codex", in: model)
            #expect(model.registration(for: codex) == ["config", "add-backend", "codex", "--type", "codex-cli"])
            await model.register(codex)
            #expect(runner.calls.contains("run config add-backend codex --type codex-cli"))
            #expect(!runner.calls.contains { $0.hasPrefix("read providers register") })
            #expect(model.registered == nil)
            #expect(said.lines.last == "added backend 'codex' to /Users/someone/.apogee/config/config.json")
        }
    }

    // MARK: - The allowed hosts

    @Test("on M33's binary Permissions reads the allowed hosts through config get, and reads them back after each change")
    func hostsReadThroughTheCLI() async throws {
        let runner = RuleRunner()
        runner.when(["config", "get", "tools.allowed_hosts"], .done(stdout: recordedText("allowed-hosts.txt")))
        runner.when(["config", "delete-allowed-host", "docs.python.org"], .done(stdout: recordedText("allowed-host-delete.txt")))
        let (model, said) = model(runner, vocabulary: "2026-10-26")
        await model.connect(binary: Self.binary, version: "0.2.0")
        #expect(model.allowedHosts == ["docs.python.org", "developer.apple.com"])
        #expect(model.allowedHostsUnread == nil)
        runner.when(["config", "get", "tools.allowed_hosts"], .done(stdout: recordedText("allowed-hosts-after-delete.txt")))
        await model.forbidHost("docs.python.org")
        #expect(model.allowedHosts == ["developer.apple.com"], "read back")
        #expect(said.lines.last == "removed docs.python.org from tools.allowed_hosts")
    }

    @Test("an empty list reads as none, never as unread; allowing a host says the CLI's words and reads the list again")
    func hostsEmpty() async {
        let runner = RuleRunner()
        runner.when(["config", "get", "tools.allowed_hosts"], .done(stdout: recordedText("allowed-hosts-empty.txt")))
        runner.when(["config", "add-allowed-host", "docs.python.org"], .done(stdout: recordedText("allowed-host-add.txt")))
        let (model, said) = model(runner, vocabulary: "2026-10-26")
        await model.connect(binary: Self.binary, version: "0.2.0")
        #expect(model.allowedHosts == [])
        await model.allowHost(" docs.python.org ")
        #expect(runner.calls.contains("run config add-allowed-host docs.python.org"))
        #expect(said.lines.last == "tools.allowed_hosts includes docs.python.org")
        #expect(runner.calls.filter { $0 == "run config get tools.allowed_hosts" }.count == 2, "read back")
    }

    @Test("a binary before M33 keeps today's form: the hosts never read, each change's words its confirmation")
    func hostsOlderAsToday() async {
        for vocabulary in ["2026-10-25", nil] as [String?] {
            let runner = RuleRunner()
            let (model, _) = model(runner, vocabulary: vocabulary)
            await model.connect(binary: Self.binary, version: "0.2.0")
            #expect(!runner.calls.contains("run config get tools.allowed_hosts"))
            #expect(model.allowedHosts == nil && model.allowedHostsUnread == nil)
        }
    }

    @Test("a dated binary that refuses the key falls back to today's form, its words kept")
    func hostsRefusedFallsBack() async {
        let runner = RuleRunner()
        runner.when(["config", "get", "tools.allowed_hosts"], refused("allowed-hosts-older.err"))
        let (model, _) = model(runner, vocabulary: "2026-10-26")
        await model.connect(binary: Self.binary, version: "0.2.0")
        #expect(model.allowedHosts == nil)
        #expect(model.allowedHostsUnread?.hasPrefix("apogee config: no such config key: 'tools.allowed_hosts'") == true)
    }
}
