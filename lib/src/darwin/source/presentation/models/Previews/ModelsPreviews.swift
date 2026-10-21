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
}
#endif
