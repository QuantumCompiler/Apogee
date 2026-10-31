import BusinessReads
import DataProtocol
import Foundation
import Testing

/// Recorded from the branch's binary (the CLI's 55h) over a sandbox home --
/// the mock trainer, two named pipelines (`nightly`, one stage; `skills`,
/// two, the second's data regressing the first's suite until it was fixed)
/// and the cycle over a queue directory with its breaker at 1 -- the home
/// written `/Users/someone` (`Fixtures/train-pipeline-*`, `train-cycle-*`,
/// `train-status-*`). A run was held mid-stage by not reading its stream
/// while these reads ran, so the running documents are the binary's own.
private func fixture(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name)")
    return try! String(contentsOf: url, encoding: .utf8)
}

private func document(_ name: String) -> ReadDocument {
    ReadDocument(text: fixture("\(name).json"))!
}

@Suite("Pipelines and the cycle")
struct PipelineCycleReadsTests {
    @Test("the pipeline runs as train pipeline status lists them: newest first, each its spec, status and stages passed")
    func pipelineList() throws {
        #expect(PipelineRunRow.list(from: document("train-pipeline-status-none")).isEmpty, "data is [] with no run")
        let runs = PipelineRunRow.list(from: document("train-pipeline-status-list"))
        #expect(runs.map(\.specName) == ["skills", "skills", "nightly", "skills"])
        #expect(runs.map(\.status) == ["aborted", "complete", "complete", "complete"])
        #expect(runs.map(\.passed) == [1, 2, 1, 2] && runs.map(\.stages) == [2, 2, 1, 2])
        #expect(runs[0].completedAt == nil && runs[1].completedAt != nil, "an aborted run has no completed_at")
        #expect(runs.map(\.isResumable) == [true, false, false, false])
        let held = PipelineRunRow.list(from: document("train-pipeline-status-running-list"))
        #expect(held.first?.status == "running" && held.first?.isResumable == false, "a running one is never offered a resume")
    }

    @Test("a manifest: its stages counted from 1 as the events count them, each its word, its run, and its gate's score once gated")
    func manifest() throws {
        let aborted = try #require(PipelineRun(document: document("train-pipeline-status-aborted")))
        #expect(aborted.specName == "skills" && aborted.student == "tiny" && aborted.status == "aborted")
        #expect(aborted.baseModel == "/Users/someone/.apogee/models/tiny/safetensors/aaaaaaaaaaaa")
        #expect(aborted.stages.map(\.index) == [1, 2], "the manifest's own index counts from 0")
        #expect(aborted.stages.map(\.name) == ["greetings", "refusals"])
        #expect(aborted.stages.map(\.status) == ["passed", "failed"])
        #expect(aborted.stages.map(\.score) == [1.0, 0.5])
        #expect(aborted.stages.map(\.runID) == ["\(aborted.id)-s0", "\(aborted.id)-s1"])
        #expect(aborted.isResumable && aborted.resumesAt?.index == 2)

        let running = try #require(PipelineRun(document: document("train-pipeline-status-running")))
        #expect(running.status == "running" && !running.isResumable)
        #expect(running.stages.map(\.status) == ["passed", "training"])
        #expect(running.stages.map(\.score) == [1.0, nil], "a training stage's recorded 0 is no score")

        let stopped = try #require(PipelineRun(document: document("train-pipeline-status-stopped")))
        #expect(stopped.status == "aborted" && stopped.isResumable, "a Ctrl-C is recorded as aborted")
        #expect(stopped.stages.map(\.status) == ["passed", "training"], "its stage stays where the manifest left it")
        #expect(stopped.resumesAt?.name == "refusals")

        let complete = try #require(PipelineRun(document: document("train-pipeline-status-complete")))
        #expect(complete.id == aborted.id && complete.status == "complete" && complete.completedAt != nil)
        #expect(!complete.isResumable && complete.stages.map(\.score) == [1.0, 1.0])
    }

