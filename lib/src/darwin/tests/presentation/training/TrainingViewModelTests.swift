import BusinessJobs
import BusinessReads
import Foundation
import Testing

@testable import PresentationTraining

func fixture(_ name: String, _ ext: String = "json") -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name).\(ext)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

@Suite("TrainingViewModel", .timeLimit(.minutes(1)))
@MainActor
struct TrainingViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func training(_ jobs: [ScriptedJob] = []) async -> (TrainingViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["train", "status"], fixture("train-status"))
        runner.reading(["train", "versions"], #"{"object":"list","data":[]}"#)
        runner.reading(["train", "regime", "list"], fixture("train-regimes"))
        runner.reading(["datasets", "kits"], fixture("datasets-kits"))
        runner.reading(["datasets", "list"], fixture("datasets-list"))
        runner.reading(["models", "list"], fixture("models-stored-list-all"))
        runner.answering(["datasets"], .done(stdout: "done\n"))
        let starter = ScriptedJobs(jobs)
        let model = TrainingViewModel(runner: runner, starter: starter)
        await model.connect(binary: Self.binary)
        return (model, runner, starter)
    }

    @Test("every document read: runs, regimes, kits, datasets; students are the stored SafeTensors sets by handle")
    func reads() async {
        let (model, runner, _) = await training()
        #expect(model.runs.count == 1 && model.regimes.count == 3 && model.datasets.count == 1 && !model.kits.isEmpty)
        #expect(runner.calls.contains("read models|list|--all"))
        #expect(model.students == [
            "Gemma4-26B-Instruct/safetensors/13de95725073", "Llama3.2-1B-Instruct/safetensors/edaba290ce7f", "Qwen3-VL-8B-Instruct/safetensors/63f2634a2019",
        ])
        #expect(model.backends == ["mock-chat"])
        #expect(TrainingViewModel.gate(model.runs[0]) == "failed the gate (46%) -- promote refuses it unless forced")
    }

    @Test("a run is train run as typed, launched only from the sheet, supervised; its busy line the trainer's words")
    func run() async {
        var form = TrainingViewModel.RunForm()
        #expect(form.arguments == nil)
        form.student = "Llama3.2-1B-Instruct/safetensors/edaba290ce7f"
        form.dataset = "heat-notes"
        #expect(form.arguments == ["train", "run", "Llama3.2-1B-Instruct/safetensors/edaba290ce7f", "--dataset", "heat-notes"])
        form.method = "qlora"
        form.iters = " 200 "
        form.batchSize = "2"
        form.gradCheckpoint = true
        form.maskPrompt = true
        #expect(form.arguments == ["train", "run", "Llama3.2-1B-Instruct/safetensors/edaba290ce7f", "--dataset", "heat-notes",
            "--method", "qlora", "--iters", "200", "--batch-size", "2", "--grad-checkpoint", "--mask-prompt"])
        let job = ScriptedJob()
        let (model, _, starter) = await training([job])
        model.newRun()
        #expect(model.sheet == .run && model.runForm.student.hasPrefix("Gemma4") && model.runForm.dataset == "heat-notes")
        #expect(starter.starts.isEmpty, "nothing starts until the person launches")
        model.startRun()
        #expect(starter.starts.count == 1 && model.sheet == nil && model.running == "Training")
        job.say(.busy("[train] iter 2/4 · loss 0.6200 · lr 1.00e-04 · 10.0 it/s"))
        #expect(await eventually { model.run?.busy == "[train] iter 2/4 · loss 0.6200 · lr 1.00e-04 · 10.0 it/s" })
        job.end()
        #expect(await eventually { !model.isRunning })
    }

    @Test("gate, promote and a regime are their verbs, supervised; promote carries --force only when chosen")
    func gateAndPromote() async {
        let gate = ScriptedJob()
        let promote = ScriptedJob()
        let regime = ScriptedJob()
        let (model, _, starter) = await training([gate, promote, regime])
        let run = model.runs[0]
        model.gate(run)
        #expect(starter.starts.last == ["train", "eval", run.id])
        gate.end()
        #expect(await eventually { !model.isRunning })
        model.newPromotion(of: run)
        model.promoteForm.backend = "helper"
        #expect(model.promoteForm.arguments == ["train", "promote", run.id, "--as", "helper"])
        model.promoteForm.target = "gguf"
        model.promoteForm.quantize = "Q4_K_M"
        model.promoteForm.force = true
        model.promote()
        #expect(starter.starts.last == ["train", "promote", run.id, "--as", "helper", "--target", "gguf", "--quantize", "Q4_K_M", "--force"])
        promote.end(status: 1, stderr: "apogee train: run failed its gate")
        #expect(await eventually { !model.isRunning })
        model.runRegime(model.regimes[0])
        #expect(starter.starts.last == ["train", "regime", "run", "foundation"])
        regime.end()
    }

    @Test("datasets: created and deleted as their verbs, the delete only after the person's word; synthesis supervised")
    func datasets() async {
        let synth = ScriptedJob()
        let (model, runner, starter) = await training([synth])
        model.newDataset()
        model.createName = "notes"
        model.createFrom = "sessions"
        await model.createDataset()
        #expect(runner.calls.contains("run datasets|create|notes|--from|sessions"))
        model.pending = .deleteDataset("heat-notes")
        await model.confirm()
        #expect(runner.calls.contains("run datasets|delete|heat-notes|--yes"))
        #expect(TrainingViewModel.Pending.rollback(backend: "helper").arguments == ["train", "rollback", "helper"])
        model.newSynthesis()
        model.synthForm.name = "clerk-data"
        model.synthForm.count = "50"
        model.synthesize()
        #expect(starter.starts.last == ["datasets", "synth", "clerk-data", "--teacher", "mock-chat", "--kit", model.kits[0].name, "--count", "50"])
        synth.end()
    }
}
