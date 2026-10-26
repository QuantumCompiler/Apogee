import BusinessReads
import Foundation
import Testing

@testable import PresentationModels

/// GUI M4: nothing the CLI lists is hidden from the places that show
/// backends. Over entries written into the config by hand over a drafter, a
/// video model, a voice model and a kind this app does not know -- recorded
/// from the binary -- the Backends tab lists each and Roles offers each to
/// every role but chat; Use in a new session and the chat role ask the one
/// answer, so only the entry that chats opens one or answers in sessions.
@Suite("BackendsKeepEveryEntry", .timeLimit(.minutes(1)))
@MainActor
struct BackendsKeepEveryEntryTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    static func recorded(_ name: String) -> String {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../../business/reads/Fixtures/\(name).json")
        return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
    }

    @Test("the Backends tab and Roles keep every entry written by hand; Use in a new session and the chat role only the one that chats")
    func everyEntryShown() async {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], Self.recorded("store-list-kinds-by-hand"))
        runner.reading(["models", "status"], Self.recorded("models-status-kinds-registered"))
        let model = ModelsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        let every = ["Gemma4-E4B-Assistant", "Kokoro-82M", "MusicGen-Small", "Qwen3-8B-4bit", "Wan2.2-TI2V-5B"]
        #expect(model.groups.flatMap(\.rows).map(\.backend).sorted() == every, "the Backends tab")
        #expect(model.count(.backends) == 5)
        #expect(model.backends.sorted() == every, "every entry Roles shows")
        for role in model.roles {
            #expect(model.choices(for: role).sorted() == (role.role == "chat" ? ["Qwen3-8B-4bit"] : every), "\(role.role)'s choices")
        }
        #expect(model.roles.count == 6)
        #expect(model.rows.filter { model.opensSessions($0) }.map(\.backend) == ["Qwen3-8B-4bit"])
        #expect(model.rows.allSatisfy { model.opensSessions($0) == $0.chats })
    }
}
