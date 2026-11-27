import BusinessJobs
import BusinessReads
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationSettings

/// Settings' and the Xcode pane's lines with every positional after `--`
/// (GUI M23): a typed host, a listed host byte for byte, a permission's tool
/// and level, a key's provider, a plain config key and its value, the video
/// default, a provider registered, a key path held in a variable -- and the
/// busy label the line as a person types it, never its first three words.
@Suite("Settings names", .timeLimit(.minutes(1)))
@MainActor
struct SettingsNamesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func model(_ runner: RuleRunner) -> SettingsViewModel {
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        model.vocabulary = { "2026-10-26" }
        model.hasTrack52 = { true }
        return model
    }

    @Test("a typed -h host is allowed after --, the CLI's words said; a listed host is removed as the list holds it, never trimmed")
    func hosts() async {
        let runner = RuleRunner()
        runner.when(["config", "add-allowed-host", "-h"], .failed(exit: ChildExit(reason: .exited, status: 106), stderrTail: {
            var tail = BoundedTail()
            tail.append(Data("host is required\nRun with --help for more information.\n".utf8))
            return tail
        }()))
        runner.when(["config", "add-allowed-host", "--", "-h"], .done(stdout: "tools.allowed_hosts includes -h\n"))
        let settings = model(runner)
        var said: [String] = []
        settings.say = { said.append($0) }
        await settings.connect(binary: Self.binary, version: "0.2.2")
        await settings.allowHost(" -h ")
        #expect(runner.calls.contains("run config add-allowed-host -- -h"), "a typed host is trimmed")
        #expect(said.last == "tools.allowed_hosts includes -h", "said: \(said)")
        await settings.forbidHost("docs.python.org ")
        #expect(runner.calls.contains("run config delete-allowed-host -- docs.python.org "), "a listed host byte for byte")
    }

    @Test("the busy label is the line as a person types it, Argv.shown's, never its first three words")
    func busyLabel() async {
        let runner = HoldingSettingsRunner(holding: ["config", "set-permission", "--", "-x", "ask"])
        let settings = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        await settings.connect(binary: Self.binary, version: "0.2.2")
        let change = Task { await settings.setPermission("-x", "ask") }
        #expect(await settingsEventually { runner.held })
        #expect(settings.working == "config set-permission -- -x ask")
        runner.release()
        await change.value
        #expect(settings.working == nil)
    }

    @Test("a key's line names its provider after --, the key on standard input alone; the sheet shows the line that runs")
    func keyLines() {
        #expect(SettingsViewModel.addKeyArguments("anthropic") == ["auth", "add", "--stdin", "--", "anthropic"])
        #expect(Argv.shown(SettingsViewModel.addKeyArguments("anthropic")) == "auth add --stdin anthropic")
    }

    @Test("every line Settings builds with -x, -h or --graph reaches the verb: each recorded from the built CLI, none the parser's refusal")
    func sweep() async {
        var lines: [[String]] = []
        for name in NameVerdicts.names {
            let runner = RuleRunner()
            let settings = model(runner)
            await settings.connect(binary: Self.binary, version: "0.2.2")
            await settings.allowHost(name)
            await settings.forbidHost(name)
            await settings.setPermission(name, "ask")
            await settings.removeKey(for: name)
            await settings.setDefaultVideo(name)
            await settings.setConfig(name, "1")
            lines += NameVerdicts.lines(ofCalls: runner.calls, separator: " ")
            lines += [
                SettingsViewModel.addKeyArguments(name),
                Argv.line(["providers", "register"], names: [name]),
                Argv.adding(["--output-format", "json"], to: Argv.line(["providers", "register"], names: [name])),
                SettingsViewModel.Provider(provider: name, label: name, tier: "local", backend: "not registered", type: "llamacpp").registration(),
                Argv.line(["config", "get"], names: ["backends.\(name).server_tools"]),
                Argv.line(["config", "delete-mcp-server"], names: [name]),
            ]
        }
        let (missing, refused) = NameVerdicts.judged(lines)
        #expect(missing.isEmpty, "never recorded -- add these rows to tests/presentation/shell/Fixtures/argv-name-verdicts.txt and run scripts/refresh-argv-name-verdicts.sh <the built apogee>:\n\(missing.joined(separator: "\n"))")
        #expect(refused.isEmpty, "the parser refuses:\n\(refused.joined(separator: "\n"))")
    }
}

/// A runner that holds one command until released, answering every other
/// read and command as done and empty.
final class HoldingSettingsRunner: CommandRunning, @unchecked Sendable {
    private let holding: [String]
    private let lock = NSLock()
    private var waiter: CheckedContinuation<Void, Never>?
    private var isHeld = false

    init(holding: [String]) { self.holding = holding }

    var held: Bool { lock.withLock { isHeld } }

    func release() {
        let waiter = lock.withLock { () -> CheckedContinuation<Void, Never>? in
            let waiter = self.waiter
            self.waiter = nil
            return waiter
        }
        waiter?.resume()
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome { .couldNotStart("no read here") }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        if arguments == holding {
            await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
                lock.withLock {
                    waiter = continuation
                    isHeld = true
                }
            }
        }
        return .done(stdout: "")
    }
}
