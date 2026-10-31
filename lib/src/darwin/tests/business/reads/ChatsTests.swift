import BusinessReads
import DataProtocol
import Foundation
import Testing

@Suite("Chats")
struct ChatsTests {
    @Test("a chats list row: id, name, the updated stamp, turns -- and no backend before the CLI's 52a")
    func beforeTheBackendFields() throws {
        let document = try #require(ReadDocument(text: #"{"object":"list","data":[{"id":"20261021-003843-0fc4","name":"Plans for the week","turns":2,"updated":"2026-10-21T00:38:54Z"},{"name":"no id"},{"id":"20261020-1","name":"","turns":1,"updated":"not a date"}]}"#))
        let chats = ChatSummary.list(from: document)
        #expect(chats.map(\.id) == ["20261021-003843-0fc4", "20261020-1"], "a row with no id is skipped")
        #expect(chats[0].name == "Plans for the week")
        #expect(chats[0].turns == 2)
        #expect(chats[0].updated == Date(timeIntervalSince1970: 1_792_543_134))
        #expect(chats[0].backend == nil && chats[0].model == nil)
        #expect(chats[1].name == "20261020-1", "an empty name shows the id")
        #expect(chats[1].updated == nil, "a stamp that does not parse is unknown, not guessed")
    }

    @Test("since 52a a row names its backend and the roster model pinned there; an empty one is none")
    func backendFields() throws {
        let document = try #require(ReadDocument(text: #"{"object":"list","data":[{"id":"a","name":"a","turns":1,"updated":"2026-10-20T10:00:00Z","backend":"claude","model":"claude-sonnet-5"},{"id":"b","name":"b","turns":1,"updated":"2026-10-20T10:00:00Z","backend":"local","model":""}]}"#))
        let chats = ChatSummary.list(from: document)
        #expect(chats[0].backend == "claude" && chats[0].model == "claude-sonnet-5")
        #expect(chats[1].backend == "local" && chats[1].model == nil)
    }

    @Test("a label: value record keeps its order and its words")
    func fields() throws {
        let document = try #require(ReadDocument(text: #"{"fields":[{"field":"chat","value":"actor"},{"field":"vision","value":"(unset -- the chat's own backend)"}]}"#))
        #expect(Field.list(from: document) == [Field("chat", "actor"), Field("vision", "(unset -- the chat's own backend)")])
    }

    @Test("a backend's kind comes from its type; one this build does not know is none")
    func kinds() throws {
        #expect(ModelKind(backendType: "llamacpp") == .local)
        #expect(ModelKind(backendType: "mlx") == .local)
        #expect(ModelKind(backendType: "ollama-cli") == .local)
        #expect(ModelKind(backendType: "anthropic") == .api)
        #expect(ModelKind(backendType: "openai") == .api)
        #expect(ModelKind(backendType: "google") == .api)
        #expect(ModelKind(backendType: "claude-cli") == .subscription)
        #expect(ModelKind(backendType: "codex-cli") == .subscription)
        #expect(ModelKind(backendType: "gemini-cli") == .subscription)
        #expect(ModelKind(backendType: "mock") == nil)
        #expect(ModelKind(backendType: "a-future-type") == nil)
        let models = try #require(ReadDocument(text: #"{"object":"list","data":[{"backend":"local","backend_type":"llamacpp"},{"backend":"claude","backend_type":"anthropic"},{"backend":"m","backend_type":"mock"}]}"#))
        #expect(ModelKind.byBackend(from: models) == ["local": .local, "claude": .api])
    }
}
