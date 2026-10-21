import BusinessJobs
import BusinessReads
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization
import Testing

@testable import PresentationSettings

/// A runner that records every command and the input it was given.
final class RecordingRunner: CommandRunning, @unchecked Sendable {
    private let reads = Mutex<[([String], String)]>([])
    private let log = Mutex<[String]>([])
    private let inputs = Mutex<[Data]>([])

    var calls: [String] { log.withLock { $0 } }
    var given: [Data] { inputs.withLock { $0 } }

    func reading(_ prefix: [String], _ json: String) { reads.withLock { $0.insert((prefix, json), at: 0) } }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        log.withLock { $0.append("read " + arguments.joined(separator: " ")) }
        return reads.withLock { rules in rules.first { arguments.starts(with: $0.0) } }.map { .document(ReadDocument(text: $0.1)!) }
            ?? .couldNotStart("no scripted read")
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        await command(binary: binary, arguments, input: nil)
    }

    func command(binary: URL, _ arguments: [String], input: Data?) async -> CommandOutcome {
        log.withLock { $0.append("run " + arguments.joined(separator: " ")) }
        if let input { inputs.withLock { $0.append(input) } }
        if arguments == ["config", "get", "permissions"] { return .done(stdout: "run_command: ask\nwrite_file: allow\n") }
        if arguments.starts(with: ["config", "get"]) {
            return .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: BoundedTail())
        }
        return .done(stdout: "done: \(arguments.joined(separator: " "))\n")
    }
}

final class NoJobs: JobStarting, @unchecked Sendable {
    private let started = Mutex<[[String]]>([])
    var starts: [[String]] { started.withLock { $0 } }
    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        started.withLock { $0.append(arguments) }
        return Finished()
    }
}

final class Finished: JobRunning, @unchecked Sendable {
    let updates: AsyncStream<JobUpdate>
    init() {
        let (s, c) = AsyncStream.makeStream(of: JobUpdate.self)
        updates = s
        c.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 0), stderrTail: BoundedTail(), interrupted: false)))
        c.finish()
    }
    func stop(grace: Duration) async {}
}

