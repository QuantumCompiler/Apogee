import BusinessReads
import Foundation
import Testing

@testable import PresentationModels

private func recorded(_ name: String, _ ext: String = "json") -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name).\(ext)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// Stored weights over MLX sets of every kind (the GUI's 3a), each document
/// recorded from the binary built with the CLI's 53e-53m over a sandbox: one
/// MLX directory of each kind -- chat, vision, embedding, drafter, video,
/// voice -- and one whose record names a word that build does not know
/// (`music`), each with a conversion's record saying its kind; then the same
/// store once `config scan --register` registered what it registers. Nothing
/// is folded there, so the binary's `models list --all` was byte for byte its
/// `models list`, and one document answers both.
@Suite("StoreKinds", .timeLimit(.minutes(1)))
@MainActor
struct StoreKindsTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let chat = "Qwen3-8B/mlx/1a2b3c4d5e6f"
    static let vision = "Gemma4-E4B/mlx/2b3c4d5e6f70"
    static let embedding = "EmbeddingGemma-300M/mlx/3c4d5e6f7081"
    static let drafter = "Gemma4-E4B-Assistant/mlx/4d5e6f708192"
    static let video = "Wan2.2-TI2V-5B/mlx/5e6f708192a3"
    static let voice = "Kokoro-82M/mlx/6f708192a3b4"
    static let unknown = "MusicGen-Small/mlx/708192a3b4c5"
    static let byKind = [
        ("chat", chat), ("vision", vision), ("embedding", embedding), ("drafter", drafter), ("video", video),
        ("voice", voice), ("unknown", unknown),
    ]
    /// The backends `config scan --register` added, by kind.
    static let backends = [("chat", "Qwen3-8B-4bit"), ("vision", "Gemma4-E4B-4bit"), ("embedding", "EmbeddingGemma-300M-bf16")]

    static func path(_ handle: String) -> String { "/Users/someone/.apogee/models/" + handle }

    /// The kinds, none registered.
    func kinds(_ runner: ScriptedRunner = ScriptedRunner()) async -> (StoreViewModel, ScriptedRunner) {
        runner.reading(["models", "list"], recorded("store-list-kinds"))
        for (kind, handle) in Self.byKind {
            runner.reading(["models", "info", handle], recorded("store-info-kind-\(kind)"))
        }
        runner.commanding(["config", "scan"], .done(stdout: recorded("config-scan-kinds", "txt")))
        let store = StoreViewModel(runner: runner)
        await store.connect(binary: Self.binary)
        await store.refresh()
        return (store, runner)
    }

    /// The kinds once `config scan --register` ran: the chat, vision and
    /// embedding sets registered, each found through its backend's documents.
    func registered() async -> (StoreViewModel, ScriptedRunner) {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("store-list-kinds-registered"))
        for (kind, handle) in Self.byKind {
            runner.reading(["models", "info", handle], recorded("store-info-kind-\(kind)"))
        }
        for (kind, backend) in Self.backends {
            let handle = Self.byKind.first { $0.0 == kind }!.1
            runner.reading(["models", "info", backend], recorded("store-info-kinds-backend-\(kind)"))
            runner.reading(["models", "info", Self.path(handle)], recorded("store-info-kinds-path-\(kind)"))
            runner.reading(["models", "info", handle], recorded("store-info-kinds-path-\(kind)"))
        }
        let store = StoreViewModel(runner: runner)
        await store.connect(binary: Self.binary)
        await store.refresh()
        return (store, runner)
    }

    func set(_ store: StoreViewModel, _ handle: String) throws -> StoredSet {
        try #require(store.sets.first { $0.handle == handle }, "sets: \(store.sets.map(\.handle))")
    }

    @Test("each MLX kind is said beside its format -- a word the app does not know as the CLI wrote it -- and its state says what uses it")
    func eachKindSaid() async throws {
        let (store, _) = await kinds()
        #expect(store.sets.count == 7)
        let labels = try Self.byKind.map { try set(store, $0.1).formatLabel }
        #expect(labels == ["MLX · chat", "MLX · vision", "MLX · embedding", "MLX · drafter", "MLX · video", "MLX · voice", "MLX · music"])
        let states = try Self.byKind.map { try set(store, $0.1).state }
        #expect(states == [
            "not registered", "not registered", "not registered", "drafts beside its base", "for video generate",
            "for voice say", "nothing here runs it",
        ])
        #expect(try set(store, Self.unknown).note == "kind 'music', not one this apogee knows: nothing here runs it", "the CLI's own words")
        #expect(store.unregistered == 3, "what config scan --register registers: chat, vision and embedding")
        #expect(recorded("config-scan-kinds-register", "txt").contains("registered 3 backend(s)"), "as the binary did")
    }

    @Test("each kind offers only its own actions")
    func eachKindsActions() async throws {
        let (store, _) = await kinds()
        let files: [StoreViewModel.Action] = [.checkFiles, .showInFinder, .delete]
        #expect(store.actions(try set(store, Self.chat)) == [.register] + files)
        #expect(store.actions(try set(store, Self.vision)) == [.register] + files)
        #expect(store.actions(try set(store, Self.embedding)) == [.register] + files)
        #expect(store.actions(try set(store, Self.drafter)) == [.pairWithBase] + files)
        #expect(store.actions(try set(store, Self.video)) == [.newClip, .makeDefaultVideo] + files)
        #expect(store.actions(try set(store, Self.voice)) == [.openVoice] + files)
        #expect(store.actions(try set(store, Self.unknown)) == [.delete], "its details and its delete, nothing more")
        for handle in [Self.drafter, Self.video, Self.voice, Self.unknown] {
            let set = try set(store, handle)
            #expect(!set.isRunnable && set.advice == nil)
            await store.askToRegister(set)
            #expect(store.register == nil, "\(handle) opens no register sheet around the view")
            #expect(!store.converts(set, mlx: true) && !store.converts(set, mlx: false))
        }
    }

    @Test("an embedding set registers as an mlx entry the embedding role can name; registered, Use for embeddings points the role at it, and no session opens on it")
    func embeddingRegistersForTheRole() async throws {
        let (store, runner) = await kinds()
        let set = try set(store, Self.embedding)
        #expect(store.advice(set)?.contains("the embedding role can't name it yet") == true)
        await store.askToRegister(set)
        let form = try #require(store.register)
        #expect(form.name == "EmbeddingGemma-300M-bf16", "the name config scan lists it under")
        #expect(form.kind == .embedding && form.vectorWidth == 768)
        #expect(form.arguments == ["config", "add-backend", "EmbeddingGemma-300M-bf16", "--type", "mlx", "--model-path", Self.path(Self.embedding)])
        #expect(form.about == "An entry the embedding role can name, embedding with these weights on MLX. It answers no chat, so no session uses it. Its vectors are 768 wide.")
        await store.registerIt()
        #expect(runner.calls.contains("run config add-backend EmbeddingGemma-300M-bf16 --type mlx --model-path \(Self.path(Self.embedding))"))

        let (registered, calls) = await registered()
        let entry = try self.set(registered, Self.embedding)
        #expect(entry.backend == "EmbeddingGemma-300M-bf16" && entry.state == "backend EmbeddingGemma-300M-bf16")
        #expect(registered.actions(entry) == [
            .useForEmbeddings(backend: "EmbeddingGemma-300M-bf16"), .removeBackend("EmbeddingGemma-300M-bf16"), .checkFiles, .showInFinder, .delete,
        ])
        await registered.useForEmbeddings("EmbeddingGemma-300M-bf16")
        #expect(calls.calls.contains("run config set-default-embedding EmbeddingGemma-300M-bf16"))
        #expect(calls.calls.filter { $0 == "read models list --all" }.count == 2, "read back")
    }

    @Test("a vision set reads images, and registers and opens a session as a chat model does")
    func visionReadsImages() async throws {
        let (store, _) = await kinds()
        await store.askToRegister(try set(store, Self.vision))
        let form = try #require(store.register)
        #expect(form.name == "Gemma4-E4B-4bit" && form.type == "mlx")
        #expect(form.about.contains("It reads images: mlx-vlm built it with its towers."))
        await store.askToRegister(try set(store, Self.chat))
        #expect(store.register?.about == "A backend sessions, suites and roles can use, running these weights on MLX.")

        let (registered, _) = await registered()
        for (kind, backend) in Self.backends where kind != "embedding" {
            let handle = Self.byKind.first { $0.0 == kind }!.1
            #expect(registered.actions(try set(registered, handle)) == [.useInSession(backend: backend), .removeBackend(backend), .checkFiles, .showInFinder, .delete])
        }
        #expect(try set(registered, Self.vision).mlxKind == .vision)
    }

    @Test("a drafter offers nothing that runs it as chat: Pair with its base, said as the draft of the mlx backend over its base")
    func drafterPairs() async throws {
        let (store, _) = await kinds()
        let set = try set(store, Self.drafter)
        let actions = store.actions(set)
        #expect(actions == [.pairWithBase, .checkFiles, .showInFinder, .delete], "not paired with any backend here")
        #expect(!actions.contains(.register) && !actions.contains { if case .useInSession = $0 { true } else { false } })
        let note = store.pairingNote(set)
        #expect(note.hasPrefix("A drafter runs beside its base, as the draft of the mlx backend over it"))
        #expect(store.advice(set) == note)
        #expect(store.infos[Self.drafter]?.field("backends")?.contains("'apogee config set-backend <that backend> --draft <the path below>'") == true,
                "the CLI's own models info names the command the sheet runs")
    }

    @Test("a video model makes clips and can be the default, as a pipeline does; a voice model opens Voice")
    func videoAndVoice() async throws {
        let (store, runner) = await kinds()
        await store.makeDefaultVideo(try set(store, Self.video))
        #expect(runner.calls.contains("run config set-default-video \(Self.video)"), "the CLI's 53l takes an MLX video model")
        #expect(store.infos[Self.voice]?.field("voices")?.contains("af_heart") == true, "its voices, as the CLI lists them")
    }

    @Test("against a binary before the CLI's 53e, Stored weights reads as today: an MLX set is a chat model's, its format alone")
    func olderBinaryReadsAsToday() async throws {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("store-list"))
        runner.reading(["models", "list", "--all"], recorded("store-list-all"))
        runner.reading(["models", "info", "llama3.2-1b-Q4KM"], recorded("store-info-backend"))
        runner.reading(["models", "info", StoreViewModelTests.q4Path], recorded("store-info-q4"))
        runner.reading(["models", "info", "Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41"], recorded("store-info-f16"))
        runner.reading(["models", "info", "Gemma4-26B-Instruct/mlx/a88b9f386aff"], recorded("store-info-mlx"))
        runner.commanding(["config", "scan"], .done(stdout: recorded("config-scan", "txt")))
        let store = StoreViewModel(runner: runner)
        await store.connect(binary: Self.binary)
        await store.refresh()
        let files: [StoreViewModel.Action] = [.checkFiles, .showInFinder, .delete]
        let mlx = try set(store, "Gemma4-26B-Instruct/mlx/a88b9f386aff")
        #expect(mlx.kind == nil && mlx.mlxKind == .chat && mlx.formatLabel == "MLX")
        #expect(store.actions(mlx) == [.register] + files)
        #expect(store.actions(try set(store, "Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41")) == [.quantize, .register] + files)
        #expect(store.actions(try set(store, "Llama3.2-1B-Instruct/gguf/8d51e6c04a27")) == [.useInSession(backend: "llama3.2-1b-Q4KM"), .removeBackend("llama3.2-1b-Q4KM")] + files)
        #expect(store.actions(try set(store, "Kokoro-82M/voice/5f3a2c91d0e4")) == [.openVoice] + files, "a voice model converts only where its document says so")
        #expect(store.unregistered == 2)
        await store.askToRegister(mlx)
        #expect(store.register?.about == "A backend sessions, suites and roles can use, running these weights on MLX.")
    }
}

