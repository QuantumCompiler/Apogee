import BusinessSession
import DataDiscovery
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

/// The installed binary, when this machine has one: the release channel's,
/// else the dev or test channel's.
private let installed: URL? = ["apogee", "apogee-dev", "apogee-test"]
    .map { FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".local/bin/\($0)") }
    .first { FileManager.default.isExecutableFile(atPath: $0.path) }

/// The one pass against a real `apogee`: the mock-actor conversation -- an
/// ask, a gated write, a streamed answer -- on the CLI's scripted mock
/// backend, in a throwaway `APOGEE_HOME`, never `~/.apogee`. No model, no
/// network. Skipped, and said, on a machine with no install.
@Suite(
    "The real binary",
    .enabled(if: installed != nil, "no apogee in ~/.local/bin: the real-binary pass is skipped"),
    .timeLimit(.minutes(1)))
struct RealBinaryTests {
    @Test("discovery finds the installed binary on this machine, verified by its version line")
    func discoveryOnThisMachine() async throws {
        let binary = try #require(installed)
        guard case .found(let found, _) = await BinaryDiscovery(spawner: ProcessSpawner()).locate(override: nil) else {
            Issue.record("discovery did not find \(binary.path)")
            return
        }
        #expect(found.url == binary)
        #expect(found.versionLine.hasPrefix("apogee "))
    }

    @Test("the core drives the installed binary through the mock-actor conversation")
    func mockActor() async throws {
        let binary = try #require(installed)
        let sandbox = FileManager.default.temporaryDirectory.appendingPathComponent("apogee-real-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: sandbox) }
        let home = sandbox.appendingPathComponent("home")
        let work = sandbox.appendingPathComponent("work")
        try FileManager.default.createDirectory(at: home, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: work, withIntermediateDirectories: true)
        let script = home.appendingPathComponent("actor.json")
        try #"""
            {"turns":[
             {"reasoning":"They want a note written. I should ask what to call it.","text":"","tool_calls":[{"name":"ask_user","arguments":{"questions":[{"header":"Name","question":"What should the note be called?","multi_select":false,"options":[{"label":"todo.md","description":"A to-do list"},{"label":"ideas.md","description":"Loose ideas"}]}]}}]},
             {"text":"","tool_calls":[{"name":"write_file","arguments":{"path":"todo.md","content":"- buy milk\n"}}]},
             {"text":"I wrote todo.md with one item: buy milk.","delay_ms":5}
            ]}
            """#.write(to: script, atomically: true, encoding: .utf8)
        let environment = ["APOGEE_HOME": home.path]
        for arguments in [
            ["config", "init"], ["config", "add-backend", "actor", "--type", "mock", "--model-path", script.path],
            ["config", "set-default", "actor"],
        ] {
            let run = try await ProcessSpawner().capture(
                ChildCommand(executable: binary, arguments: arguments, environment: environment), timeout: .seconds(30))
            try #require(run.exit.succeeded, "apogee \(arguments.joined(separator: " ")): \(run.stderrTail.text)")
        }

        var command = ChatLaunch(tools: true).command(binary: binary)
        command.environment = environment
        command.workingDirectory = work
        let session = MachineSession.start(
            spawner: ProcessSpawner(), command: command,
            hello: .hello(clientName: "Apogee", clientVersion: "test", wants: ["tools"]))
        var reader = EventReader(session)
        try await session.send(user: "Write me a note.")
        var answered = 0
        while true {
            let read = await reader.untilKind {
                switch $0 {
                case .question, .result, .error: true
                default: false
                }
            }
            guard case .event(let last) = read.last else {
                Issue.record("the session ended mid-turn")
                return
            }
            if case .question(let question) = last.kind {
                try await session.answer(question.isPermission ? "yes" : "todo.md")
                answered += 1
                continue
            }
            guard case .result(let result) = last.kind else {
                Issue.record("the turn failed: \(last.fields)")
                return
            }
            #expect(result.text == "I wrote todo.md with one item: buy milk.")
            break
        }
        #expect(answered == 2)
        #expect(try String(contentsOf: work.appendingPathComponent("todo.md"), encoding: .utf8) == "- buy milk\n")
        await session.close()
        #expect(await reader.untilEnded()?.reason == .finished)
    }
}