    @Test("a document that is not a pipeline's is no manifest; fields this client does not know are passed over")
    func manifestShapes() {
        #expect(PipelineRun(document: ReadDocument(text: #"{"kind":"run","run":{"id":"x"}}"#)!) == nil)
        #expect(PipelineRun(document: ReadDocument(text: #"{"data":[],"object":"list"}"#)!) == nil)
        let later = ReadDocument(
            text: #"{"kind":"pipeline","later":1,"pipeline":{"pipeline_run_id":"p","status":"failed","x":[1],"stages":[{"index":0,"name":"a","status":"failed","cumulative_score":0.0,"new":true}]}}"#)!
        let run = PipelineRun(document: later)
        #expect(run?.id == "p" && run?.stages.first?.score == nil, "failed with no eval_results: never gated, no score")
        #expect(run?.isResumable == true)
    }

    @Test("only fusing, passed and failed are past the gate")
    func gated() {
        #expect(["pending", "training", "evaluating"].allSatisfy { !PipelineRun.isGated($0) })
        #expect(["fusing", "passed", "failed"].allSatisfy(PipelineRun.isGated))
    }

    @Test("the cycle's history: the anchor, the breaker, a halt and its reason, every pass, whether a pass holds it now")
    func cycleHistory() throws {
        let passed = try #require(CycleHistory(document: document("train-cycle-status-passed")))
        #expect(passed.backend == "nightly-model" && passed.anchorVersion == 1 && passed.anchorScore == 1.0)
        #expect(!passed.halted && !passed.active && passed.consecutiveFails == 0 && passed.totalRuns == 2)
        #expect(passed.runs.map(\.gate) == ["skipped", "pass"])
        #expect(passed.runs.map(\.id) == [1, 2])
        #expect(passed.runs[0].gateReason == "no new training data in any source" && passed.runs[0].source.isEmpty)
        #expect(passed.runs[1].promotedVersion == 1 && passed.runs[1].datasetRows == 2 && passed.runs[1].source == "directory")
        #expect(passed.runs[1].pipelineID.hasPrefix("cycle-") && passed.runs[1].note == nil)

        let halted = try #require(CycleHistory(document: document("train-cycle-status-halted")))
        #expect(halted.halted && halted.consecutiveFails == 1)
        #expect(halted.haltReason == "circuit breaker: 1 consecutive failed cycle(s) reached circuit_breaker_k 1")
        #expect(halted.runs.last?.gate == "fail" && halted.runs.last?.note == halted.haltReason)
        #expect(halted.runs.last?.gateReason.hasPrefix("pipeline aborted") == true)

        let byHand = try #require(CycleHistory(document: document("train-cycle-status-halted-by-hand")))
        #expect(byHand.halted && byHand.haltReason == "halted by 'apogee train cycle halt'" && byHand.consecutiveFails == 0)

        let active = try #require(CycleHistory(document: document("train-cycle-status-active")))
        #expect(active.active && !active.halted)

        let skipped = try #require(CycleHistory(document: document("train-cycle-status-skipped")))
        #expect(skipped.anchorVersion == 0 && skipped.runs.map(\.gate) == ["skipped"] && skipped.runs[0].promotedVersion == 0,
            "the first pass found nothing: skipped, no anchor yet")

        let noPasses = try #require(CycleHistory(document: document("train-cycle-status-halted-no-history")))
        #expect(noPasses.halted && noPasses.runs.isEmpty && noPasses.backend.isEmpty, "a halt before any pass writes a history")
        #expect(CycleHistory(document: ReadDocument(text: #"{"data":[],"object":"list"}"#)!) == nil)
    }

    @Test("train status: the cycle null until its history exists, then its headline; the pipeline in progress and the newest")
    func status() throws {
        let none = TrainingStatus(document: document("train-status-none"))
        #expect(none.cycle == nil && none.latestPipeline == nil && none.activePipeline == nil && none.pipelines == 0)
        #expect(fixture("train-cycle-status-none.err") == "apogee train: no cycle history yet ('apogee train cycle run' writes it)\n",
            "with no history the cycle's own read refuses (exit 1) -- what reading train status first avoids")

        let cycle = TrainingStatus(document: document("train-status-cycle"))
        let headline = try #require(cycle.cycle)
        #expect(headline.backend == "nightly-model" && headline.anchorVersion == 1 && headline.totalRuns == 2 && !headline.halted)
        #expect(!cycle.cycleActive && cycle.activePipeline == nil && cycle.latestPipeline?.specName == "nightly")

        let halted = TrainingStatus(document: document("train-status-cycle-halted"))
        #expect(halted.cycle?.halted == true && halted.cycle?.consecutiveFails == 1)

        let active = TrainingStatus(document: document("train-status-cycle-active"))
        #expect(active.cycleActive && active.activePipeline == active.latestPipeline?.id && active.latestPipeline?.status == "running")

        let running = TrainingStatus(document: document("train-status-pipeline-running"))
        #expect(running.activePipeline != nil && running.latestPipeline?.status == "running" && running.pipelines == 3)
    }
}
