import BusinessSession
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
}
