import BusinessReads
import DataProtocol
import Foundation

/// The store's video and voice models, read once for every sheet that names
/// one: the clip sheet and the compose sheet a video model, the say sheet and
/// the compose sheet a voice model (GUI 3b; Compose's since 4f). The rows are
/// `models list`'s own, never a list of the app's.
extension MediaModel {
    /// The store's voice models as `models list` states them, in the order a
    /// sheet offers them: each voice model as Kokoro publishes it on PyTorch,
    /// each MLX directory of kind `voice` on MLX (the CLI's 53m) -- PyTorch
    /// first within a folder.
    public static func voiceModels(in list: ReadDocument) -> [MediaModel] {
        stored(in: list, MediaEngine.voice(format:kind:))
    }

    /// The store's video models as `models list` states them, in the order a
    /// sheet offers them: each diffusers pipeline on PyTorch, each MLX
    /// directory of kind `video` on MLX (the CLI's 53l).
    public static func videoModels(in list: ReadDocument) -> [MediaModel] {
        stored(in: list, MediaEngine.video(format:kind:))
    }

    /// `models list` read through `runner`, and the store's voice models in
    /// it; `nil` when the read fails, so a sheet keeps what it last read.
    public static func readVoiceModels(_ runner: any CommandRunning, binary: URL) async -> [MediaModel]? {
        await runner.read(binary: binary, ["models", "list"]).document.map(voiceModels(in:))
    }

    private static func stored(in list: ReadDocument, _ engine: (String?, String?) -> MediaEngine?) -> [MediaModel] {
        offered(
            ModelRow.list(from: list).filter(\.isStored).compactMap { row in
                engine(row.format, row.mlxKind?.word).map { MediaModel(handle: row.model, engine: $0) }
            })
    }
}