/// The convert sheet over the routes the CLI's 53f-53m decide, each `models
/// info` recorded from the binary built with them over a Python environment
/// holding only the files the CLI reads to decide -- nothing in it runs.
@Suite("ConvertRoutes", .timeLimit(.minutes(1)))
@MainActor
struct ConvertRoutesTests {
    static let lm = "Llama3.2-1B-Instruct/safetensors/edaba290ce7f"
    static let vlm = "Gemma4-E4B/safetensors/9e1f2a3b4c5d"
    static let textOnly = "Qwen3-VL-8B-Instruct/safetensors/63f2634a2019"
    static let drafter = "Gemma4-E4B-Assistant/safetensors/b1c2d3e4f5a6"
    static let embeddings = "EmbeddingGemma-300M/safetensors/af0e1d2c3b4a"
    static let video = "Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83"
    static let voice = "Kokoro-82M/voice/5f3a2c91d0e4"
    static let routes = [
        ("mlx-lm", lm), ("mlx-vlm", vlm), ("text-only", textOnly), ("drafter", drafter), ("mlx-embeddings", embeddings),
        ("mlx-video", video), ("mlx-audio", voice),
    ]

    func store() async -> StoreViewModel {
        let runner = ScriptedRunner()
        // Nothing folded: the binary's --all was byte for byte this document.
        runner.reading(["models", "list"], recorded("store-list-routes"))
        for (route, handle) in Self.routes {
            runner.reading(["models", "info", handle], recorded("store-info-route-\(route)"))
        }
        let store = StoreViewModel(runner: runner)
        await store.connect(binary: StoreKindsTests.binary)
        await store.refresh()
        return store
    }

