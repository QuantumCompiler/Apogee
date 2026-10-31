import BusinessReads
import DataProtocol
import Foundation
import Testing

@testable import PresentationModels

private func recorded(_ name: String, _ ext: String = "json") -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name).\(ext)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// The binary's verdicts on the pairing's commands (`Fixtures/drafter-pairing-
/// verdicts.txt`): 3b's, and the base side's appended (GUI M5).
private func pairingVerdicts() -> [String: String] {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/drafter-pairing-verdicts.txt")
    var out: [String: String] = [:]
    for line in ((try? String(contentsOf: url, encoding: .utf8)) ?? "").split(separator: "\n") {
        let parts = line.split(separator: "\t", maxSplits: 1).map(String.init)
        if parts.count == 2 { out[parts[1]] = parts[0] }
    }
    return out
}

/// GUI M5: the Backends table says each `mlx` entry's kind and marks a base
/// with a drafter paired; a drafter is paired from its base's details; a
/// pairing that can no longer draft is drawn in the CLI's words. Every
/// document is recorded from the branch's binary over sandbox homes, the home
/// written `/Users/someone`, with tiny fake MLX directories as the CLI's own
/// tests make them:
/// - `base-pair-*`: mlx-vlm's builds of Gemma 4 E4B (2560 wide) and 12B
///   (3840), mlx-lm's Qwen3 -- registered by `config scan --register` -- and
///   two drafters, the E4B's assistant (its record naming Gemma4-E4B) and the
///   12B's (naming Gemma4-12B), over a Python environment holding only the
///   files the CLI reads to decide a pairing. `models list` is the same bytes
///   before and after the pairing: it says nothing of it. Each base's
///   completion of `--draft` offers its own drafter alone.
/// - `draft-gone-*`: the E4B paired, then its drafter's directory deleted.
/// - `draft-rebuilt-*`: the E4B paired, then its directory recorded as
///   mlx-lm's build (`chat`), as a conversion again would leave it.
@Suite("BackendsKindAndDrafter", .timeLimit(.minutes(1)))
@MainActor
struct BackendsKindAndDrafterTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let root = "/Users/someone/.apogee/models/"
    static let drafter = "Gemma4-E4B-Assistant/mlx/4d5e6f708192"
    static let drafter12B = "Gemma4-12B-Assistant/mlx/8192a3b4c5d6"
    static let bases = ["Gemma4-E4B-4bit", "Gemma4-12B-4bit", "Qwen3-8B-4bit"]

    /// The base-side home; `paired`, the E4B's `models info` as read back
    /// after `config set-backend Gemma4-E4B-4bit --draft <its drafter>`.
    static func basePair(paired: Bool = false) -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("base-pair-list"))
        runner.reading(["models", "status"], recorded("models-status-kinds-registered"))
        for base in bases {
            let name = "base-pair-info-backend-\(base)" + (paired && base == "Gemma4-E4B-4bit" ? "-paired" : "")
            runner.reading(["models", "info", base], recorded(name))
            runner.commanding(["__complete", "config", "set-backend", base, "--draft"], .done(stdout: recorded("base-pair-complete-\(base)", "txt")))
        }
        runner.reading(["models", "info", drafter], recorded("base-pair-info-drafter"))
        runner.reading(["models", "info", drafter12B], recorded("base-pair-info-drafter-12b"))
        return runner
    }

    /// A home whose E4B's pairing can no longer draft: `gone`, its drafter
    /// deleted; else its base converted again as mlx-lm's build.
    static func cannotDraft(gone: Bool) -> ScriptedRunner {
        let home = gone ? "draft-gone" : "draft-rebuilt"
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("\(home)-list"))
        runner.reading(["models", "status"], recorded("models-status-kinds-registered"))
        runner.reading(["models", "info", "Gemma4-E4B-4bit"], recorded("\(home)-info-backend"))
        return runner
    }

    func connected(_ runner: ScriptedRunner) async -> ModelsViewModel {
        let model = ModelsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        return model
    }

    func row(_ backend: String, in model: ModelsViewModel) throws -> ModelRow {
        try #require(model.rows.first { $0.backend == backend && !$0.isStored }, "rows: \(model.rows.map(\.name))")
    }

    // MARK: - The table's kinds

    @Test("the Backends table says each mlx entry's kind beside its format, as Stored weights says it: MLX · vision, then its precision")
    func tableSaysEachKind() async throws {
        let model = await connected(Self.basePair())
        #expect(ModelsViewModel.formatLines(try row("Gemma4-E4B-4bit", in: model)) == ["MLX · vision", "4-bit (affine, group 64)"])
        #expect(ModelsViewModel.formatLines(try row("Gemma4-12B-4bit", in: model)) == ["MLX · vision", "4-bit (affine, group 64)"])
        #expect(ModelsViewModel.formatLines(try row("Qwen3-8B-4bit", in: model)) == ["MLX · chat", "4-bit (affine, group 64)"])
        // Every kind the CLI writes, and a word this app does not know, as written.
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("store-list-kinds-by-hand"))
        let byHand = await connected(runner)
        for (backend, kind) in [("Gemma4-E4B-Assistant", "drafter"), ("Kokoro-82M", "voice"), ("MusicGen-Small", "music"), ("Wan2.2-TI2V-5B", "video")] {
            #expect(ModelsViewModel.formatLines(try row(backend, in: byHand)) == ["MLX · \(kind)", "4-bit (affine, group 64)"])
        }
        let registered = ScriptedRunner()
        registered.reading(["models", "list"], recorded("store-list-kinds-registered"))
        #expect(ModelsViewModel.formatLines(try row("EmbeddingGemma-300M-bf16", in: await connected(registered))) == ["MLX · embedding", "bf16"])
    }

    // MARK: - The paired mark

    @Test("a base with a drafter paired is marked, the drafter named by its handle and the CLI's words in its tooltip; the others are not")
    func pairedBaseMarked() async throws {
        let runner = Self.basePair(paired: true)
        let model = await connected(runner)
        let mark = try #require(model.drafterMark(try row("Gemma4-E4B-4bit", in: model)), "the E4B drafts")
        #expect(mark == DrafterMark(
            drafter: Self.drafter, drafts: true,
            said: "drafts beside it through mlx-vlm's speculative path (mtp, mlx-vlm's default block); the answer is the base's"))
        #expect(mark.help == "Drafter: \(Self.drafter) -- drafts beside it through mlx-vlm's speculative path (mtp, mlx-vlm's default block); the answer is the base's")
        let wider = try row("Gemma4-12B-4bit", in: model), qwen = try row("Qwen3-8B-4bit", in: model)
        #expect(model.drafterMark(wider) == nil && model.drafterMark(qwen) == nil)
        // The list says nothing of a pairing: each mlx entry's models info is
        // read with it, once.
        for base in Self.bases {
            #expect(runner.calls.filter { $0 == "read models info \(base)" }.count == 1, "\(base)'s models info, once")
        }
        // Unpaired, and read back: the mark goes, and Pair a drafter… is offered.
        let e4b = try row("Gemma4-E4B-4bit", in: model)
        runner.reading(["models", "info", "Gemma4-E4B-4bit"], recorded("base-pair-info-backend-Gemma4-E4B-4bit"))
        await model.unpair(e4b)
        #expect(runner.calls.contains("run config set-backend Gemma4-E4B-4bit --draft "))
        #expect(model.drafterMark(e4b) == nil && model.offersDrafterPairing(e4b))
    }

    // MARK: - Pairing from the base

    @Test("Pair a drafter… is offered on a vision mlx entry with none paired, and offers exactly the drafters the binary's completion offers it")
    func pairFromTheBase() async throws {
        let runner = Self.basePair()
        let model = await connected(runner)
        #expect(model.offersDrafterPairing(try row("Gemma4-E4B-4bit", in: model)))
        #expect(model.offersDrafterPairing(try row("Gemma4-12B-4bit", in: model)))
        #expect(!model.offersDrafterPairing(try row("Qwen3-8B-4bit", in: model)), "mlx-lm's build: no drafter runs beside it")
        #expect(!model.rows.filter(\.isStored).contains { model.offersDrafterPairing($0) }, "stored weights are no backend")
        let e4b = try row("Gemma4-E4B-4bit", in: model)
        await model.select(e4b)
        #expect(model.draft == nil)
        await model.askToPairDrafter(e4b)
        let form = try #require(model.pairingDrafter)
        #expect(form.backend == "Gemma4-E4B-4bit")
        #expect(form.offers == [DrafterPairing.Offer(
            handle: Self.drafter, path: Self.root + Self.drafter,
            said: "a drafter (Gemma4AssistantForCausalLM) for a 2560-wide gemma4 model, its record naming Gemma4-E4B")])
        #expect(form.chosen == Self.drafter)
        #expect(form.arguments == ["config", "set-backend", "Gemma4-E4B-4bit", "--draft", Self.root + Self.drafter])
        #expect(runner.calls.contains("run __complete config set-backend Gemma4-E4B-4bit --draft "))
        #expect(!runner.calls.contains("read models info \(Self.drafter12B)"), "the 12B's drafter is not offered, so not even read")
        model.pairingDrafter = nil
        await model.askToPairDrafter(try row("Gemma4-12B-4bit", in: model))
        #expect(model.pairingDrafter?.offers.map(\.handle) == [Self.drafter12B])
        #expect(model.pairingDrafter?.offers.first?.said == "a drafter (Gemma4UnifiedAssistantForCausalLM) for a 3840-wide gemma4 model, its record naming Gemma4-12B")
        model.pairingDrafter = nil
        let asked = runner.calls.count
        await model.askToPairDrafter(try row("Qwen3-8B-4bit", in: model))
        #expect(model.pairingDrafter == nil && runner.calls.count == asked, "nothing asked for mlx-lm's build")
    }

    @Test("the base-side pairing's commands are held to the binary's verdicts: each base's own drafter taken, another width's refused in its words")
    func pairFromTheBaseHeldToVerdicts() async throws {
        let verdicts = pairingVerdicts()
        #expect(verdicts.count == 7, "3b's five and the base side's two: \(verdicts.keys.sorted())")
        let model = await connected(Self.basePair())
        for base in ["Gemma4-E4B-4bit", "Gemma4-12B-4bit"] {
            await model.askToPairDrafter(try row(base, in: model))
            let command = try #require(model.pairingDrafter?.arguments).joined(separator: " ")
            #expect(verdicts[command] == "taken", "\(command): \(verdicts[command] ?? "never recorded")")
            model.pairingDrafter = nil
        }
        let notOffered = "config set-backend Gemma4-E4B-4bit --draft \(Self.root + Self.drafter12B)"
        #expect(verdicts[notOffered]?.contains("drafts for a 3840-wide gemma4 model, reading its hidden states, and /Users/someone/.apogee/models/Gemma4-E4B/mlx/2b3c4d5e6f70 is a 2560-wide gemma4 model") == true,
                "the drafter the E4B's completion leaves out is refused: \(verdicts[notOffered] ?? "never recorded")")
        #expect(recorded("base-pair-complete-Gemma4-E4B-4bit", "txt") == recorded("drafts-complete-Gemma4-E4B-4bit", "txt"),
                "the E4B's completion, recorded again with a second drafter stored, is 3b's byte for byte")
    }

    @Test("pairing from the base runs the command and reads it back: the details say the drafter, the table marks it, Pair a drafter… goes")
    func pairsAndReadsBack() async throws {
        let runner = Self.basePair()
        let model = await connected(runner)
        var said: [String] = []
        model.say = { said.append($0) }
        let e4b = try row("Gemma4-E4B-4bit", in: model)
        await model.select(e4b)
        await model.askToPairDrafter(e4b)
        runner.commanding(["config", "set-backend"], .done(stdout: recorded("drafts-pair", "txt")))
        runner.reading(["models", "info", "Gemma4-E4B-4bit"], recorded("base-pair-info-backend-Gemma4-E4B-4bit-paired"))
        await model.pairDrafter()
        #expect(model.pairingDrafter == nil)
        #expect(runner.calls.contains("run config set-backend Gemma4-E4B-4bit --draft \(Self.root + Self.drafter)"))
        #expect(runner.calls.filter { $0 == "read models list" }.count == 2, "read back")
        let draft = try #require(model.draft, "the details say it")
        #expect(draft.path == Self.root + Self.drafter && draft.drafts && draft.refusal == nil)
        #expect(model.drafterMark(e4b)?.drafts == true)
        #expect(!model.offersDrafterPairing(e4b))
        #expect(said.last == "Gemma4-E4B-4bit drafts with \(Self.drafter)")
        #expect(model.failure == nil)
    }

    @Test("a vision base no stored drafter pairs with: the sheet offers none and builds no command")
    func noDrafterOffered() async throws {
        // 3b's home: a 12B vision base, and only the E4B's drafter stored.
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("drafts-list"))
        runner.reading(["models", "info", "Gemma4-12B-4bit"], recorded("drafts-info-backend-Gemma4-12B-4bit"))
        runner.commanding(["__complete", "config", "set-backend", "Gemma4-12B-4bit", "--draft"], .done(stdout: recorded("drafts-complete-Gemma4-12B-4bit", "txt")))
        let model = await connected(runner)
        let wider = try row("Gemma4-12B-4bit", in: model)
        #expect(model.offersDrafterPairing(wider))
        await model.askToPairDrafter(wider)
        let form = try #require(model.pairingDrafter)
        #expect(form.offers.isEmpty && form.chosen == nil && form.arguments == nil)
        #expect(!runner.calls.contains { $0.hasPrefix("read models info Gemma4-E4B-Assistant") })
    }

    // MARK: - A pairing that can no longer draft

    @Test("a pairing whose drafter was deleted is drawn in the CLI's words: the mark says it cannot draft, the details say why, Unpair is the CLI's way out")
    func drafterGone() async throws {
        let runner = Self.cannotDraft(gone: true)
        let model = await connected(runner)
        let e4b = try row("Gemma4-E4B-4bit", in: model)
        let path = Self.root + Self.drafter
        let why = "there is no drafter at \(path) -- 'apogee models convert <a draft head> --mlx' makes one"
        let mark = try #require(model.drafterMark(e4b))
        #expect(mark == DrafterMark(drafter: path, drafts: false, said: "cannot draft beside it: " + why), "named by its path: the store lists it no more")
        #expect(mark.help == "Drafter: \(path) -- cannot draft beside it: \(why)")
        #expect(!model.offersDrafterPairing(e4b), "a pairing stands: Unpair is its way out")
        await model.select(e4b)
        let draft = try #require(model.draft)
        #expect(!draft.drafts && draft.refusal == why && draft.drafter == nil)
        // The same words the binary refuses the backend in.
        #expect(e4b.state == "draft" && e4b.note?.hasPrefix("its draft cannot run beside it: " + why) == true)
        let check = try #require(ReadDocument(text: recorded("draft-gone-check")))
        let checkRow = try #require(check.value["rows"]?.arrayValue?.first { $0["name"]?.stringValue == "backend: Gemma4-E4B-4bit" })
        #expect(checkRow["detail"]?.stringValue == "mlx -- its draft cannot run beside it: " + why)
        #expect(checkRow["remedy"]?.stringValue == "apogee config set-backend Gemma4-E4B-4bit --draft \"\"", "the doctor's way out is Unpair")
        #expect(recorded("draft-gone-chat", "err").contains("Gemma4-E4B-4bit: its draft cannot run beside it: " + why))
        #expect(pairingVerdicts()["config set-backend Gemma4-E4B-4bit --draft \"\""] == "taken")
        await model.unpair(e4b)
        #expect(runner.calls.contains("run config set-backend Gemma4-E4B-4bit --draft "))
    }

    @Test("a pairing whose base was converted again as mlx-lm's build: the table says MLX · chat, and the mark and details the CLI's refusal")
    func baseRebuiltAsChat() async throws {
        let model = await connected(Self.cannotDraft(gone: false))
        let e4b = try row("Gemma4-E4B-4bit", in: model)
        #expect(ModelsViewModel.formatLines(e4b) == ["MLX · chat", "4-bit (affine, group 64)"])
        let why = "/Users/someone/.apogee/models/Gemma4-E4B/mlx/2b3c4d5e6f70 is mlx-lm's build (kind chat, recorded), laid out for mlx-lm, and drafting runs through mlx-vlm's speculative path, which loads the base through mlx-vlm -- 'apogee models convert <its SafeTensors set> --mlx' builds it through mlx-vlm, and the drafter runs beside that"
        #expect(model.drafterMark(e4b) == DrafterMark(drafter: Self.drafter, drafts: false, said: "cannot draft beside it: " + why),
                "the drafter is still stored: named by its handle")
        #expect(!model.offersDrafterPairing(e4b))
        await model.select(e4b)
        #expect(model.draft?.refusal == why && model.draft?.drafts == false)
        #expect(model.draft?.drafter == "a drafter (Gemma4AssistantForCausalLM) for a 2560-wide gemma4 model, its record naming Gemma4-E4B")
        let check = try #require(ReadDocument(text: recorded("draft-rebuilt-check")))
        #expect(check.value["rows"]?.arrayValue?.contains { $0["detail"]?.stringValue == "mlx -- its draft cannot run beside it: " + why } == true)
        #expect(recorded("draft-rebuilt-chat", "err").contains("Gemma4-E4B-4bit: its draft cannot run beside it: " + why))
    }

    // MARK: - An older binary

    @Test("against a binary before the CLI's 53e and 53i the table and details read as today: no kind, no mark, no drafter read, no pairing offered")
    func olderBinaryKeepsTodaysTable() async throws {
        let runner = ScriptedRunner()
        let model = await ModelsViewModelTests().connected(runner)
        #expect(ModelsViewModel.formatLines(try row("qwen-mlx", in: model)) == ["mlx"])
        #expect(ModelsViewModel.formatLines(try row("qwen3-8b-Q4KM", in: model)) == ["gguf · Q4_K_M"])
        #expect(ModelsViewModel.formatLines(try row("claude", in: model)) == [""])
        #expect(!runner.calls.contains { $0.hasPrefix("read models info") }, "no drafter is read: \(runner.calls)")
        #expect(model.rows.allSatisfy { model.drafterMark($0) == nil && !model.offersDrafterPairing($0) })
        runner.reading(["models", "info", "qwen-mlx"], #"{"name":"qwen-mlx","fields":[{"field":"backend","value":"qwen-mlx"},{"field":"path","value":"/Users/someone/.apogee/models/mlx/qwen"}]}"#)
        await model.select(try row("qwen-mlx", in: model))
        #expect(model.draft == nil && model.recordFacts == model.facts)
        // 2h's store, recorded from a binary before 53e.
        let store = ScriptedRunner()
        store.reading(["models", "list"], recorded("store-list"))
        let recordedModel = await connected(store)
        #expect(ModelsViewModel.formatLines(try row("llama3.2-1b-Q4KM", in: recordedModel)) == ["gguf"])
        #expect(!store.calls.contains { $0.hasPrefix("read models info") })
    }
}
