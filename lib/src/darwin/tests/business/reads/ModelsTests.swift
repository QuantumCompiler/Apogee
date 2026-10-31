import BusinessReads
import DataProtocol
import Foundation
import Testing

/// A `models list` document recorded from the installed binary over a scratch
/// store (`Fixtures/models-stored-*.json`, paths made neutral): two
/// SafeTensors sets, an MLX model converted from a third -- which the default
/// list folds -- a pipeline, a voice model, and a configured mock backend.
func recorded(_ name: String) -> ReadDocument {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("Fixtures/models-stored-\(name).json")
    return ReadDocument(text: try! String(contentsOf: url, encoding: .utf8))!
}

@Suite("Models")
struct ModelsTests {
    @Test("stored weights are named by their handle, a configured backend by its own name; no two rows share an id")
    func storedByHandle() {
        let rows = ModelRow.list(from: recorded("list"))
        #expect(rows.map(\.name) == [
            "Llama3.2-1B-Instruct/safetensors/edaba290ce7f", "Qwen3-VL-8B-Instruct/safetensors/63f2634a2019",
            "Gemma4-26B-Instruct/mlx/a88b9f386aff", "Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6",
            "Kokoro-82M/voice/5f3a2c91d0e4", "mock-chat",
        ])
        #expect(Set(rows.map(\.id)).count == rows.count)
        #expect(rows.map(\.isStored) == [true, true, true, true, true, false])
        #expect(rows.allSatisfy { $0.backend != ModelRow.unconfigured || $0.isStored }, "the CLI's placeholder, as recorded")
        #expect(rows[0].renameName == "Llama3.2-1B-Instruct", "models rename takes stored weights' directory")
        #expect(rows[5].renameName == "mock-chat")
    }

    @Test("the fold is the list's own count; --all lists the consumed set and folds nothing")
    func folded() {
        #expect(ModelRow.folded(in: recorded("list")) == 1)
        let all = recorded("list-all")
        #expect(ModelRow.folded(in: all) == 0)
        #expect(ModelRow.list(from: all).first?.name == "Gemma4-26B-Instruct/safetensors/13de95725073")
    }

    /// A store document recorded from the binary built with the CLI's 53e-53m
    /// (`Fixtures/store-*.json`, the sandbox's home written `/Users/someone/.apogee`).
    static func store(_ name: String) -> ReadDocument {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name).json")
        return ReadDocument(text: try! String(contentsOf: url, encoding: .utf8))!
    }

    @Test("an MLX row carries its kind as the CLI wrote it -- a word it does not know kept as written -- and no other row has one")
    func rowsCarryTheirKind() {
        let rows = ModelRow.list(from: Self.store("store-list-kinds"))
        let kinds = Dictionary(uniqueKeysWithValues: rows.compactMap { row in row.mlxKind.map { (row.name, $0) } })
        #expect(kinds["Qwen3-8B/mlx/1a2b3c4d5e6f"] == .chat)
        #expect(kinds["Gemma4-E4B/mlx/2b3c4d5e6f70"] == .vision)
        #expect(kinds["EmbeddingGemma-300M/mlx/3c4d5e6f7081"] == .embedding)
        #expect(kinds["Gemma4-E4B-Assistant/mlx/4d5e6f708192"] == .drafter)
        #expect(kinds["Wan2.2-TI2V-5B/mlx/5e6f708192a3"] == .video)
        #expect(kinds["Kokoro-82M/mlx/6f708192a3b4"] == .voice)
        #expect(kinds["MusicGen-Small/mlx/708192a3b4c5"] == .other("music"))
        #expect(kinds["MusicGen-Small/mlx/708192a3b4c5"]?.word == "music", "shown as written")
        #expect(rows.count == 7 && kinds.count == 7)

        let registered = ModelRow.list(from: Self.store("store-list-kinds-registered"))
        let embedding = registered.first { $0.backend == "EmbeddingGemma-300M-bf16" }
        #expect(embedding?.mlxKind == .embedding && embedding?.isStored == false, "an mlx backend over a directory carries its kind too")
        #expect(embedding?.kind == .local, "where it runs is still its backend's type")

        // Before the CLI's 53e no row says a kind, and an MLX set reads as a
        // chat model's.
        let older = ModelRow.list(from: recorded("list"))
        #expect(older.allSatisfy { $0.mlxKind == nil })
        #expect(MLXKind.reading(nil) == .chat)
    }

    @Test("what each kind is for: chat and vision chat, an embedding model registers too, the rest no backend runs")
    func kindsAndWhatRunsThem() {
        #expect([MLXKind.chat, .vision].allSatisfy { $0.chats })
        #expect([MLXKind.embedding, .drafter, .video, .voice, .other("music")].allSatisfy { !$0.chats })
        #expect([MLXKind.chat, .vision, .embedding].allSatisfy { $0.registers })
        #expect([MLXKind.drafter, .video, .voice, .other("music")].allSatisfy { !$0.registers })
        for word in ["chat", "vision", "embedding", "drafter", "video", "voice", "something-later"] {
            #expect(MLXKind(word: word).word == word)
        }
    }

    @Test("models info's typed facts gain kind, vector_width and mlx_conversion where the CLI states them, nil where it does not")
    func factsOfTheKinds() {
        let embedding = ModelInfo(document: Self.store("store-info-kind-embedding")).data
        #expect(embedding?.kind == .embedding)
        #expect(embedding?.vectorWidth == 768)
        #expect(embedding?.mlxConversion == nil, "what a conversion made converts no further")
        #expect(ModelInfo(document: Self.store("store-info-kind-unknown")).data?.kind == .other("music"))
        #expect(ModelInfo(document: Self.store("store-info-kind-video")).data?.window == nil, "a video model has no token window")

        let towered = ModelInfo(document: Self.store("store-info-route-mlx-vlm")).data
        #expect(towered?.kind == nil, "kind is an MLX set's alone")
        #expect(towered?.mlxConversion == ModelInfo.MLXConversion(towers: ["vision_config", "audio_config"], converter: "mlx-vlm", textOnly: true))
        let textOnly = ModelInfo(document: Self.store("store-info-route-text-only")).data?.mlxConversion
        #expect(textOnly == ModelInfo.MLXConversion(towers: ["vision_config"], converter: nil, textOnly: true), "only --text-only builds it")
        #expect(ModelInfo(document: Self.store("store-info-route-mlx-audio")).data?.mlxConversion?.converter == "mlx-audio")
        #expect(ModelInfo(document: Self.store("store-info-route-mlx-audio")).data?.convertsTo == ["mlx"], "a voice model converts to MLX alone")
        #expect(ModelInfo(document: Self.store("store-info-route-mlx-video")).data?.mlxConversion?.converter == "mlx-video")
        #expect(ModelInfo(document: Self.store("store-info-route-drafter")).data?.mlxConversion?.towers == [])

        // A document from before 53e-53h states none of them.
        let before = ModelInfo(document: Self.store("store-info-mlx")).data
        #expect(before != nil && before?.kind == nil && before?.vectorWidth == nil && before?.mlxConversion == nil)
    }
}
