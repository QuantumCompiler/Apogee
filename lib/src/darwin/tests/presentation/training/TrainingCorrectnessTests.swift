import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationTraining

/// Training's buttons do what they say (GUI 4h): no apogee is an empty
/// state, never an alert each visit; a new dataset is selected only once the
/// binary made it.
@Suite("Training correctness", .timeLimit(.minutes(1)))
@MainActor
struct TrainingCorrectnessTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    @Test("with no apogee the place says so where its lists are, and no visit raises an alert")
    func missingBinaryIsEmpty() async {
        let runner = ScriptedRunner()
        let model = TrainingViewModel(runner: runner, starter: ScriptedJobs([]))
        #expect(model.missingText == nil, "not told yet: nothing to say")
        await model.connect(binary: nil)
        #expect(model.failure == nil)
        #expect(model.missingText == "Apogee isn't installed, so there is nothing to train.")
        for _ in 0..<3 { await model.appeared() }
        #expect(model.failure == nil, "every visit: no alert")
        #expect(runner.calls.isEmpty)
    }

    @Test("every failed read is said where what it reads is drawn, in the CLI's words, the rows read before kept, and no alert")
    func failedReadsSaid() async throws {
        let runner = ScriptedRunner()
        runner.reading(["train", "status"], fixture("train-status"))
        runner.reading(["train", "versions"], #"{"object":"list","data":[]}"#)
        runner.reading(["train", "regime", "list"], fixture("train-regimes"))
        runner.reading(["datasets", "kits"], fixture("datasets-kits"))
        runner.reading(["datasets", "list"], fixture("datasets-list"))
        runner.reading(["models", "list"], fixture("models-stored-list-all"))
        let model = TrainingViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        #expect([model.runsText, model.versionsText, model.regimesText, model.datasetsText, model.kitsText, model.modelsText].allSatisfy { $0 == nil })
        let kits = model.kits.count
        let students = model.students

        // Recorded: a config hand-edited into invalid JSON, which these two refuse.
        let exit1 = ChildExit(reason: .exited, status: 1)
        runner.failing(["train", "regime", "list"], .failed(exit: exit1, stderrTail: tail(fixture("train-regime-list-bad-config", "err").trimmingCharacters(in: .newlines))))
        runner.failing(["models", "list"], .failed(exit: exit1, stderrTail: tail(fixture("models-list-bad-config", "err").trimmingCharacters(in: .newlines))))
        // Scripted: nothing the probes did made these four fail (they read
        // tolerant stores and no config), so the reader's own outcomes stand in.
        runner.failing(["train", "status"], .timedOut(after: .seconds(60), stderrTail: BoundedTail()))
        runner.failing(["train", "versions"], .timedOut(after: .seconds(60), stderrTail: BoundedTail()))
        runner.failing(["datasets", "list"], .undecodable(stdout: "Traceback", stderrTail: BoundedTail()))
        runner.failing(["datasets", "kits"], .failed(exit: ChildExit(reason: .signalled, status: 9), stderrTail: BoundedTail()))
        await model.appeared()

        #expect(model.failure == nil, "said where each list is, never an alert")
        #expect(model.runs.count == 1 && model.regimes.count == 3 && model.datasets.count == 1, "the rows read before stay")
        #expect(model.kits.count == kits && model.students == students && !students.isEmpty)
        let invalid = "/Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)"
        #expect(model.regimesText == "The regimes could not be read: apogee train: \(invalid)")
        #expect(model.modelsText == "The stored models could not be read: apogee models: \(invalid)")
        #expect(model.runsText?.hasPrefix("The runs could not be read: it did not finish within") == true)
        #expect(model.versionsText?.hasPrefix("The promoted versions could not be read: it did not finish within") == true)
        #expect(model.datasetsText == "The datasets could not be read: apogee printed something that is not one JSON document")
        #expect(model.kitsText == "The kits could not be read: apogee said nothing (ended by signal 9)")
    }

    @Test("with no apogee each tab's list says so once; the secondary lists say nothing more")
    func missingSaidOnce() async {
        let model = TrainingViewModel(runner: ScriptedRunner(), starter: ScriptedJobs([]))
        await model.connect(binary: nil)
        let missing = "Apogee isn't installed, so there is nothing to train."
        #expect(model.runsText == missing && model.regimesText == missing && model.datasetsText == missing)
        #expect(model.versionsText == nil && model.kitsText == nil && model.modelsText == nil)
    }

    @Test("a new dataset is selected only when the binary made it; a refusal keeps the selection and is said verbatim")
    func createSelectsOnlyOnSuccess() async {
        let runner = ScriptedRunner()
        runner.reading(["train", "status"], fixture("train-status"))
        runner.reading(["train", "versions"], #"{"object":"list","data":[]}"#)
        runner.reading(["train", "regime", "list"], fixture("train-regimes"))
        runner.reading(["datasets", "kits"], fixture("datasets-kits"))
        runner.reading(["datasets", "list"], fixture("datasets-list"))
        runner.reading(["models", "list"], fixture("models-stored-list-all"))
        let taken = tail(fixture("datasets-create-taken", "err").trimmingCharacters(in: .newlines))
        runner.answering(["datasets", "create"], .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: taken))
        let model = TrainingViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        model.selectedDataset = "heat-notes"
        model.newDataset()
        model.createName = "notes"
        await model.createDataset()
        #expect(runner.calls.contains("run datasets|create|notes|--from|template"))
        #expect(model.selectedDataset == "heat-notes", "the prior selection kept, its page still drawn")
        #expect(model.failure == "apogee datasets: dataset already exists: /Users/someone/.apogee/training/datasets/notes.jsonl (pass --force to overwrite it) (exited with status 1)")
        model.dismissFailure()
        runner.answering(["datasets", "create"], .done(stdout: "/Users/someone/.apogee/training/datasets/clerk.jsonl\n2 example(s)\n"))
        model.newDataset()
        model.createName = "clerk"
        await model.createDataset()
        #expect(model.selectedDataset == "clerk")
    }
}