@Suite("SettingsViewModel", .timeLimit(.minutes(1)))
@MainActor
struct SettingsViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func runner() -> RecordingRunner {
        let runner = RecordingRunner()
        runner.reading(["models", "list"], #"{"object":"list","data":[{"backend":"local","backend_type":"llamacpp","state":"ready"},{"backend":"wan","backend_type":"","format":"pipeline"}]}"#)
        runner.reading(["auth", "list"], #"{"object":"list","data":[{"provider":"anthropic","source":"keychain"}],"backends":[]}"#)
        runner.reading(["providers", "scan"], #"{"object":"list","data":[{"provider":"claude","label":"Claude CLI","tier":"credentials found","backend":"not registered","type":"claude-cli"}]}"#)
        runner.reading(["check"], #"{"rows":[{"section":"Config","name":"config.json","status":"ok","detail":"parses cleanly"},{"section":"Runtimes","name":"llama-server","status":"fail","detail":"not installed","remedy":"apogee check --fix"}],"ok":false,"failures":1,"warnings":0}"#)
        return runner
    }

    @Test("every pane reads its verbs: backends, keys (names only), providers, health with remedies, gate levels, defaults")
    func reads() async {
        let runner = runner()
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        await model.connect(binary: Self.binary, version: "0.1.9")
        #expect(model.backends.map(\.backend) == ["local"])
        #expect(model.pipelines == ["wan"])
        #expect(model.keyed == ["anthropic"])
        #expect(model.providers.map(\.label) == ["Claude CLI"])
        #expect(model.checks.count == 2 && model.checks[1].remedy == "apogee check --fix")
        #expect(model.checkSummary == "1 failing, 0 to look at")
        #expect(model.permissions.map(\.tool) == ["run_command", "write_file"])
        #expect(model.permissions.map(\.level) == ["ask", "allow"])
        #expect(model.webSearch == nil && model.recall == nil, "unset keys are the defaults, said so")
        #expect(runner.calls.contains("run config get tools.search.url"))
    }

    @Test("a key goes on auth add's standard input, never its arguments, and nothing in the model keeps it")
    func keyRetainedNowhere() async {
        let runner = runner()
        let store = MemoryStore()
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: store))
        await model.connect(binary: Self.binary, version: nil)
        let secret = "sk-ant-THIS-IS-THE-SECRET"
        await model.addKey("  \(secret) ", for: "anthropic")
        #expect(runner.calls.contains("run auth add anthropic --stdin"))
        #expect(!runner.calls.contains { $0.contains(secret) }, "never on a command line")
        #expect(runner.given == [Data("\(secret)\n".utf8)])
        let kept = String(describing: Mirror(reflecting: model).children.map { "\($0.label ?? ""): \($0.value)" })
        #expect(!kept.contains(secret), "no property of the view-model holds it")
        #expect(!(["binaryOverride", "theme", "textSize"].compactMap { store.string(forKey: $0) }.contains { $0.contains(secret) }))
        await model.addKey("x", for: "not-a-provider")
        #expect(runner.given.count == 1, "only the providers auth add takes")
    }

    @Test("each change is its verb, read back")
    func changes() async {
        let runner = runner()
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        await model.connect(binary: Self.binary, version: nil)
        await model.setPermission("run_command", "deny")
        #expect(runner.calls.contains("run config set-permission run_command deny"))
        await model.allowHost(" docs.python.org ")
        #expect(runner.calls.contains("run config add-allowed-host docs.python.org"))
        await model.forbidHost("docs.python.org")
        #expect(runner.calls.contains("run config delete-allowed-host docs.python.org"))
        await model.setDefaultVideo(nil)
        #expect(runner.calls.contains("run config set-default-video off"))
        await model.removeKey(for: "anthropic")
        #expect(runner.calls.contains("run auth clear anthropic"))
        #expect(runner.calls.last?.hasPrefix("run config get") == true, "read back")
    }

    @Test("before 52e the config's plain keys are read-only and forgetting recall is not offered; with it, they are set")
    func track52() async {
        let runner = runner()
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        await model.connect(binary: Self.binary, version: nil)
        await model.setConfig("memory.recall", "false")
        await model.forgetRecall()
        #expect(!runner.calls.contains { $0.hasPrefix("run config set ") || $0.hasPrefix("run chats recall") })
        model.hasTrack52 = { true }
        await model.setConfig("memory.recall", "false")
        await model.forgetRecall()
        #expect(runner.calls.contains("run config set memory.recall false"))
        #expect(runner.calls.contains("run chats recall --forget"))
    }

    @Test("check --fix and bootstrap run as supervised jobs, and the health is read again")
    func fixes() async {
        let jobs = NoJobs()
        let model = SettingsViewModel(runner: runner(), starter: jobs, preferences: Preferences(store: MemoryStore()))
        await model.connect(binary: Self.binary, version: nil)
        model.fix(["check", "--fix"])
        #expect(jobs.starts == [["check", "--fix"]])
    }

    @Test("the theme and text size are the app's own preferences, the design's three themes or the system's")
    func theme() {
        let store = MemoryStore()
        let settings = ThemeSettings(preferences: Preferences(store: store))
        #expect(settings.choice == .system && settings.size == .medium)
        settings.choice = .contrast
        settings.size = .large
        let again = ThemeSettings(preferences: Preferences(store: store))
        #expect(again.choice == .contrast && again.size == .large)
        #expect(ThemeSettings.Choice.allCases.map(\.title) == ["Follow the system", "Graphite Light", "Graphite Dark", "High contrast"])
        #expect(ThemeSettings.Size.large.factor > 1 && ThemeSettings.Size.small.factor < 1)
    }

    @Test("the keyboard list is the design's own")
    func keyboard() {
        #expect(SettingsViewModel.Pane.allCases.map(\.title) == ["General", "Appearance", "Backends & keys", "Video & voice", "Permissions"])
    }

    @Test("a key provider reads as its company writes it; any other as the CLI says it")
    func providerNames() {
        #expect(SettingsViewModel.keyProviders.map(SettingsViewModel.providerName) == ["Anthropic", "OpenAI", "Google"])
        #expect(SettingsViewModel.providerName("mistral") == "mistral")
    }
}
