import BusinessJobs
import BusinessReads
import BusinessSession
import Darwin
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import PresentationSession
import Testing

@testable import PresentationApp

/// A home of a test's own holding a fake `apogee`: `version` answered as a
/// release, a chat that reads until its input closes, and every other command
/// a long run that works until it is stopped. Real processes, never the
/// installed binary.
struct FakeApogeeHome {
    let url: URL
    let apogee: URL

    init() throws {
        url = FileManager.default.temporaryDirectory
            .appendingPathComponent("apogee-children-\(UUID().uuidString)").resolvingSymlinksInPath()
        apogee = url.appendingPathComponent(".local/bin/apogee")
        try FileManager.default.createDirectory(at: apogee.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Self.script.write(to: apogee, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: apogee.path)
    }

    static let script = #"""
        #!/bin/sh
        case "$1" in
            version) echo "apogee 0.2.0 (4c0ffee, 2026-10-25, macos-arm64)" ;;
            chat | execute) exec /bin/cat >/dev/null ;;
            *) exec /bin/sleep 30 ;;
        esac
        """#

    func remove() {
        try? FileManager.default.removeItem(at: url)
    }
}

/// Answers reads by their longest matching prefix; anything else fails, as a
/// read with no binary behind it would.
final class AppRunner: CommandRunning, @unchecked Sendable {
    private let lock = NSLock()
    private var reads: [([String], String)] = []

    func reading(_ prefix: [String], _ json: String) {
        lock.withLock { reads.append((prefix, json)) }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        let text = lock.withLock { reads.filter { arguments.starts(with: $0.0) }.max { $0.0.count < $1.0.count }?.1 }
        return text.flatMap(ReadDocument.init(text:)).map(ReadOutcome.document) ?? .couldNotStart("no scripted read for \(arguments)")
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        .done(stdout: "")
    }
}

/// A recorded read from `tests/business/reads/Fixtures/`.
func recorded(_ name: String) throws -> String {
    let tests = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
    return try String(contentsOf: tests.appendingPathComponent("business/reads/Fixtures/\(name).json"), encoding: .utf8)
}

/// 4c, the fifth defect: the system monitor lists every child the app runs,
/// from one registry its supervisors fill, never a screen's own list.
@Suite("The children the monitor sees", .timeLimit(.minutes(1)))
@MainActor
struct ChildrenSeenTests {
    /// The app over `home` and `registry`: the session's client and every
    /// place's runs real processes of the fake, read documents scripted.
    func composed(_ home: FakeApogeeHome, _ registry: ChildRegistry, route: String = "sessions") throws -> AppViewModel {
        let runner = AppRunner()
        runner.reading(["symphonies", "list"], try recorded("symphonies-list"))
        // A store job reads the store before it runs, and runs nothing when
        // that read fails (GUI 4e).
        runner.reading(["models", "list", "--all"], try recorded("store-list-all"))
        let preferences = Preferences(store: MemoryStore(["route": route]))
        return AppViewModel(
            info: nil, preferences: preferences,
            starter: ApogeeClient(
                identity: ClientIdentity(version: "test"), spawner: ProcessSpawner(), preferences: preferences,
                home: home.url, children: registry),
            runner: runner, jobs: JobStarter(spawner: ProcessSpawner(), folder: home.url, registry: registry),
            children: registry, home: home.url)
    }

    /// Starts one run in every place that runs them, through its own
    /// public acts, as a person would from its screen -- the store's long
    /// runs included, which Models hands to its Jobs tab: what each is
    /// listed as, in order.
    func startEveryPlacesRun(_ app: AppViewModel, _ home: FakeApogeeHome) throws -> [String] {
        app.downloads.reference = "org/model"
        app.downloads.start()
        app.tasks.form.goal = "say hello"
        app.tasks.start()
        app.console.input = "apogee models list"
        app.console.run()
        app.graph.build(name: "app", folder: home.url)
        app.records.newCapture()
        app.records.captureForm.text = "Ada: drop it? Bob: yes"
        app.records.capture()
        app.collections.newIngest(into: "notes")
        app.collections.ingestForm.path = "notes"
        app.collections.ingest()
        app.training.prepare("heat-notes.jsonl")
        app.symphonies.select(try #require(app.symphonies.symphonies.first { $0.name == "critique-revise" }))
        app.symphonies.input = "A draft README for the tool."
        app.symphonies.playIt()
        app.review.form.ref = "acme/widget#12"
        app.review.review()
        app.settings.fix(["check", "--fix"])
        app.video.newClip()
        app.video.form.prompt = "A lighthouse at dusk"
        app.video.generate()
        app.compose.newComposition()
        app.compose.form.brief = "Tides, explained"
        app.compose.compose()
        app.voice.text = "Hello there."
        app.voice.speak()
        // A store job (Stored weights' Check files, on Jobs) starts once its
        // read of the store has answered: last.
        app.models.checkFiles?("org--model/gguf/9f8e")
        return [
            "apogee models pull org/model",
            "apogee task run say hello",
            "apogee models list",
            "apogee graph build app",
            "apogee knowledge capture Ada: drop it? Bob: yes",
            "apogee embed ingest notes notes",
            "apogee datasets prepare heat-notes.jsonl",
            "apogee symphonies play critique-revise",
            "apogee review mr acme/widget#12",
            "apogee check",
            "apogee video generate A lighthouse at dusk",
            "apogee video compose Tides, explained",
            "apogee voice say Hello there.",
            "apogee models repair org--model/gguf/9f8e",
        ]
    }

    @Test("every place's run and the session's child are the monitor's children, each under the command it ran, until each ends")
    func everyScreensJob() async throws {
        let home = try FakeApogeeHome()
        defer { home.remove() }
        let registry = ChildRegistry()
        let app = try composed(home, registry)
        await app.start()
        #expect(await eventually { registry.children(.session).count == 1 }, "the session's child")
        let labels = try startEveryPlacesRun(app, home)
        #expect(await eventually { registry.children(.job).count == labels.count }, "listed: \(registry.children.map(\.label))")
        #expect(registry.children(.job).map(\.label) == labels)
        #expect(registry.children(.session).map(\.label) == ["Session: starting"], "the fake never says its model")
        let seen = app.monitor.liveChildren()
        #expect(seen.map(\.label) == registry.children.map(\.label), "the monitor reads the registry")
        #expect(seen.map(\.pid) == registry.children.map(\.processIdentifier))
        #expect(seen.allSatisfy { kill($0.pid, 0) == 0 }, "each a live child")
        #expect(seen.first?.label == "Session: starting", "the session started first")

        // Each run's Stop -- here the registry's, as quit asks it -- ends
        // it, and it leaves; the session leaves at its end.
        #expect(await registry.stop(.job, grace: .seconds(3)))
        #expect(registry.children(.job).isEmpty)
        await app.sessions.shutDown()
        #expect(await eventually { registry.children.isEmpty })
        #expect(app.monitor.liveChildren().isEmpty)
    }

    /// 4j: a pipeline run and a cycle pass are long acts like any other --
    /// supervised on the jobs core, so the registry lists them.
    @Test("a pipeline run and a cycle pass are the monitor's children, under their commands, each ended by its Stop")
    func pipelineAndCycleSeen() async throws {
        let home = try FakeApogeeHome()
        defer { home.remove() }
        let registry = ChildRegistry()
        let app = try composed(home, registry, route: "training")
        await app.training.connect(binary: home.apogee)
        app.training.pending = .runPipeline("skills")
        await app.training.confirm()
        #expect(await eventually { registry.children(.job).map(\.label) == ["apogee train pipeline run"] })
        #expect(app.monitor.liveChildren().map(\.label) == ["apogee train pipeline run"])
        await app.training.stop()
        #expect(await eventually { registry.children.isEmpty && !app.training.isRunning })
        app.training.pending = .runCycle
        await app.training.confirm()
        #expect(await eventually { registry.children(.job).map(\.label) == ["apogee train cycle run"] })
        await app.training.stop()
        #expect(await eventually { registry.children.isEmpty && !app.training.isRunning })
    }

    @Test("a streamed datasets pull is seen under its command and its Stop reaches it (GUI M14)")
    func pullSeen() async throws {
        let home = try FakeApogeeHome()
        defer { home.remove() }
        let registry = ChildRegistry()
        let app = try composed(home, registry, route: "training")
        await app.training.connect(binary: home.apogee)
        // The branch binary's vocabulary: the pull asks for its stream.
        app.training.vocabulary = { "2026-10-26" }
        app.training.newPull()
        app.training.pullRef = "someone/heat-notes"
        app.training.pull()
        #expect(await eventually { registry.children(.job).map(\.label) == ["apogee datasets pull someone/heat-notes"] })
        #expect(app.monitor.liveChildren().map(\.label) == ["apogee datasets pull someone/heat-notes"])
        await app.training.stop()
        #expect(await eventually { registry.children.isEmpty && !app.training.isRunning })
    }

    @Test("composed with only a registry, the app's own starters list in it")
    func defaultStartersList() async throws {
        let home = try FakeApogeeHome()
        defer { home.remove() }
        let registry = ChildRegistry()
        let app = AppViewModel(
            info: nil, preferences: Preferences(store: MemoryStore(["route": "voice"])), runner: AppRunner(),
            children: registry, home: home.url)
        app.console.connect(binary: home.apogee)
        app.console.input = "apogee models list"
        app.console.run()
        #expect(await eventually { app.monitor.liveChildren().map(\.label) == ["apogee models list"] })
        #expect(await registry.stop(.job, grace: .seconds(3)))
        #expect(app.monitor.liveChildren().isEmpty)
    }

    @Test("quit stops the running jobs -- the session idle -- within the session's grace, and says so")
    func quitStopsTheJobs() async throws {
        let home = try FakeApogeeHome()
        defer { home.remove() }
        let registry = ChildRegistry()
        let app = try composed(home, registry, route: "voice")
        app.console.connect(binary: home.apogee)
        app.console.input = "apogee models list"
        app.console.run()
        await app.graph.connect(binary: home.apogee)
        app.graph.build(name: "app", folder: home.url)
        #expect(await eventually { registry.children(.job).count == 2 })
        var lines: [String] = []
        var replies: [Bool] = []
        app.termination.log = { lines.append($0) }
        let clock = ContinuousClock()
        let asked = clock.now
        #expect(app.termination.ask { replies.append($0) } == .later)
        #expect(await eventually { replies == [true] })
        #expect(clock.now - asked < SessionViewModel.grace)
        #expect(registry.children.isEmpty)
        #expect(lines == [
            "quit: stopping the 2 running jobs first -- interrupted as Ctrl-C does, terminated after 3 s and killed 2 s later",
            "quit: the jobs have stopped; quitting",
        ])
    }

    /// 4c, the third defect, through the composition: the launch's
    /// discovery, the toast, and Settings › General.
    @Test("an override passed over at launch is said once, and Settings › General is told it with its reason")
    func settingsToldOfTheOverride() async throws {
        let home = try FakeApogeeHome()
        defer { home.remove() }
        let mine = home.url.appendingPathComponent("tools/apogee")
        try FileManager.default.createDirectory(at: mine.deletingLastPathComponent(), withIntermediateDirectories: true)
        try "#!/bin/sh\necho 'error: broken install' >&2\nexit 1\n".write(to: mine, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: mine.path)
        let registry = ChildRegistry()
        let preferences = Preferences(store: MemoryStore(["route": "voice"]))
        preferences.binaryOverride = mine
        let app = AppViewModel(
            info: nil, preferences: preferences,
            starter: ApogeeClient(
                identity: ClientIdentity(version: "test"), spawner: ProcessSpawner(), preferences: preferences,
                home: home.url, children: registry),
            runner: AppRunner(), jobs: JobStarter(spawner: ProcessSpawner(), folder: home.url, registry: registry),
            children: registry, home: home.url)
        var said: [String] = []
        app.sessions.say = { said.append($0) }
        await app.start()
        #expect(app.settings.binary == home.apogee.path, "the release answered")
        #expect(app.settings.skippedOverrideLine
            == "Not using \(mine.path): `version` exited with status 1: error: broken install. This launch runs the one above.")
        #expect(app.settings.pendingOverride == nil)
        #expect(said == ["Not using \(mine.path): `version` exited with status 1: error: broken install -- running \(home.apogee.path) instead"])
    }
}
