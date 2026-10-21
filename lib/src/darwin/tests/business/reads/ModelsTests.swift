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
}
