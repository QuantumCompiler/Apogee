import BusinessReads
import Foundation
import Testing

@testable import PresentationModels

/// GUI M4: Roles' chat row points the chat role (`config set-default`), so
/// its picker offers only the backends a chat runs on (`ModelRow.chats`).
/// Every other role's picker keeps every entry, the embedding role the
/// embedding entry among them; what Roles shows is unchanged, and a chat
/// role the config already points at an entry that answers no chat is shown
/// as it stands. Over documents recorded from the binary: 3a's registered
/// kinds, entries written into the config by hand over every kind that
/// answers no chat, and 3a's three entries with `config set-default` -- which
/// the CLI takes -- pointing the chat role at the embedding entry.
@Suite("RolesChatChoices", .timeLimit(.minutes(1)))
@MainActor
struct RolesChatChoicesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    static func recorded(_ name: String) -> String {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../../business/reads/Fixtures/\(name).json")
        return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
    }

    func models(list: String, status: String) async -> ModelsViewModel {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], Self.recorded(list))
        runner.reading(["models", "status"], Self.recorded(status))
        let model = ModelsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        return model
    }

    func role(_ name: String, in model: ModelsViewModel) throws -> RoleAssignment {
        try #require(model.roles.first { $0.role == name })
    }

    @Test("over the registered kinds the chat role offers the chat and vision entries, never the embedding entry; every other role offers all three")
    func chatRoleOverRegisteredKinds() async throws {
        let model = await models(list: "store-list-kinds-registered", status: "models-status-kinds-registered")
        let chat = try role("chat", in: model)
        #expect(model.choices(for: chat) == ["Gemma4-E4B-4bit", "Qwen3-8B-4bit"])
        let every = ["EmbeddingGemma-300M-bf16", "Gemma4-E4B-4bit", "Qwen3-8B-4bit"]
        #expect(model.choices(for: try role("embedding", in: model)) == every, "the embedding role still offers the embedding entry")
        for other in ["extraction", "vision", "transcription", "utility"] {
            #expect(model.choices(for: try role(other, in: model)) == every, "\(other): unchanged")
        }
        #expect(model.roles.map(\.role) == ["chat", "embedding", "extraction", "vision", "transcription", "utility"], "Roles shows every role")
        #expect(model.backends == every, "and every entry")
    }

    @Test("entries written by hand over a drafter, a video, a voice model or an unknown kind: never the chat role's; every other role's as before")
    func chatRoleOverEntriesByHand() async throws {
        let model = await models(list: "store-list-kinds-by-hand", status: "models-status-kinds-registered")
        let chat = try role("chat", in: model)
        #expect(model.choices(for: chat) == ["Qwen3-8B-4bit"])
        #expect(model.choices(for: try role("vision", in: model)) == model.backends)
        #expect(model.backends.count == 5)
    }

    @Test("a chat role the config already points at the embedding entry is shown as it stands: its picker keeps that entry, and offers no other that cannot chat")
    func chatRoleAlreadyOnTheEmbeddingEntry() async throws {
        let model = await models(list: "store-list-chat-on-embedding", status: "models-status-chat-on-embedding")
        let chat = try role("chat", in: model)
        #expect(chat.backend == "EmbeddingGemma-300M-bf16" && chat.isSet)
        #expect(chat.said == "EmbeddingGemma-300M-bf16")
        #expect(model.choices(for: chat) == ["EmbeddingGemma-300M-bf16", "Gemma4-E4B-4bit", "Qwen3-8B-4bit"], "its selection has its row")
        // The embedding role reaches it through models.default: not set on
        // its own, so it keeps every entry as every other role does.
        let embedding = try role("embedding", in: model)
        #expect(!embedding.isSet && embedding.backend == "EmbeddingGemma-300M-bf16")
        #expect(model.choices(for: embedding) == model.backends)
    }

    @Test("a list from a binary before the CLI's 53e says no kind: the chat role offers every backend, as before")
    func olderBinary() async throws {
        let stored = await models(list: "models-stored-list", status: "models-status-kinds-registered")
        #expect(stored.choices(for: try role("chat", in: stored)) == ["mock-chat"])
        let store = await models(list: "store-list", status: "models-status-kinds-registered")
        #expect(store.choices(for: try role("chat", in: store)) == ["llama3.2-1b-Q4KM"])
        // This suite's own list: an mlx backend that states no kind, beside
        // cloud, subscription and llamacpp ones.
        let model = await ModelsViewModelTests().connected(ScriptedRunner())
        #expect(model.choices(for: try role("chat", in: model)) == model.backends)
        #expect(model.backends == ["claude", "qwen3-8b-Q4KM", "qwen-mlx", "claude-code"])
    }
}
