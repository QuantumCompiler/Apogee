import BusinessReads
import DataProtocol
import Foundation
import Testing

/// GUI M4: the one answer to "does this row chat", which every place that
/// offers a backend to chat with or to train asks -- from the row's MLX kind
/// as the binary states it, never its name -- and the backends those places
/// offer, over `models list` documents recorded from the binary.
@Suite("RowsThatChat")
struct RowsThatChatTests {
    @Test("over 3a's registered kinds: the chat and vision entries chat, the embedding entry does not; no stored set is a backend")
    func registeredKinds() {
        let rows = ModelRow.list(from: ModelsTests.store("store-list-kinds-registered"))
        let chats = Dictionary(uniqueKeysWithValues: rows.filter { !$0.isStored }.map { ($0.backend, $0.chats) })
        #expect(chats == ["Qwen3-8B-4bit": true, "Gemma4-E4B-4bit": true, "EmbeddingGemma-300M-bf16": false])
        #expect(ModelRow.chatBackends(rows) == ["Gemma4-E4B-4bit", "Qwen3-8B-4bit"], "in the list's order")
    }

    @Test("an mlx entry written by hand over a drafter, a video model, a voice model or a kind this app does not know: none chats")
    func entriesByHand() {
        // The CLI's `config add-backend` refuses each of the four, --force or
        // not; written into the config by hand, `models list` names each with
        // its kind.
        let rows = ModelRow.list(from: ModelsTests.store("store-list-kinds-by-hand"))
        let kinds = Dictionary(uniqueKeysWithValues: rows.map { ($0.backend, $0.mlxKind) })
        #expect(kinds == [
            "Gemma4-E4B-Assistant": .drafter, "Wan2.2-TI2V-5B": .video, "Kokoro-82M": .voice,
            "MusicGen-Small": .other("music"), "Qwen3-8B-4bit": .chat,
        ])
        #expect(rows.allSatisfy { !$0.isStored && $0.backendType == "mlx" })
        #expect(rows.filter(\.chats).map(\.backend) == ["Qwen3-8B-4bit"], "a word this app does not know is read strictly, as 3a reads it")
        #expect(ModelRow.chatBackends(rows) == ["Qwen3-8B-4bit"])
    }

    @Test("a row with no kind chats, as every row did before kinds: every row of a binary before the CLI's 53e, and every non-MLX backend")
    func rowsWithNoKind() {
        let older = ModelRow.list(from: recorded("list"))
        #expect(older.allSatisfy { $0.mlxKind == nil && $0.chats }, "its MLX set read as a chat model's")
        #expect(ModelRow.chatBackends(older) == ["mock-chat"], "the configured backends; the stored sets' placeholder is none")
        let store = ModelRow.list(from: ModelsTests.store("store-list"))
        #expect(store.allSatisfy { $0.mlxKind == nil && $0.chats })
        #expect(ModelRow.chatBackends(store) == ["llama3.2-1b-Q4KM"])
        let roster = ModelRow.list(from: ModelsTests.store("models-list-server-tools"))
        #expect(roster.allSatisfy { $0.mlxKind == nil })
        #expect(ModelRow.chatBackends(roster) == roster.map(\.backend), "cloud and subscription entries and a roster's models, every one")
        #expect(ModelRow.chatBackends(roster).count == 16)
    }

    @Test("each kind's word: chat and vision chat, the rest -- and a word this app does not know -- do not")
    func eachKind() {
        func row(_ kind: String?) -> ModelRow {
            let entry = kind.map { #","kind":"\#($0)""# } ?? ""
            let text = #"{"object":"list","data":[{"backend":"entry","backend_type":"mlx","format":"mlx","group":"mlx","model":"M/mlx/aaaaaaaaaaaa"\#(entry)}]}"#
            return ModelRow.list(from: ReadDocument(text: text)!)[0]
        }
        #expect(row("chat").chats && row("vision").chats && row(nil).chats)
        #expect(["embedding", "drafter", "video", "voice", "music"].allSatisfy { !row($0).chats })
    }
}
