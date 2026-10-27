#if DEBUG
import BusinessJobs
import BusinessReads
import DataPreferences
import DataProtocol
import Foundation
import PresentationShell

/// The Models place's view-models for its previews (ADR 0004), over two homes,
/// each whole in itself: the backends the view-model's own tests read, and the
/// recorded 2h scratch store. Nothing starts the binary.
@MainActor
enum ModelsPreview {
    // MARK: - The backends

    /// `models list`, `models status` and a GGUF backend's `models info` (52e's
    /// typed facts), as tests/presentation/models/ModelsViewModelTests.swift
    /// holds them (`list`, `status`, and its `details` test's document): two
    /// local backends, a stored pipeline, a cloud API and a subscription.
    static let backendsRunner = PreviewRunner(
        reads: [
            (["models", "list"], list),
            (["models", "status"], status),
            (["models", "info", "qwen3-8b-Q4KM"], ggufInfo),
        ],
        // The names `models rename` takes, as the binary completed them over
        // a GGUF family and an MLX model (M2's recording).
        commands: [(["__complete", "models", "rename"], ModelsDocuments.renameNames)])

    static let list = #"""
    {"object":"list","data":[
     {"type":"model","backend":"claude","backend_type":"anthropic","model":"claude-sonnet-5","roles":"","source":"remote","architecture":"","profile":"","state":"ready","verified":"","section":"cloud","group":"anthropic"},
     {"type":"model","backend":"qwen3-8b-Q4KM","backend_type":"llamacpp","model":"/store/gguf/qwen3-8b-Q4_K_M.gguf","roles":"chat, embedding","source":"store","architecture":"qwen3","profile":"","state":"ready","verified":"sha256 ok","section":"local","group":"gguf","format":"gguf","quant":"Q4_K_M"},
     {"type":"model","backend":"(not configured)","backend_type":"-","model":"Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3","roles":"","source":"huggingface","architecture":"WanPipeline","profile":"-","state":"pipeline","verified":"","section":"local","group":"pipeline","format":"pipeline","components":5,"bytes":28000000000},
     {"type":"model","backend":"qwen-mlx","backend_type":"mlx","model":"Qwen/Qwen3-8B","roles":"","source":"store","architecture":"qwen3","profile":"","state":"ready","verified":"","section":"local","group":"mlx","format":"mlx","window":"32768-token window (the default; trained for 131072)"},
     {"type":"model","backend":"claude-code","backend_type":"claude-cli","model":"","roles":"","source":"","architecture":"","profile":"","state":"ready","verified":"","section":"cloud","group":"claude-cli"}
    ]}
    """#

    static let status = #"{"fields":[{"field":"chat","value":"qwen3-8b-Q4KM"},{"field":"embedding","value":"qwen3-8b-Q4KM   (via models.default)"},{"field":"vision","value":"(unset -- the chat's own backend)"}]}"#

    static let ggufInfo = #"""
    {"name":"qwen3-8b-Q4KM","fields":[{"field":"backend","value":"qwen3-8b-Q4KM"}],
     "data":{"format":"gguf","handle":"qwen3-8b/gguf/6d0e12aa","bytes":5000000000,"architecture":"qwen3",
      "window":{"tokens":32768,"source":"default","trained":131072},
      "cache":{"bytes":1000000000,"type":"f16","sliding_positions":0},
      "lineage":[{"kind":"quantized","from":"qwen3-8b/gguf/F16","detail":"Q4_K_M","inferred":false,"missing":false},{"kind":"pulled","from":"Qwen/Qwen3-8B","detail":null,"inferred":true,"missing":false}]}}
    """#

    /// The backends' rows, as `models list` lists them.
    static var rows: [ModelRow] { ModelRow.list(from: ReadDocument(text: list)!) }

    /// The library, over the backends unless another home is named.
    static func models(over runner: PreviewRunner = backendsRunner) -> ModelsViewModel {
        ModelsViewModel(runner: runner)
    }

    /// The window's view-model on Models, its preferences held in memory,
    /// never the person's own defaults.
    static func shell() -> ShellViewModel {
        let shell = ShellViewModel(preferences: Preferences(store: MemoryStore()))
        shell.go(.models)
        return shell
    }

    // MARK: - A backend's adapters (GUI 4i)

    /// The adapter sandbox recorded from the branch's binary: `based` applying
    /// two adapters -- its promoted v1 and a hand-added one at scale 0.5 --
    /// both pinned; `q4` one whose base was rewritten under it, so the doctor
    /// fails its row and names the promote that mends it; `tuned` none.
    static let adaptersRunner = PreviewRunner(
        reads: [
            (["models", "list"], ModelsDocuments.adaptersList),
            (["models", "info", "based"], ModelsDocuments.adaptersInfoBased),
            (["models", "info", "q4"], ModelsDocuments.adaptersInfoQ4),
            (["check"], ModelsDocuments.adaptersCheck),
        ],
        commands: [
            (["config", "get", "backends.based.adapters"], ModelsDocuments.adaptersGet),
            (["config", "get", "backends.q4.adapters"], ModelsDocuments.adaptersGetQ4),
            (["config", "set-backend"], ModelsDocuments.adaptersSet),
        ])

    static func adapters() -> ModelsViewModel {
        ModelsViewModel(runner: adaptersRunner)
    }

    /// The adapter sandbox read, `backend` open.
    static func showingAdapters(_ models: ModelsViewModel, of backend: String) async {
        await models.connect(binary: previewBinary)
        if let row = models.rows.first(where: { $0.backend == backend }) { await models.select(row) }
    }

    /// `based`'s adapters in the sheet, as they are.
    static func editingAdapters(_ models: ModelsViewModel) async {
        await showingAdapters(models, of: "based")
        if let row = models.selectedRow { models.editAdapters(row) }
    }

    // MARK: - The store

    static let snapshot = "Llama3.2-1B-Instruct/safetensors/edaba290ce7f"
    static let f16 = "Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41"
    static let q4 = "Llama3.2-1B-Instruct/gguf/8d51e6c04a27"
    static let q4Path = "/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/8d51e6c04a27/Llama3.2-1B-Instruct-Q4KM.gguf"
    static let mlx = "Gemma4-26B-Instruct/mlx/a88b9f386aff"
    static let voice = "Kokoro-82M/voice/5f3a2c91d0e4"

    /// The 2h scratch store, as recorded and as StoreViewModelTests reads it: a
    /// pulled Llama SafeTensors set converted to an F16 GGUF (not registered)
    /// and quantized to a Q4_K_M one (registered as `llama3.2-1b-Q4KM`); a
    /// Gemma SafeTensors set and the MLX model converted from it (not
    /// registered); a voice model, its `models info` M1's recording of the same
    /// set. `config scan` answers with its recorded report.
    static let storeRunner = PreviewRunner(
        reads: [
            (["models", "list"], ModelsDocuments.storeList),
            (["models", "list", "--all"], ModelsDocuments.storeListAll),
            (["models", "info", "llama3.2-1b-Q4KM"], ModelsDocuments.storeInfoBackend),
            (["models", "info", q4Path], ModelsDocuments.storeInfoQ4),
            (["models", "info", q4], ModelsDocuments.storeInfoQ4),
            (["models", "info", f16], ModelsDocuments.storeInfoF16),
            (["models", "info", snapshot], ModelsDocuments.storeInfoSnapshot),
            (["models", "info", mlx], ModelsDocuments.storeInfoMLX),
            (["models", "info", voice], ModelsDocuments.storedInfoVoice),
        ],
        commands: [(["config", "scan"], ModelsDocuments.configScan)])

    /// Stored weights over the store, with the snapshots conversions consumed
    /// when `showingAll`. Not connected: nothing is read until a preview
    /// connects it.
    static func store(showingAll: Bool = false) -> StoreViewModel {
        let store = StoreViewModel(runner: storeRunner)
        store.showingAll = showingAll
        return store
    }

    // MARK: - What a set converts to (M3)

    static let wan = "Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83"
    static let draftHead = "Gemma4-12B-Assistant/safetensors/43b42c6289a0"

    /// A store recorded from the binary that ships the CLI's M20: a diffusion
    /// model's weights, a draft head and a Llama snapshot, each `models info`
    /// stating what it converts to.
    static let conversionsRunner = PreviewRunner(reads: [
        (["models", "list", "--all"], ModelsDocuments.conversionsListAll),
        (["models", "list"], ModelsDocuments.conversionsList),
        (["models", "info", wan], ModelsDocuments.infoDiffusion),
        (["models", "info", draftHead], ModelsDocuments.infoDraftHead),
        (["models", "info", "Llama3.2-1B-Instruct/safetensors/edaba290ce7f"], ModelsDocuments.infoSnapshotConverts),
    ])

    /// Stored weights over that store; not connected.
    static func conversionsStore() -> StoreViewModel {
        StoreViewModel(runner: conversionsRunner)
    }

    /// The Llama snapshot's bytes, as its `models info` states them.
    static var snapshotBytes: Int? {
        ModelInfo(document: ReadDocument(text: ModelsDocuments.storeInfoSnapshot)!).data?.bytes
    }

    /// `models convert` of the Llama snapshot to an F16 GGUF, then quantized to
    /// Q4_K_M, as the convert sheet builds it.
    static var conversion: ConvertForm {
        var form = ConvertForm(handle: snapshot, mlx: false, bytes: snapshotBytes)
        form.levels = ["Q4_K_M"]
        return form
    }

    // MARK: - MLX kinds (the CLI's 53e-53m)

    static let kindChat = "Qwen3-8B/mlx/1a2b3c4d5e6f"
    static let kindVision = "Gemma4-E4B/mlx/2b3c4d5e6f70"
    static let kindEmbedding = "EmbeddingGemma-300M/mlx/3c4d5e6f7081"
    static let kindDrafter = "Gemma4-E4B-Assistant/mlx/4d5e6f708192"
    static let kindVideo = "Wan2.2-TI2V-5B/mlx/5e6f708192a3"
    static let kindVoice = "Kokoro-82M/mlx/6f708192a3b4"
    static let kindUnknown = "MusicGen-Small/mlx/708192a3b4c5"

    /// Each kind's `models info`, by handle.
    static let kindInfos: [([String], String)] = [
        (["models", "info", kindChat], ModelsDocuments.kindInfoChat),
        (["models", "info", kindVision], ModelsDocuments.kindInfoVision),
        (["models", "info", kindEmbedding], ModelsDocuments.kindInfoEmbedding),
        (["models", "info", kindDrafter], ModelsDocuments.kindInfoDrafter),
        (["models", "info", kindVideo], ModelsDocuments.kindInfoVideo),
        (["models", "info", kindVoice], ModelsDocuments.kindInfoVoice),
        (["models", "info", kindUnknown], ModelsDocuments.kindInfoUnknown),
    ]

    /// A store recorded from the binary built with the CLI's 53e-53m: one MLX
    /// directory of each kind -- chat, vision, embedding, drafter, video,
    /// voice -- and one whose record names a word this build does not know
    /// (`music`), none registered. Nothing is folded, so the binary's
    /// `models list --all` is byte for byte its `models list`.
    static let kindsRunner = PreviewRunner(
        reads: [(["models", "list"], ModelsDocuments.kindsList)] + kindInfos,
        commands: [(["config", "scan"], ModelsDocuments.kindsConfigScan)])

    /// Stored weights over the kinds; not connected.
    static func kindsStore() -> StoreViewModel {
        StoreViewModel(runner: kindsRunner)
    }

    static func kindPath(_ handle: String) -> String { "/Users/someone/.apogee/models/" + handle }

    /// The same store once `config scan --register` registered what it
    /// registers -- the chat, vision and embedding sets, each an `mlx` entry
    /// -- each found through its backend's `models info` and its path's.
    static let kindsRegisteredRunner = PreviewRunner(
        reads: [
            (["models", "list"], ModelsDocuments.kindsRegisteredList),
            (["models", "status"], ModelsDocuments.kindsRegisteredStatus),
            (["models", "info", "Qwen3-8B-4bit"], ModelsDocuments.kindsBackendChat),
            (["models", "info", "Gemma4-E4B-4bit"], ModelsDocuments.kindsBackendVision),
            (["models", "info", "EmbeddingGemma-300M-bf16"], ModelsDocuments.kindsBackendEmbedding),
            (["models", "info", kindPath(kindChat)], ModelsDocuments.kindsPathChat),
            (["models", "info", kindPath(kindVision)], ModelsDocuments.kindsPathVision),
            (["models", "info", kindPath(kindEmbedding)], ModelsDocuments.kindsPathEmbedding),
        ] + kindInfos.filter { ![kindChat, kindVision, kindEmbedding].contains($0.0[2]) } + [
            (["models", "info", kindChat], ModelsDocuments.kindsPathChat),
            (["models", "info", kindVision], ModelsDocuments.kindsPathVision),
            (["models", "info", kindEmbedding], ModelsDocuments.kindsPathEmbedding),
        ])

    /// Stored weights over the registered kinds; not connected.
    static func kindsRegisteredStore() -> StoreViewModel {
        StoreViewModel(runner: kindsRegisteredRunner)
    }

    /// The same three entries, registered by `config add-backend`, with the
    /// chat role pointed at the embedding entry by `config set-default` --
    /// which the binary takes -- as `models list` and `models status` read
    /// them then (the GUI's M4).
    static let chatOnEmbeddingRunner = PreviewRunner(reads: [
        (["models", "list"], ModelsDocuments.chatOnEmbeddingList),
        (["models", "status"], ModelsDocuments.chatOnEmbeddingStatus),
    ])

    // MARK: - A drafter and its base (the CLI's 53i)

    static let drafter = "Gemma4-E4B-Assistant/mlx/4d5e6f708192"
    static let draftBase = "Gemma4-E4B/mlx/2b3c4d5e6f70"

    /// A store recorded from the binary built with the CLI's 53i, over a
    /// Python environment holding only the files the CLI reads to decide a
    /// pairing (mlx-vlm's gemma4 module and its gemma4_assistant drafter):
    /// mlx-vlm's build of Gemma 4 E4B (`Gemma4-E4B-4bit`), the drafter its
    /// assistant converts to, a wider Gemma 4 (`Gemma4-12B-4bit`) and mlx-lm's
    /// Qwen3 (`Qwen3-8B-4bit`) -- and each backend's completion of `--draft`,
    /// which offers the drafter to its own base alone. When `paired`, the
    /// documents read back after `config set-backend Gemma4-E4B-4bit --draft`.
    static func draftsRunner(paired: Bool) -> PreviewRunner {
        PreviewRunner(
            reads: [
                (["models", "list"], ModelsDocuments.draftsList),
                (["models", "info", drafter], paired ? ModelsDocuments.draftsInfoDrafterPaired : ModelsDocuments.draftsInfoDrafter),
                (["models", "info", "Gemma4-E4B-4bit"], paired ? ModelsDocuments.draftsBackendBasePaired : ModelsDocuments.draftsBackendBase),
                (["models", "info", "Gemma4-12B-4bit"], ModelsDocuments.draftsBackendWider),
                (["models", "info", "Qwen3-8B-4bit"], ModelsDocuments.draftsBackendChat),
                (["models", "info", kindPath(draftBase)], ModelsDocuments.draftsPathBase),
                (["models", "info", kindPath("Gemma4-12B/mlx/7a8b9c0d1e2f")], ModelsDocuments.draftsPathWider),
                (["models", "info", kindPath("Qwen3-8B/mlx/1a2b3c4d5e6f")], ModelsDocuments.draftsPathChat),
                (["models", "info", draftBase], ModelsDocuments.draftsSetBase),
                (["models", "info", "Gemma4-12B/mlx/7a8b9c0d1e2f"], ModelsDocuments.draftsSetWider),
                (["models", "info", "Qwen3-8B/mlx/1a2b3c4d5e6f"], ModelsDocuments.draftsSetChat),
            ],
            commands: [
                (["__complete", "config", "set-backend", "Gemma4-E4B-4bit", "--draft"], ModelsDocuments.draftsCompleteBase),
                (["__complete", "config", "set-backend", "Gemma4-12B-4bit", "--draft"], ModelsDocuments.draftsCompleteWider),
                (["__complete", "config", "set-backend", "Qwen3-8B-4bit", "--draft"], ModelsDocuments.draftsCompleteChat),
            ])
    }

    // MARK: - A drafter paired from its base (GUI M5)

    static let drafter12B = "Gemma4-12B-Assistant/mlx/8192a3b4c5d6"

    /// The base-side home recorded from the branch's binary: mlx-vlm's builds
    /// of Gemma 4 E4B and 12B and mlx-lm's Qwen3, registered by `config scan
    /// --register`, and two drafters stored -- the E4B's assistant and the
    /// 12B's -- each base's completion of `--draft` offering its own alone.
    /// When `paired`, the E4B's `models info` read back after `config
    /// set-backend Gemma4-E4B-4bit --draft <its drafter>`; the list is the
    /// same bytes either way.
    static func basePairRunner(paired: Bool) -> PreviewRunner {
        PreviewRunner(
            reads: [
                (["models", "list"], ModelsDocuments.basePairList),
                (["models", "status"], ModelsDocuments.kindsRegisteredStatus),
                (["models", "info", "Gemma4-E4B-4bit"], paired ? ModelsDocuments.basePairBackendBasePaired : ModelsDocuments.basePairBackendBase),
                (["models", "info", "Gemma4-12B-4bit"], ModelsDocuments.basePairBackendWider),
                (["models", "info", "Qwen3-8B-4bit"], ModelsDocuments.basePairBackendChat),
                (["models", "info", drafter], ModelsDocuments.basePairDrafter),
                (["models", "info", drafter12B], ModelsDocuments.basePairDrafterWider),
            ],
            commands: [
                (["__complete", "config", "set-backend", "Gemma4-E4B-4bit", "--draft"], ModelsDocuments.basePairCompleteBase),
                (["__complete", "config", "set-backend", "Gemma4-12B-4bit", "--draft"], ModelsDocuments.basePairCompleteWider),
            ])
    }

    /// The same home with the E4B paired and its drafter's directory then
    /// deleted, its environment holding what the mlx ladder asks before the
    /// draft rung, so the CLI's refusal is the draft's.
    static let draftGoneRunner = PreviewRunner(reads: [
        (["models", "list"], ModelsDocuments.draftGoneList),
        (["models", "status"], ModelsDocuments.kindsRegisteredStatus),
        (["models", "info", "Gemma4-E4B-4bit"], ModelsDocuments.draftGoneBackend),
    ])

    /// The E4B paired, then its directory's record rewritten as mlx-lm's build
    /// (`chat`, `mlx_lm.convert 4bit`) -- a fake directory standing for a base
    /// converted again as mlx-lm's build, its files unchanged.
    static let draftRebuiltRunner = PreviewRunner(reads: [
        (["models", "list"], ModelsDocuments.draftRebuiltList),
        (["models", "status"], ModelsDocuments.kindsRegisteredStatus),
        (["models", "info", "Gemma4-E4B-4bit"], ModelsDocuments.draftRebuiltBackend),
    ])

    /// Reads the backends, then opens `backend`'s details, as a person does.
    static func showing(_ models: ModelsViewModel, _ backend: String) async {
        await models.connect(binary: previewBinary)
        if let row = models.rows.first(where: { $0.backend == backend && !$0.isStored }) { await models.select(row) }
    }

    /// Then asks to pair a drafter with it, from its details.
    static func pairingDrafter(_ models: ModelsViewModel, _ backend: String) async {
        await showing(models, backend)
        if let row = models.selectedRow { await models.askToPairDrafter(row) }
    }

    // MARK: - What an MLX conversion runs (the CLI's 53f-53m)

    static let routeLM = "Llama3.2-1B-Instruct/safetensors/edaba290ce7f"
    static let routeVLM = "Gemma4-E4B/safetensors/9e1f2a3b4c5d"
    static let routeTextOnly = "Qwen3-VL-8B-Instruct/safetensors/63f2634a2019"
    static let routeDrafter = "Gemma4-E4B-Assistant/safetensors/b1c2d3e4f5a6"
    static let routeEmbeddings = "EmbeddingGemma-300M/safetensors/af0e1d2c3b4a"
    static let routeVideo = "Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83"
    static let routeVoice = "Kokoro-82M/voice/5f3a2c91d0e4"

    /// A store recorded over a Python environment holding only the files the
    /// CLI reads to decide each route -- nothing in it runs: a language model
    /// (mlx-lm), a towered set mlx-vlm keeps whole with Text only beside it, a
    /// towered set only mlx-lm's text-only build takes, a draft head mlx-vlm
    /// drafts, an embedding model mlx-embeddings builds, Wan's original layout
    /// whole (mlx-video) and a voice model (mlx-audio).
    static let routesRunner = PreviewRunner(reads: [
        (["models", "list"], ModelsDocuments.routesList),
        (["models", "info", routeLM], ModelsDocuments.routeInfoLM),
        (["models", "info", routeVLM], ModelsDocuments.routeInfoVLM),
        (["models", "info", routeTextOnly], ModelsDocuments.routeInfoTextOnly),
        (["models", "info", routeDrafter], ModelsDocuments.routeInfoDrafter),
        (["models", "info", routeEmbeddings], ModelsDocuments.routeInfoEmbeddings),
        (["models", "info", routeVideo], ModelsDocuments.routeInfoVideo),
        (["models", "info", routeVoice], ModelsDocuments.routeInfoAudio),
    ])

    /// Stored weights over the routes; not connected.
    static func routesStore() -> StoreViewModel {
        StoreViewModel(runner: routesRunner)
    }

    /// Reads `store`, then opens `handle`'s MLX conversion, as a person does.
    static func openConversion(_ store: StoreViewModel, _ handle: String) async {
        await store.connect(binary: previewBinary)
        await store.refresh()
        if let set = store.sets.first(where: { $0.handle == handle }) { store.askToConvert(set, mlx: true) }
    }

    // MARK: - The jobs

    /// The Jobs tab, each job it starts saying `said` (and ending there when
    /// it `finishes`), the store read back from the recorded one: a store
    /// job's first step runs, nothing new having landed.
    static func jobs(saying said: [JobUpdate] = [], finishes: Bool = false, streams: Bool = false) -> DownloadsViewModel {
        let jobs = DownloadsViewModel(starter: PreviewJobs(said: said, finishes: finishes))
        jobs.connect(binary: previewBinary)
        jobs.reader = storeRunner
        jobs.streams = { streams }
        return jobs
    }

    /// The conversion running its first step, saying what the CLI says as it
    /// converts (tests/presentation/models/StoreJobsTests.swift).
    static func converting() -> DownloadsViewModel {
        let jobs = Self.jobs(saying: [.said("converting Llama3.2-1B-Instruct to F16")])
        jobs.convert(conversion)
        return jobs
    }

    /// A pull of full weights on a 52b build, its first stage started and its
    /// bytes moving (tests/presentation/models/DownloadsViewModelTests.swift).
    static func pulling() -> DownloadsViewModel {
        let jobs = Self.jobs(
            saying: [
                .event(.stage(index: 1, count: 3, label: "pull org/model's full weights", state: "started")),
                .event(.bytes(done: 100, total: 400, file: "a.safetensors", index: 1, count: 2)),
            ],
            streams: true)
        jobs.reference = "org/model"
        jobs.convert = true
        jobs.start()
        return jobs
    }

    /// A pull that failed at its last step, the command that carries it on
    /// named (tests/presentation/models/DownloadsViewModelTests.swift).
    static func pullFailed() -> DownloadsViewModel {
        let jobs = Self.jobs(
            saying: [
                .event(.stage(index: 3, count: 3, label: "quantize to Q4_K_M", state: "failed")),
                .event(.notice("stopped at step 3 of 3 -- carry on with: apogee models quantize org--model/gguf/F16 Q4_K_M")),
                .event(.error("quantize failed: out of disk space")),
                previewEnd(status: 1),
            ],
            finishes: true, streams: true)
        jobs.reference = "org/model"
        jobs.convert = true
        jobs.start()
        return jobs
    }

    /// Check the files of the Llama snapshot, passed: its result the CLI's
    /// last line (tests/presentation/models/StoreJobsTests.swift).
    static func checked() -> DownloadsViewModel {
        let jobs = Self.jobs(
            saying: [.said("Llama3.2-1B-Instruct/safetensors/edaba290ce7f: 9 file(s) match the record"), previewEnd(status: 0)],
            finishes: true)
        jobs.check(snapshot)
        return jobs
    }

    /// A chained pull that finished, its stream the CLI's own pinned one
    /// (hand-made: a real chain needs real weights) -- naming a resume on
    /// the way, as `model_chain.cpp` does on success too
    /// (tests/presentation/models/JobIdentityTests.swift).
    static func chainDone() -> DownloadsViewModel {
        let jobs = Self.jobs(saying: previewStream(ModelsDocuments.chainDone) + [previewEnd(status: 0)], finishes: true, streams: true)
        jobs.reference = "org/model"
        jobs.convert = true
        jobs.start()
        return jobs
    }

    // MARK: - A quantization run again (GUI 4e)

    static let quantizeF16 = "Llama3.2-1B-Instruct/gguf/dfed3eebdfdb"
    static let quantizePath = "/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/5703795782eb/Llama3.2-1B-Instruct-Q4_K_M.gguf"

    /// The store recorded from the branch's binary quantizing a tiny F16 for
    /// real: before the run, and `models info` of the file it printed; its
    /// registration as `config add-backend` answered.
    static func quantizeStore() -> PreviewStore {
        PreviewStore(
            reads: [
                (["models", "list", "--all"], ModelsDocuments.quantizeListBefore),
                (["models", "info", quantizePath], ModelsDocuments.quantizeInfoLlamaPath),
            ],
            commands: [(["config", "add-backend"], ModelsDocuments.quantizeAddBackendLlama)])
    }

    /// A quantization asked to register what it makes: its first run failed,
    /// the store not writable, in llama.cpp's words; the second, its own
    /// output naming the GGUF it made (tests/presentation/models/JobIdentityTests.swift).
    static func quantizing(_ store: PreviewStore) -> DownloadsViewModel {
        let jobs = DownloadsViewModel(starter: PreviewJobQueue([
            (said: [previewEnd(status: 1, stderr: ModelsDocuments.quantizeLlamaFailed)], finishes: true),
            (said: previewSaid(ModelsDocuments.quantizeLlama) + [previewEnd(status: 0)], finishes: true),
        ]))
        jobs.connect(binary: previewBinary)
        jobs.reader = store
        jobs.quantize(QuantizeForm(handle: quantizeF16, base: "Llama3.2-1B-Instruct", bytes: nil))
        return jobs
    }

    /// Run again on the failed quantization, once it has failed: the store
    /// then as the binary read it after the second run.
    static func runAgain(_ jobs: DownloadsViewModel, _ store: PreviewStore) async {
        await until { jobs.downloads.first?.endedAt != nil }
        guard let failed = jobs.downloads.first else { return }
        store.change([(["models", "list", "--all"], ModelsDocuments.quantizeListBothLanded)])
        jobs.again(failed)
    }

    /// Waits, a preview's way, for what its story needs next.
    static func until(_ condition: @MainActor () -> Bool) async {
        for _ in 0..<500 where !condition() {
            try? await Task.sleep(for: .milliseconds(10))
        }
    }

    // MARK: - Drafters from the list, and the roles a change moves (GUI M16)

    /// The branch's binary (schema 2026-10-26, the CLI's M32) over M5's fake
    /// directories, every base registered by `config scan --register`: the
    /// E4B paired and drafting, the 12B paired and its drafter's directory
    /// then deleted, Qwen3 unpaired -- each drafter on its `models list` row,
    /// so the table reads no `models info`; each backend's answers only when
    /// its details open.
    static let rowsDraftsRunner = PreviewRunner(reads: [
        (["models", "list"], ModelsDocuments.rowsDraftsList),
        (["models", "status"], ModelsDocuments.rowsDraftsStatus),
        (["models", "info", "Gemma4-E4B-4bit"], ModelsDocuments.rowsDraftsInfoBase),
        (["models", "info", "Gemma4-12B-4bit"], ModelsDocuments.rowsDraftsInfoWider),
        (["models", "info", "Qwen3-8B-4bit"], ModelsDocuments.rowsDraftsInfoChat),
    ])

    /// The library on a binary dated for the CLI's M32, as the launch's
    /// vocabulary read says it.
    static func dated(over runner: some CommandRunning) -> ModelsViewModel {
        let models = ModelsViewModel(runner: runner)
        models.vocabulary = { "2026-10-26" }
        return models
    }

    /// The same bases with nothing paired, the 12B the default and Qwen3
    /// the utility role; then `config delete-backend Gemma4-12B-4bit`, which
    /// clears the default that named it (the CLI's M33), and the list and
    /// roles as the binary read them after.
    static func removeStore() -> PreviewStore {
        PreviewStore(
            reads: [
                (["models", "list"], ModelsDocuments.removeList),
                (["models", "status"], ModelsDocuments.removeStatus),
            ],
            commands: [(["config", "delete-backend", "Gemma4-12B-4bit"], ModelsDocuments.deleteBackendClears)])
    }

    /// Reads the backends, asks to remove the 12B -- the default -- and
    /// removes it, as a person does.
    static func removeTheDefault(_ models: ModelsViewModel, _ runner: PreviewStore) async {
        await models.connect(binary: previewBinary)
        if let row = models.rows.first(where: { $0.backend == "Gemma4-12B-4bit" }) { models.askToRemove(row) }
        runner.change([
            (["models", "list"], ModelsDocuments.removeListAfter),
            (["models", "status"], ModelsDocuments.removeStatusAfter),
        ])
        await models.confirmRemove()
    }

    /// The same bases, Qwen3 the default and the utility role; its weights
    /// deleted from the Backends tab with its backend -- the plan read first,
    /// then `models delete Qwen3-8B-4bit --yes --and-backends`, which clears
    /// both roles that named it -- and the list and roles read back.
    static func deleteRolesStore() -> PreviewStore {
        PreviewStore(
            reads: [
                (["models", "list"], ModelsDocuments.deleteRolesList),
                (["models", "status"], ModelsDocuments.deleteRolesStatus),
            ],
            commands: [
                (["models", "delete", "Qwen3-8B-4bit"], ModelsDocuments.deleteRolesPlan),
                (["models", "delete", "Qwen3-8B-4bit", "--yes", "--and-backends"], ModelsDocuments.deleteRolesAndBackends),
            ])
    }

    /// Reads the backends, asks to delete Qwen3 with its weights, and
    /// deletes it with its backend, as a person does.
    static func deleteTheRoleHolder(_ models: ModelsViewModel, _ runner: PreviewStore) async {
        await askToDeleteBackend(models, "Qwen3-8B-4bit")
        runner.change([
            (["models", "list"], ModelsDocuments.deleteRolesListAfter),
            (["models", "status"], ModelsDocuments.deleteRolesStatusAfter),
        ])
        await models.confirmDelete(andBackends: true)
    }

    static let addChat = "Qwen3-8B/mlx/1a2b3c4d5e6f"

    /// The same fake directories, nothing registered and no default; then
    /// Qwen3 registered from Stored weights -- `config add-backend`, which
    /// sets the default none was (the CLI's M33) -- and the store as the
    /// binary read it after.
    static func addStore() -> PreviewStore {
        PreviewStore(
            reads: [
                (["models", "list"], ModelsDocuments.addList),
                (["models", "list", "--all"], ModelsDocuments.addListAll),
                (["models", "info", addChat], ModelsDocuments.addInfoChat),
                (["models", "info", "Gemma4-E4B/mlx/2b3c4d5e6f70"], ModelsDocuments.addInfoBase),
                (["models", "info", "Gemma4-12B/mlx/7a8b9c0d1e2f"], ModelsDocuments.addInfoWider),
                (["models", "info", drafter], ModelsDocuments.addInfoDrafter),
                (["models", "info", drafter12B], ModelsDocuments.addInfoDrafterWider),
            ],
            commands: [
                (["config", "scan"], ModelsDocuments.addScan),
                (["config", "add-backend", "Qwen3-8B-4bit"], ModelsDocuments.addBackendDefault),
            ])
    }

    /// Reads the store, asks to register Qwen3, and registers it, as a
    /// person does.
    static func registerTheFirst(_ store: StoreViewModel, _ runner: PreviewStore) async {
        await store.connect(binary: previewBinary)
        await store.refresh()
        await until { store.infos[addChat] != nil }
        if let set = store.sets.first(where: { $0.handle == addChat }) { await store.askToRegister(set) }
        runner.change([
            (["models", "list"], ModelsDocuments.addListAfter),
            (["models", "list", "--all"], ModelsDocuments.addListAllAfter),
            (["models", "info", "Qwen3-8B-4bit"], ModelsDocuments.addInfoBackendAfter),
            (["models", "info", kindPath(addChat)], ModelsDocuments.addInfoPathAfter),
        ])
        await store.registerIt()
    }

    // MARK: - A delete, as the CLI plans and narrates it (GUI 4e)

    static let deleteLlamaQ4 = "Llama3.2-1B-Instruct/gguf/5703795782eb"
    static let deleteGemmaQ4 = "Gemma3-1B-Instruct/gguf/09f55af982ed"
    static let deleteLlamaF16 = "Llama3.2-1B-Instruct/gguf/dfed3eebdfdb"
    static let deleteGemmaPath = "/Users/someone/.apogee/models/Gemma3-1B-Instruct/gguf/09f55af982ed/Gemma3-1B-Instruct-Q4_K_M.gguf"

    /// The store recorded from the branch's binary after both quantizations
    /// were registered: Llama's and Gemma's F16s and Q4_K_Ms, each Q4_K_M a
    /// backend; each delete's plan, printed without `--yes`, and each delete
    /// (tests/presentation/models/StoreDeleteTests.swift).
    static func deletesStore() -> PreviewStore {
        PreviewStore(
            reads: [
                (["models", "list"], ModelsDocuments.deleteList),
                (["models", "list", "--all"], ModelsDocuments.deleteListAll),
                (["models", "info", "Llama3.2-1B-Instruct-Q4KM"], ModelsDocuments.deleteInfoBackendLlama),
                (["models", "info", "Gemma3-1B-Instruct-Q4KM"], ModelsDocuments.deleteInfoBackendGemma),
                (["models", "info", "/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/5703795782eb/Llama3.2-1B-Instruct-Q4_K_M.gguf"],
                 ModelsDocuments.deleteInfoPathLlama),
                (["models", "info", deleteGemmaPath], ModelsDocuments.deleteInfoPathGemma),
                (["models", "info", deleteLlamaQ4], ModelsDocuments.deleteInfoLlamaQ4),
                (["models", "info", deleteGemmaQ4], ModelsDocuments.deleteInfoGemmaQ4),
                (["models", "info", deleteLlamaF16], ModelsDocuments.deleteInfoLlamaF16),
                (["models", "info", "Gemma3-1B-Instruct/gguf/cce9d2f99787"], ModelsDocuments.deleteInfoGemmaF16),
            ],
            commands: [
                (["models", "delete", deleteLlamaQ4], ModelsDocuments.deletePlanBackend),
                (["models", "delete", deleteGemmaQ4], ModelsDocuments.deletePlanGemma),
                (["models", "delete", deleteLlamaF16], ModelsDocuments.deletePlanNone),
                (["models", "delete", deleteGemmaQ4, "--yes"], ModelsDocuments.deleteKept),
                (["models", "delete", deleteLlamaQ4, "--yes", "--and-backends"], ModelsDocuments.deleteAndBackends),
            ])
    }

    /// Reads the store, then asks to delete `handle`, as a person does.
    static func askToDelete(_ store: StoreViewModel, _ handle: String) async {
        await store.connect(binary: previewBinary)
        await store.refresh()
        if let set = store.sets.first(where: { $0.handle == handle }) { await store.askToDelete(set) }
    }

    /// Gemma's Q4_K_M deleted, its backend kept: the store then as the
    /// binary read it.
    static func deleteKept(_ store: StoreViewModel, _ runner: PreviewStore) async {
        await askToDelete(store, deleteGemmaQ4)
        runner.change([
            (["models", "list"], ModelsDocuments.deleteListAfterKept),
            (["models", "list", "--all"], ModelsDocuments.deleteListAllAfterKept),
            (["models", "info", "Gemma3-1B-Instruct-Q4KM"], ModelsDocuments.deleteInfoBackendGemmaAfterKept),
            (["models", "info", deleteGemmaPath], nil),
        ])
        await store.confirm()
    }

    // MARK: - The Backends tab's Delete with its weights (GUI 4e)

    /// The Backends tab over the same store, each Q4_K_M's backend deleted by
    /// its name: the binary answered every plan, delete and list so byte for
    /// byte as by the set's handle; `models status` and the rename
    /// completion as it answered them there.
    static func backendsDeleteStore() -> PreviewStore {
        PreviewStore(
            reads: [
                (["models", "list"], ModelsDocuments.deleteList),
                (["models", "status"], ModelsDocuments.backendsStatus),
            ],
            commands: [
                (["__complete", "models", "rename"], ModelsDocuments.backendsCompleteRename),
                (["models", "delete", "Llama3.2-1B-Instruct-Q4KM"], ModelsDocuments.deletePlanBackend),
                (["models", "delete", "Gemma3-1B-Instruct-Q4KM"], ModelsDocuments.deletePlanGemma),
                (["models", "delete", "Gemma3-1B-Instruct-Q4KM", "--yes"], ModelsDocuments.deleteKept),
                (["models", "delete", "Llama3.2-1B-Instruct-Q4KM", "--yes", "--and-backends"], ModelsDocuments.deleteAndBackends),
            ])
    }

    /// Reads the backends, then asks to delete `backend` with its weights, as
    /// a person does.
    static func askToDeleteBackend(_ models: ModelsViewModel, _ backend: String) async {
        await models.connect(binary: previewBinary)
        if let row = models.rows.first(where: { $0.backend == backend }) { await models.askToDelete(row) }
    }

    /// Gemma's backend deleted with its weights, the entry kept: the list
    /// then as the binary read it.
    static func deleteBackendKept(_ models: ModelsViewModel, _ runner: PreviewStore) async {
        await askToDeleteBackend(models, "Gemma3-1B-Instruct-Q4KM")
        runner.change([(["models", "list"], ModelsDocuments.deleteListAfterKept)])
        await models.confirmDelete(andBackends: false)
    }

    /// Then Llama's, with the backend that uses its files.
    static func deleteBackendWithBackends(_ models: ModelsViewModel, _ runner: PreviewStore) async {
        await deleteBackendKept(models, runner)
        if let row = models.rows.first(where: { $0.backend == "Llama3.2-1B-Instruct-Q4KM" }) { await models.askToDelete(row) }
        runner.change([(["models", "list"], ModelsDocuments.deleteListAfterRemoved)])
        await models.confirmDelete(andBackends: true)
    }

    /// Then Llama's, with the backend that runs from it.
    static func deleteWithBackends(_ store: StoreViewModel, _ runner: PreviewStore) async {
        await deleteKept(store, runner)
        if let set = store.sets.first(where: { $0.handle == deleteLlamaQ4 }) { await store.askToDelete(set) }
        store.andBackends = true
        runner.change([
            (["models", "list"], ModelsDocuments.deleteListAfterRemoved),
            (["models", "list", "--all"], ModelsDocuments.deleteListAllAfterRemoved),
        ])
        await store.confirm()
    }
}
#endif
