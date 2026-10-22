import BusinessReads
import Foundation
import Testing

@testable import PresentationModels

private func recorded(_ name: String, _ ext: String = "json") -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name).\(ext)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// The binary's verdict on the pairing's commands (`Fixtures/drafter-pairing-
/// verdicts.txt`), each run in a copy of the recorded sandbox: `taken`, or
/// `refused: <its first line>`.
private func pairingVerdicts() -> [String: String] {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/drafter-pairing-verdicts.txt")
    var out: [String: String] = [:]
    for line in ((try? String(contentsOf: url, encoding: .utf8)) ?? "").split(separator: "\n") {
        let parts = line.split(separator: "\t", maxSplits: 1).map(String.init)
        if parts.count == 2 { out[parts[1]] = parts[0] }
    }
    return out
}

/// A drafter paired with its base (the GUI's 3b, the CLI's 53i), every
/// document recorded from the binary built with 53i over a sandbox store and
/// a Python environment holding only the files the CLI reads to decide a
/// pairing (mlx-vlm's gemma4 module and its gemma4_assistant drafter):
/// mlx-vlm's build of Gemma 4 E4B (2560 wide), its assistant's drafter (its
/// record naming Gemma4-E4B), mlx-vlm's build of a wider Gemma 4 (3840) and
/// mlx-lm's Qwen3 -- the three registered by `config scan --register` -- and
/// each backend's completion of `--draft`. Nothing is folded there, so one
/// document answers `models list` and `models list --all`.
@Suite("DrafterPairing", .timeLimit(.minutes(1)))
@MainActor
struct DrafterPairingTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let drafter = "Gemma4-E4B-Assistant/mlx/4d5e6f708192"
    static let drafterPath = "/Users/someone/.apogee/models/" + drafter
    static let sets = [
        ("Gemma4-E4B-4bit", "Gemma4-E4B/mlx/2b3c4d5e6f70"), ("Gemma4-12B-4bit", "Gemma4-12B/mlx/7a8b9c0d1e2f"),
        ("Qwen3-8B-4bit", "Qwen3-8B/mlx/1a2b3c4d5e6f"),
    ]

    /// The recorded store; `paired`, as read back after the pairing.
    func runner(paired: Bool = false) -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("drafts-list"))
        runner.reading(["models", "info", Self.drafter], recorded(paired ? "drafts-info-drafter-paired" : "drafts-info-drafter"))
        for (backend, handle) in Self.sets {
            let pairedBase = paired && backend == "Gemma4-E4B-4bit"
            runner.reading(["models", "info", backend], recorded("drafts-info-backend-\(backend)" + (pairedBase ? "-paired" : "")))
            runner.reading(["models", "info", "/Users/someone/.apogee/models/" + handle], recorded("drafts-info-path-\(backend)"))
            runner.reading(["models", "info", handle], recorded("drafts-info-set-\(backend)"))
            runner.commanding(["__complete", "config", "set-backend", backend, "--draft"], .done(stdout: recorded("drafts-complete-\(backend)", "txt")))
        }
        return runner
    }

    func store(_ runner: ScriptedRunner) async throws -> (StoreViewModel, StoredSet) {
        let store = StoreViewModel(runner: runner)
        await store.connect(binary: Self.binary)
        await store.refresh()
        let set = try #require(store.sets.first { $0.handle == Self.drafter }, "sets: \(store.sets.map(\.handle))")
        return (store, set)
    }

    @Test("Pair with its base offers only the backends the binary completes for the drafter: its base, never another width or mlx-lm's build")
    func offersTheCLIsBases() async throws {
        let runner = runner()
        let (store, set) = try await store(runner)
        #expect(set.mlxKind == .drafter && store.actions(set) == [.pairWithBase, .checkFiles, .showInFinder, .delete])
        await store.askToPair(set)
        let form = try #require(store.pairing)
        #expect(form.bases == [StoreViewModel.PairForm.Base(backend: "Gemma4-E4B-4bit", handle: "Gemma4-E4B/mlx/2b3c4d5e6f70")])
        #expect(form.base == "Gemma4-E4B-4bit" && form.draft == Self.drafterPath, "the drafter's directory, as its models info names it")
        #expect(form.said == "a drafter (Gemma4AssistantForCausalLM) for a 2560-wide gemma4 model, its record naming Gemma4-E4B", "the CLI's own line")
        for (backend, _) in Self.sets {
            #expect(runner.calls.contains("run __complete config set-backend \(backend) --draft "), "asked of \(backend)")
        }
        #expect(form.arguments == ["config", "set-backend", "Gemma4-E4B-4bit", "--draft", Self.drafterPath])
    }

    @Test("the pairing's commands are pinned to the binary's verdicts: the base and the clearing taken, the other bases refused in its words")
    func commandsPinned() async throws {
        let verdicts = pairingVerdicts()
        #expect(verdicts.count == 5, "recorded: \(verdicts.keys.sorted())")
        let (store, set) = try await store(runner())
        await store.askToPair(set)
        let pair = try #require(store.pairing?.arguments).joined(separator: " ")
        #expect(verdicts[pair] == "taken", "\(pair): \(verdicts[pair] ?? "never recorded")")
        #expect(recorded("drafts-pair", "txt").contains("set draft; drafter \(Self.drafterPath) beside /Users/someone/.apogee/models/Gemma4-E4B/mlx/2b3c4d5e6f70"),
                "the binary's own words for the pairing it took")
        #expect(verdicts["config set-backend Gemma4-E4B-4bit --draft \"\""] == "taken", "Unpair's command")
        #expect(recorded("drafts-unpair", "txt").contains("edited backend 'Gemma4-E4B-4bit'") && recorded("drafts-unpair", "txt").contains("cleared draft"))
        #expect(verdicts["config set-backend Gemma4-E4B-4bit --draft \(Self.drafter)"] == "taken", "the handle the completion offers is taken too")
        #expect(verdicts["config set-backend Gemma4-12B-4bit --draft \(Self.drafterPath)"]?.contains("is a 3840-wide gemma4 model -- a drafter runs only beside a base of its family and width") == true)
        #expect(verdicts["config set-backend Qwen3-8B-4bit --draft \(Self.drafterPath)"]?.contains("is mlx-lm's build (kind chat, recorded)") == true)
    }

    @Test("pairing runs the command and reads it back: the drafter is the draft of its base, and offers Unpair")
    func pairsAndReadsBack() async throws {
        let runner = runner()
        let (store, set) = try await store(runner)
        await store.askToPair(set)
        runner.commanding(["config", "set-backend"], .done(stdout: recorded("drafts-pair", "txt")))
        runner.reading(["models", "info", Self.drafter], recorded("drafts-info-drafter-paired"))
        runner.reading(["models", "info", "Gemma4-E4B-4bit"], recorded("drafts-info-backend-Gemma4-E4B-4bit-paired"))
        await store.pair()
        #expect(store.pairing == nil)
        #expect(runner.calls.contains("run config set-backend Gemma4-E4B-4bit --draft \(Self.drafterPath)"))
        #expect(runner.calls.filter { $0 == "read models list --all" }.count == 2, "read back")
        let paired = try #require(store.sets.first { $0.handle == Self.drafter })
        #expect(store.pairedWith(paired) == ["Gemma4-E4B-4bit"], "as the base's models info names its draft")
        #expect(store.actions(paired) == [.pairWithBase, .unpair(backend: "Gemma4-E4B-4bit"), .checkFiles, .showInFinder, .delete])
        #expect(store.advice(paired)?.hasPrefix("The draft of Gemma4-E4B-4bit") == true)
        #expect(store.infos[Self.drafter]?.field("backends") == "none of its own; the draft of Gemma4-E4B-4bit", "the CLI says the same")
        await store.unpair("Gemma4-E4B-4bit")
        #expect(runner.calls.contains("run config set-backend Gemma4-E4B-4bit --draft "), "--draft \"\": the argument is empty")
    }

    @Test("no backend runs a base the drafter pairs with: the sheet offers none and builds no command")
    func noBase() async throws {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("store-list-kinds"))
        runner.reading(["models", "info", Self.drafter], recorded("store-info-kind-drafter"))
        let (store, set) = try await store(runner)
        await store.askToPair(set)
        let form = try #require(store.pairing)
        #expect(form.bases.isEmpty && form.base == nil && form.arguments == nil)
        #expect(!runner.calls.contains { $0.hasPrefix("run __complete") }, "no mlx backend to ask of")
        #expect(store.advice(set)?.hasPrefix("A drafter runs beside its base") == true)
    }

    @Test("the backend's details say its drafter in the CLI's words, apart from the record, with Unpair as --draft \"\"")
    func backendSaysItsDrafter() async throws {
        let runner = runner(paired: true)
        runner.reading(["models", "status"], recorded("models-status-kinds-registered"))
        let models = ModelsViewModel(runner: runner)
        await models.connect(binary: Self.binary)
        let row = try #require(models.rows.first { $0.backend == "Gemma4-E4B-4bit" })
        await models.select(row)
        let draft = try #require(models.draft)
        #expect(draft.path == Self.drafterPath)
        #expect(draft.said == "drafts beside it through mlx-vlm's speculative path (mtp, mlx-vlm's default block); the answer is the base's")
        #expect(draft.drafter == "a drafter (Gemma4AssistantForCausalLM) for a 2560-wide gemma4 model, its record naming Gemma4-E4B")
        #expect(models.facts.contains { $0.label == "draft" } && !models.recordFacts.contains { $0.label == "draft" || $0.label == "drafter" })
        runner.reading(["models", "info", "Gemma4-E4B-4bit"], recorded("drafts-info-backend-Gemma4-E4B-4bit"))
        await models.unpair(row)
        #expect(runner.calls.contains("run config set-backend Gemma4-E4B-4bit --draft "))
        #expect(models.draft == nil, "read back: no drafter")
        #expect(pairingVerdicts()["config set-backend Gemma4-E4B-4bit --draft \"\""] == "taken")
    }

    @Test("against a binary before the CLI's 53i nothing changes: no draft in a backend's details, its record whole")
    func olderBinary() async throws {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("store-list-kinds-registered"))
        runner.reading(["models", "status"], recorded("models-status-kinds-registered"))
        runner.reading(["models", "info", "Gemma4-E4B-4bit"], recorded("store-info-kinds-backend-vision"))
        let models = ModelsViewModel(runner: runner)
        await models.connect(binary: Self.binary)
        await models.select(try #require(models.rows.first { $0.backend == "Gemma4-E4B-4bit" }))
        #expect(models.draft == nil && models.recordFacts == models.facts && !models.facts.isEmpty)
    }
}
