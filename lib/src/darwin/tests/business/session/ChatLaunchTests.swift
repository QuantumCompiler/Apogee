import BusinessSession
import DataProtocol
import DataPreferences
import Foundation
import InfrastructureProcess
import Testing

@Suite("ChatLaunch")
struct ChatLaunchTests {
    let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    @Test("a chat with tools: both directions stream-json, then --tools")
    func withTools() {
        let command = ChatLaunch(tools: true).command(binary: binary)
        #expect(command.executable == binary)
        #expect(
            command.arguments == [
                "chat", "--output-format", "stream-json", "--input-format", "stream-json", "--tools",
            ])
    }

    @Test("tools are on unless asked otherwise, matching the CLI's interactive default")
    func toolsByDefault() {
        #expect(ChatLaunch().tools)
        #expect(ChatLaunch(tools: false).arguments == [
            "chat", "--output-format", "stream-json", "--input-format", "stream-json",
        ])
    }

    @Test("a saved conversation is carried on with --resume and its id, after the flags")
    func resume() {
        #expect(ChatLaunch(resume: "20261021-003843-0fc4").arguments == [
            "chat", "--output-format", "stream-json", "--input-format", "stream-json", "--tools",
            "--resume", "20261021-003843-0fc4",
        ])
        #expect(ChatLaunch().resume == nil)
    }

    @Test("the run settings are the flags a person would type; a private chat is --private, its fallback --no-recall")
    func settingsAndPrivate() {
        let base = ["chat", "--output-format", "stream-json", "--input-format", "stream-json", "--tools"]
        #expect(ChatLaunch(resume: "c1", settings: RunSettings(model: "claude", system: "Be brief.", think: "off", effort: "high")).arguments
            == base + ["--resume", "c1", "-m", "claude", "-s", "Be brief.", "--think", "off", "--effort", "high"])
        #expect(ChatLaunch(settings: RunSettings(effort: "auto", temperature: 0.2)).arguments == base + ["-t", "0.2"],
            "auto effort is the default; temperature is -t")
        #expect(ChatLaunch(privately: true).arguments == base + ["--private"])
        #expect(ChatLaunch(noRecall: true).arguments == base + ["--no-recall"])
    }

    @Test("temperature is a launch flag too: -t, on chat and on execute (the branch binary's `chat --help` and `execute --help`)")
    func temperatureAtLaunch() {
        let chat = ["chat", "--output-format", "stream-json", "--input-format", "stream-json", "--tools"]
        #expect(ChatLaunch(settings: RunSettings(temperature: 0.2)).arguments == chat + ["-t", "0.2"])
        #expect(ChatLaunch(resume: "c1", settings: RunSettings(model: "claude", effort: "high", temperature: 1)).arguments
            == chat + ["--resume", "c1", "-m", "claude", "--effort", "high", "-t", "1"], "after the other settings, as set orders them")
        let execute = ["execute", "--output-format", "stream-json", "--input-format", "stream-json", "--tools"]
        #expect(ChatLaunch(settings: RunSettings(temperature: 0.7), execute: ChatLaunch.Execute()).arguments
            == execute + ["-t", "0.7"])
    }

    @Test("the execute door: execute's word, the same two directions, then --suite and --orchestrate as chosen")
    func execute() {
        let base = ["execute", "--output-format", "stream-json", "--input-format", "stream-json", "--tools"]
        #expect(ChatLaunch(execute: ChatLaunch.Execute()).arguments == base, "left to the config: the default suite, else bare")
        #expect(ChatLaunch(execute: ChatLaunch.Execute(suite: "duo")).arguments == base + ["--suite", "duo"])
        #expect(ChatLaunch(execute: ChatLaunch.Execute(suite: "duo", orchestrate: true)).arguments
            == base + ["--suite", "duo", "--orchestrate"])
        #expect(ChatLaunch(settings: RunSettings(model: "helper"), execute: ChatLaunch.Execute()).arguments
            == base + ["-m", "helper"], "bare on one model")
        #expect(ChatLaunch(resume: "c1", execute: ChatLaunch.Execute(suite: "duo")).arguments
            == base + ["--resume", "c1", "--suite", "duo"], "paused and resumed, it reopens as it opened")
    }

    @Test("a chat's suite: --suite <name>, --suite off for none, --force only when forced; none named leaves it to the CLI (GUI 4i)")
    func chatSuite() {
        let head = ["chat", "--output-format", "stream-json", "--input-format", "stream-json", "--tools"]
        #expect(ChatLaunch().arguments == head, "no --suite: a new chat runs under models.default_suite, a resumed one the suite it had")
        #expect(ChatLaunch(suite: "pair").arguments == head + ["--suite", "pair"])
        #expect(ChatLaunch(suite: ChatLaunch.noSuite).arguments == head + ["--suite", "off"])
        #expect(ChatLaunch(suite: "vast", forceSuite: true).arguments == head + ["--suite", "vast", "--force"])
        #expect(
            ChatLaunch(resume: "20261026-065601-ab12", settings: RunSettings(model: "plain"), suite: "fast").arguments
                == head + ["--resume", "20261026-065601-ab12", "-m", "plain", "--suite", "fast"])
        #expect(ChatLaunch(suite: "pair").namedSuite == "pair")
    }

    @Test("a launch names its suite where its opening takes it: a chat's own, an execute session's door's")
    func namingTheSuite() {
        var chat = ChatLaunch(suite: "pair")
        chat.name(suite: "fast", force: true)
        #expect(chat.suite == "fast" && chat.forceSuite && chat.execute == nil)
        var execute = ChatLaunch(execute: ChatLaunch.Execute(suite: "coding"))
        execute.name(suite: "fast", force: false)
        #expect(execute.execute?.suite == "fast" && execute.suite == nil && execute.namedSuite == "fast")
        #expect(execute.arguments == ["execute", "--output-format", "stream-json", "--input-format", "stream-json", "--tools", "--suite", "fast"])
        #expect(ChatLaunch(execute: ChatLaunch.Execute()).namedSuite == nil, "the door left it to the config")
    }

    @Test("a launch's folder is where its child works: a project for execute")
    func folder() {
        let project = URL(fileURLWithPath: "/Users/someone/code/ingest")
        let launch = ChatLaunch(execute: ChatLaunch.Execute(), folder: project)
        #expect(launch.command(binary: binary).workingDirectory == project)
        #expect(ChatLaunch().command(binary: binary).workingDirectory == nil, "the client's own default, the home folder")
    }

    @Test("a client starts a chat in the user's home unless given a folder, and says hello first")
    func clientStartsInHome() async throws {
        let fake = try FakeApogee()
        let home = fake.directory.appendingPathComponent("home")
        try FileManager.default.createDirectory(at: home, withIntermediateDirectories: true)
        let client = ApogeeClient(
            identity: ClientIdentity(version: "0.2.0"), spawner: ProcessSpawner(),
            preferences: Preferences(store: MemoryStore()), home: home)
        let cwd = fake.directory.appendingPathComponent("cwd")
        let session = client.startChat(
            binary: fake.executable,
            environment: ["FAKE_SCENARIO": "conversation", "FAKE_LOG": fake.log.path, "FAKE_CWD": cwd.path])
        var reader = EventReader(session)
        _ = await reader.untilKind { if case .session = $0 { true } else { false } }
        await session.close()
        _ = await reader.untilEnded()
        #expect(fake.received.first == #"{"type":"hello","client":{"name":"Apogee","version":"0.2.0"},"wants":["tools"]}"#)
        // Both sides through realpath(3): Foundation's own resolving drops /private.
        let physical = try #require(realpath(home.path, nil))
        defer { free(physical) }
        #expect(try String(contentsOf: cwd, encoding: .utf8) == String(cString: physical))
    }

    @Test("a client opens a launch in the launch's folder: an execute door's project")
    func clientStartsInTheLaunchFolder() async throws {
        let fake = try FakeApogee()
        let home = fake.directory.appendingPathComponent("home")
        let project = fake.directory.appendingPathComponent("project")
        for folder in [home, project] {
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        }
        let client = ApogeeClient(
            identity: ClientIdentity(version: "0.2.0"), spawner: ProcessSpawner(),
            preferences: Preferences(store: MemoryStore()), home: home)
        let cwd = fake.directory.appendingPathComponent("cwd")
        let session = client.startChat(
            binary: fake.executable, launch: ChatLaunch(execute: ChatLaunch.Execute(), folder: project),
            environment: ["FAKE_SCENARIO": "conversation", "FAKE_LOG": fake.log.path, "FAKE_CWD": cwd.path])
        var reader = EventReader(session)
        _ = await reader.untilKind { if case .session = $0 { true } else { false } }
        await session.close()
        _ = await reader.untilEnded()
        let physical = try #require(realpath(project.path, nil))
        defer { free(physical) }
        #expect(try String(contentsOf: cwd, encoding: .utf8) == String(cString: physical))
    }
}
