import BusinessJobs
import BusinessReads
import Foundation
import Testing

@testable import PresentationTraining

/// GUI M4: the Training place offers only backends a chat runs on -- the
/// synthesize sheet's teacher and the adapter promote's judge, roles that
/// chat, and the entry an adapter goes onto, a target to train. Over 3a's
/// registered kinds and entries written into the config by hand over every
/// kind that answers no chat, recorded from the binary (each folds nothing,
/// so `models list --all` prints the same bytes); and over lists from a
/// binary before the CLI's 53e, exactly as before.
@Suite("TrainingChatChoices", .timeLimit(.minutes(1)))
@MainActor
struct TrainingChatChoicesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func training(models: String) async -> TrainingViewModel {
        let runner = ScriptedRunner()
        runner.reading(["train", "status"], fixture("train-status-passed"))
        runner.reading(["train", "versions"], fixture("train-versions-adapters"))
        runner.reading(["train", "regime", "list"], fixture("train-regimes"))
        runner.reading(["datasets", "kits"], fixture("datasets-kits"))
        runner.reading(["datasets", "list"], fixture("datasets-list"))
        runner.reading(["models", "list"], models)
        let model = TrainingViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        return model
    }

    @Test("the synthesize sheet's teacher: the registered chat and vision entries, never the embedding entry, which it no longer opens on")
    func teacherOverRegisteredKinds() async {
        let model = await training(models: fixture("store-list-kinds-registered"))
        #expect(model.backends == ["Gemma4-E4B-4bit", "Qwen3-8B-4bit"])
        model.newSynthesis()
        #expect(model.synthForm.teacher == "Gemma4-E4B-4bit", "the first that chats, where the embedding entry stood first")
        model.synthForm.name = "notes"
        #expect(model.synthForm.arguments?.contains("EmbeddingGemma-300M-bf16") == false)
    }

    @Test("the adapter promote's judge: the registered chat and vision entries, never the embedding entry")
    func judgeOverRegisteredKinds() async {
        let model = await training(models: fixture("store-list-kinds-registered"))
        model.newPromotion(of: model.runs[0])
        model.promoteForm.adapter = true
        #expect(model.backends == ["Gemma4-E4B-4bit", "Qwen3-8B-4bit"], "the judge's choices")
        #expect(!model.backends.contains("EmbeddingGemma-300M-bf16"))
        model.promoteForm.backend = "Qwen3-8B-4bit"
        model.promoteForm.judge = model.backends.first
        #expect(model.promoteForm.arguments == ["train", "promote", model.runs[0].id, "--as", "Qwen3-8B-4bit", "--adapter", "--judge", "Gemma4-E4B-4bit"])
    }

    @Test("the entry an adapter goes onto: the registered chat and vision entries, never the embedding entry")
    func ontoOverRegisteredKinds() async {
        let model = await training(models: fixture("store-list-kinds-registered"))
        #expect(model.adapterBackends == ["Gemma4-E4B-4bit", "Qwen3-8B-4bit"])
        #expect(!model.adapterBackends.contains("EmbeddingGemma-300M-bf16"))
    }

    @Test("an mlx entry written by hand over a drafter, a video model, a voice model or a kind this app does not know: no teacher, judge or target")
    func entriesByHand() async {
        let model = await training(models: fixture("store-list-kinds-by-hand"))
        #expect(model.backends == ["Qwen3-8B-4bit"])
        #expect(model.adapterBackends == ["Qwen3-8B-4bit"])
    }

    @Test("a list from a binary before the CLI's 53e says no kind: the same teachers, judges and targets as before")
    func olderBinary() async {
        let stored = await training(models: fixture("models-stored-list-all"))
        #expect(stored.backends == ["mock-chat"])
        #expect(stored.adapterBackends.isEmpty, "a mock entry takes no adapters")
        #expect(stored.students.count == 3, "the students, stored SafeTensors sets, are untouched")
    }

    @Test("llamacpp entries carry no MLX kind: each a teacher, a judge and a target, as before")
    func rowsWithNoKind() async {
        let adapters = await training(models: fixture("models-list-adapters"))
        #expect(adapters.backends == ["based", "q4", "tuned"])
        #expect(adapters.adapterBackends == ["based", "q4", "tuned"])
        #expect(adapters.students == ["tiny/safetensors/aaaaaaaaaaaa"])
    }
}