    func form(_ store: StoreViewModel, _ handle: String) throws -> ConvertForm {
        let set = try #require(store.sets.first { $0.handle == handle }, "sets: \(store.sets.map(\.handle))")
        #expect(store.converts(set, mlx: true), "\(handle) converts to MLX, as its models info says")
        store.askToConvert(set, mlx: true)
        return try #require(store.convertForm)
    }

    @Test("a towered set's sheet says mlx-vlm keeps its towers, and offers Text only, which passes --text-only")
    func toweredKeepsItsTowers() async throws {
        let store = await store()
        var form = try form(store, Self.vlm)
        #expect(form.converter == "mlx-vlm" && form.towers == ["vision_config", "audio_config"])
        #expect(form.textOnlyOffered && !form.textOnlyOnly && !form.textOnly)
        #expect(form.converterNote.contains("with its towers kept (vision_config, audio_config)"))
        #expect(form.towersSaid == "vision_config, audio_config -- --mlx builds it through mlx-vlm, its towers kept; --mlx --text-only builds its language model alone with mlx-lm",
                "the CLI's own towers line")
        #expect(form.arguments == ["models", "convert", Self.vlm, "--mlx", "--register"])
        form.textOnly = true
        #expect(form.converter == "mlx-lm" && form.registers)
        #expect(form.arguments == ["models", "convert", Self.vlm, "--mlx", "--text-only", "--register"])
        #expect(form.converterNote.contains("Its towers (vision_config, audio_config) are left out"))
        #expect(form.steps.first?.label == "Convert to MLX 4bit, its language model alone")
        form.textOnly = false
        #expect(form.arguments == ["models", "convert", Self.vlm, "--mlx", "--register"], "towers kept again")
    }

    @Test("where only mlx-lm's text-only build is offered, the sheet passes --text-only and it cannot be turned off")
    func onlyTextOnly() async throws {
        let store = await store()
        var form = try form(store, Self.textOnly)
        #expect(form.textOnlyOnly && form.textOnly)
        form.textOnly = false
        #expect(form.textOnly, "a plain --mlx is refused here, naming --text-only")
        #expect(form.arguments == ["models", "convert", Self.textOnly, "--mlx", "--text-only", "--register"])
        #expect(form.towersSaid?.contains("builds no qwen3_vl model, so --mlx --text-only builds its language model alone") == true)
    }

    @Test("a language model and an embedding model register what they make; neither is offered Text only")
    func registeringRoutes() async throws {
        let store = await store()
        var lm = try form(store, Self.lm)
        #expect(lm.converter == "mlx-lm" && !lm.textOnlyOffered && lm.registers)
        lm.textOnly = true
        #expect(!lm.textOnly, "a language model has no tower to leave out")
        #expect(lm.arguments == ["models", "convert", Self.lm, "--mlx", "--register"])
        let embedding = try form(store, Self.embeddings)
        #expect(embedding.converter == "mlx-embeddings" && embedding.registers && !embedding.textOnlyOffered)
        #expect(embedding.converterNote.contains("an entry the embedding role can name"))
        #expect(embedding.arguments == ["models", "convert", Self.embeddings, "--mlx", "--register"])
    }

    @Test("a draft head, Wan's original layout and a voice model convert with none of --register, --register-with, --text-only or --model-type, whatever is chosen")
    func noRegistrationWhereNoBackendRuns() async throws {
        let store = await store()
        let refused = ["--register", "--register-with", "--text-only", "--model-type", "--base-name"]
        for (handle, converter, precision) in [(Self.drafter, "mlx-vlm", "bf16"), (Self.video, "mlx-video", "4bit"), (Self.voice, "mlx-audio", "bf16")] {
            let opened = try form(store, handle)
            #expect(opened.converter == converter && !opened.registers && !opened.register, "\(handle)")
            #expect(opened.defaultPrecision == precision && opened.written == precision)
            #expect(opened.levelChoices.isEmpty, "no further levels: --register-with is a registration")
            #expect(opened.arguments == ["models", "convert", handle, "--mlx"])
            #expect(opened.steps.map(\.label) == ["Convert to MLX \(precision)"])
            #expect(opened.converterNote.contains("nothing is registered"))
            for precision in [nil] + opened.precisions.dropFirst().map(Optional.some) {
                var form = opened
                form.precision = precision
                form.register = true
                form.levels = Set(ConvertForm.mlxPrecisions)
                form.baseName = "named"
                form.modelType = "gemma4"
                form.textOnly = true
                #expect(form.arguments.allSatisfy { !refused.contains($0) }, "\(form.arguments)")
            }
        }
        #expect(try form(store, Self.video).precisions == ["4bit", "8bit"], "mlx-video writes 4 or 8 bits")
        #expect(try form(store, Self.drafter).precisions.first == "bf16")
    }

    @Test("a voice model is offered Convert to MLX only where its models info says so; GGUF never")
    func voiceConvertsToMLX() async throws {
        let store = await store()
        let voice = try #require(store.sets.first { $0.handle == Self.voice })
        #expect(store.converts(voice, mlx: true) && !store.converts(voice, mlx: false))
        #expect(store.actions(voice) == [.convertToMLX, .openVoice, .checkFiles, .showInFinder, .delete])
        let drafter = try #require(store.sets.first { $0.handle == Self.drafter })
        #expect(store.actions(drafter) == [.convertToMLX, .checkFiles, .showInFinder, .delete], "a draft head is a SafeTensors set: its MLX build is the drafter")
    }
}
