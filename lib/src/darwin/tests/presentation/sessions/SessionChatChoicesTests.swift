import BusinessReads
import DataPreferences
import DataProtocol
import Foundation
import PresentationSession
import Testing

@testable import PresentationSessions

/// GUI M4: the session settings sheet's configured models (`/model`, ⇧⌘M)
/// and the execute door's model offer only the backends a chat runs on.
/// Over 3a's registered kinds -- a chat, a vision and an embedding set, each
/// an `mlx` entry -- and over entries written into the config by hand over a
/// drafter, a video model, a voice model and a kind this app does not know,
/// both recorded from the binary; and over lists from a binary before the
/// CLI's 53e, which state no kind: every configured backend, as before.
@Suite("SessionChatChoices", .timeLimit(.minutes(1)))
@MainActor
struct SessionChatChoicesTests {
    /// A `models list` recorded from the binary (`tests/business/reads/Fixtures`).
    static func recorded(_ name: String) -> String {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../../business/reads/Fixtures/\(name).json")
        return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
    }

    /// The library started over `models`, as the app starts it: the list read
    /// once at launch, for the settings sheet and the door.
    func library(models: String) async -> SessionLibraryViewModel {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], #"{"object":"list","data":[]}"#)
        runner.reading(["models", "list"], models)
        runner.commanding(["config", "get", "suites"], .done(stdout: "duo\n"))
        runner.commanding(["config", "get", "models.default_suite"], .done(stdout: "\n"))
        let starter = ScriptedStarter(sessions: [ScriptedSession(), ScriptedSession(), ScriptedSession()])
        let library = SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: MemoryStore()))
        await library.start(inView: true)
        return library
    }

    @Test("the settings sheet offers the registered chat and vision entries, never the embedding entry -- and a new session keeps it so")
    func settingsSheetOverRegisteredKinds() async {
        let library = await library(models: Self.recorded("store-list-kinds-registered"))
        #expect(library.current.modelChoices == ["Gemma4-E4B-4bit", "Qwen3-8B-4bit"])
        #expect(!library.current.modelChoices.contains("EmbeddingGemma-300M-bf16"), "its chat the CLI refuses, naming the embedding role")
        await library.newSession()
        #expect(library.current.modelChoices == ["Gemma4-E4B-4bit", "Qwen3-8B-4bit"], "the next session's sheet offers the same")
    }

    @Test("the execute door offers the registered chat and vision entries, never the embedding entry")
    func executeDoorOverRegisteredKinds() async {
        let library = await library(models: Self.recorded("store-list-kinds-registered"))
        await library.openExecuteDoor()
        #expect(library.modelChoices == ["Gemma4-E4B-4bit", "Qwen3-8B-4bit"])
        #expect(!library.modelChoices.contains("EmbeddingGemma-300M-bf16"))
    }

    @Test("an mlx entry written by hand over a drafter, a video model, a voice model or a kind this app does not know is offered by neither")
    func entriesByHand() async {
        let library = await library(models: Self.recorded("store-list-kinds-by-hand"))
        #expect(library.current.modelChoices == ["Qwen3-8B-4bit"])
        await library.openExecuteDoor()
        #expect(library.modelChoices == ["Qwen3-8B-4bit"])
    }

    @Test("a list from a binary before the CLI's 53e says no kind: every configured backend is offered as before")
    func olderBinary() async {
        let stored = await library(models: Self.recorded("models-stored-list"))
        #expect(stored.current.modelChoices == ["mock-chat"], "its stored sets are no backend: the list's placeholder is not offered")
        #expect(stored.modelChoices == ["mock-chat"])
        let store = await library(models: Self.recorded("store-list"))
        #expect(store.current.modelChoices == ["llama3.2-1b-Q4KM"], "its MLX set, stored and kindless, is no backend either")
        #expect(store.modelChoices == ["llama3.2-1b-Q4KM"])
    }

    @Test("a row with no MLX kind -- a cloud, subscription, llamacpp or mock backend, a roster's model -- is offered as before")
    func rowsWithNoKind() async {
        let roster = await library(models: Self.recorded("models-list-server-tools"))
        #expect(roster.current.modelChoices == [
            "claude", "claude-api", "claude:claude-fable-5", "claude:claude-fable-5-1", "claude:claude-haiku-4-5",
            "claude:claude-haiku-5-5", "claude:claude-opus-4-5", "claude:claude-opus-4-6", "claude:claude-opus-4-7",
            "claude:claude-opus-4-8", "claude:claude-opus-5", "claude:claude-opus-5-5", "claude:claude-sonnet-4-6",
            "claude:claude-sonnet-5", "claude:claude-sonnet-5-5", "gpt",
        ])
        let suites = await library(models: Self.recorded("suites-models-list"))
        #expect(suites.modelChoices == ["claude", "fasthelper", "fastroot", "helper", "plain", "root", "scribe", "vast"])
    }
}
