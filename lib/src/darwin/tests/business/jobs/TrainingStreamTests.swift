import BusinessJobs
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

/// The training runs' streams (the CLI's 55h), recorded from the branch's
/// binary over a sandbox home with the mock trainer, the home written
/// `/Users/someone` (`Fixtures/train-pipeline-*.jsonl`,
/// `train-cycle-run-*.jsonl`): `skills` aborted at its second stage by the
/// cumulative gate (exit 2), then resumed to complete; `nightly` in one go;
/// a run and a pass stopped by Ctrl-C (exit 130); the cycle's passes --
/// skipped, passed and promoted, failed and halted by its breaker -- and its
/// refusals: halted, locked by another pass, not configured (exit 1).
private func stream(_ name: String) -> [String] {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name).jsonl")
    return try! String(contentsOf: url, encoding: .utf8).split(separator: "\n").map(String.init)
}

private func folded(_ name: String, exit status: Int32? = nil, interrupted: Bool = false) -> JobProgress {
    var progress = JobProgress()
    for line in stream(name) { progress.apply(.event(JobEvent.decode(line: line))) }
    if let status {
        progress.apply(.ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: BoundedTail(), interrupted: interrupted)))
    }
    return progress
}

@Suite("Training streams")
struct TrainingStreamTests {
    @Test("55h's events are read typed: a pipeline's stage with its run and score, an iteration, a pass; a malformed one is kept, not guessed")
    func events() {
        #expect(JobEvent.decode(line: #"{"count":2,"cumulative_score":1.0,"index":1,"label":"greetings","pipeline_run_id":"pipe-1","run_id":"pipe-1-s0","state":"fusing","type":"stage"}"#)
            == .stage(index: 1, count: 2, label: "greetings", state: "fusing", pipelineRunID: "pipe-1", runID: "pipe-1-s0", cumulativeScore: 1.0))
        #expect(JobEvent.decode(line: #"{"count":3,"index":1,"label":"pull org/model's full weights","state":"started","type":"stage"}"#)
            == .stage(index: 1, count: 3, label: "pull org/model's full weights", state: "started"), "a pull's stage is as before")
        #expect(JobEvent.decode(line: #"{"iteration":2,"iterations":4,"loss":0.62,"lr":0.0001,"stage":1,"throughput":10.0,"type":"iteration"}"#)
            == .iteration(iteration: 2, iterations: 4, loss: 0.62, lr: 0.0001, throughput: 10.0, stage: 1))
        #expect(JobEvent.decode(line: #"{"iteration":7,"loss":1.5,"type":"iteration"}"#)
            == .iteration(iteration: 7, iterations: nil, loss: 1.5, lr: nil, throughput: nil, stage: nil), "the total only when said")
        #expect(JobEvent.decode(line: #"{"state":"gating","text":"[cycle] gate -- current 96%","type":"pass"}"#)
            == .pass(state: "gating", text: "[cycle] gate -- current 96%"))
        #expect(JobEvent.decode(line: #"{"type":"iteration","loss":1.5}"#) == .other(type: "iteration", fields: ["type": .string("iteration"), "loss": .number(1.5)]))
        #expect(JobEvent.decode(line: #"{"type":"pass","text":"x"}"#) == .other(type: "pass", fields: ["type": .string("pass"), "text": .string("x")]))
    }

    @Test("every recorded training stream is read typed, line by line: nothing left unknown")
    func everyLineTyped() {
        for name in [
            "train-pipeline-run", "train-pipeline-resume", "train-pipeline-run-nightly", "train-pipeline-run-stopped",
            "train-pipeline-resume-complete", "train-pipeline-run-unknown", "train-cycle-run-passed", "train-cycle-run-failed",
            "train-cycle-run-skipped", "train-cycle-run-refused", "train-cycle-run-locked", "train-cycle-run-stopped",
            "train-cycle-run-unconfigured",
        ] {
            let lines = stream(name)
            #expect(!lines.isEmpty, "\(name) is recorded")
            for line in lines {
                if case .other(let type, _) = JobEvent.decode(line: line) { Issue.record("\(name): \(type) left unknown") }
            }
        }
    }

    @Test("a pipeline aborted at its second stage folds to its stages as the manifest moved them, its iteration, its document and the gate's words")
    func pipelineAborted() throws {
        let progress = folded("train-pipeline-run", exit: 2)
        let id = try #require(progress.pipelineRunID)
        #expect(id.hasPrefix("pipe-"))
        #expect(progress.stageCount == 2)
        #expect(progress.orderedStages.map { $0.stage.label } == ["greetings", "refusals"])
        #expect(progress.orderedStages.map { $0.stage.state } == ["passed", "failed"])
        #expect(progress.orderedStages.map { $0.stage.runID } == ["\(id)-s0", "\(id)-s1"])
        #expect(progress.orderedStages.map { $0.stage.cumulativeScore } == [1.0, 0.5])
        #expect(progress.iteration?.stage == 2 && progress.iteration?.iteration == 4 && progress.iteration?.iterations == 4)
        #expect(progress.documents.count == 1 && progress.documents.first?["kind"]?.stringValue == "pipeline")
        #expect(progress.documents.first?["pipeline"]?["status"]?.stringValue == "aborted")
        guard case .failed(let words) = progress.status else {
            Issue.record("not failed: \(progress.status)")
            return
        }
        #expect(words.hasPrefix("pipeline aborted -- stage 'refusals' failed the cumulative eval gate (50% (1/2))"))
        #expect(words.contains("'apogee train pipeline resume \(id)'"))
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/train-pipeline-run.err")
        #expect(try String(contentsOf: url, encoding: .utf8) == "apogee train: \(words)\n", "the stream's words are stderr's")
        #expect(progress.passes.isEmpty, "a pipeline narrates no pass")
    }

    @Test("a pipeline's stages move in the manifest's order, and the latest iteration is the stage's own")
    func pipelineMidway() {
        var progress = JobProgress()
        var states: [String] = []
        for line in stream("train-pipeline-run") {
            let event = JobEvent.decode(line: line)
            progress.apply(.event(event))
            if case .stage(let index, _, _, let state, _, _, _) = event { states.append("\(index):\(state)") }
            if case .stage(2, _, _, "training", _, _, _) = event {
                #expect(progress.stages[1]?.state == "passed" && progress.stages[2]?.state == "training")
                #expect(progress.iteration?.stage == 1, "the last iteration said is stage 1's until stage 2 trains")
            }
        }
        #expect(states == ["1:pending", "2:pending", "1:training", "1:evaluating", "1:fusing", "1:passed", "2:training", "2:evaluating", "2:failed"])
    }

    @Test("a resume's first write reports every stage, so the stage that passed shows passed; it ends complete")
    func pipelineResumed() {
        let lines = stream("train-pipeline-resume")
        var first = JobProgress()
        for line in lines.prefix(4) { first.apply(.event(JobEvent.decode(line: line))) }
        #expect(first.orderedStages.map { $0.stage.state } == ["passed", "failed"], "the first write: what is already there")
        let progress = folded("train-pipeline-resume", exit: 0)
        #expect(progress.status == .done)
        #expect(progress.orderedStages.map { $0.stage.state } == ["passed", "passed"])
        #expect(progress.documents.last?["pipeline"]?["status"]?.stringValue == "complete")
    }

    @Test("stopped by its Stop, a pipeline is stopped -- its stage left where the manifest left it, its document the aborted manifest")
    func pipelineStopped() {
        let progress = folded("train-pipeline-run-stopped", exit: 130, interrupted: true)
        #expect(progress.status == .stopped)
        #expect(progress.orderedStages.map { $0.stage.state } == ["passed", "training"], "never turned into a word the manifest has not")
        #expect(progress.documents.last?["pipeline"]?["status"]?.stringValue == "aborted")
    }

    @Test("a refused run says the CLI's words and has no document")
    func pipelineRefused() {
        let complete = folded("train-pipeline-resume-complete", exit: 1)
        #expect(complete.documents.isEmpty && complete.stages.isEmpty)
        guard case .failed(let words) = complete.status else {
            Issue.record("not failed")
            return
        }
        #expect(words.hasPrefix("pipeline pipe-") && words.hasSuffix(" is already complete"))
        let unknown = folded("train-pipeline-run-unknown", exit: 1)
        #expect(unknown.status == .failed("no pipeline 'nosuch': not a spec file, and not an entry under training.pipelines"))
    }

    @Test("a cycle pass folds to its narration, in order, its pipeline's stages and its history last")
    func cyclePassed() {
        let progress = folded("train-cycle-run-passed", exit: 0)
        #expect(progress.status == .done)
        #expect(progress.passes.map(\.state) == ["collecting", "merging", "training", "gating", "promoting", "passed", "passed"])
        #expect(progress.pass?.text.hasPrefix("[cycle] complete -- nightly-model promoted to v1") == true)
        #expect(progress.pipelineRunID?.hasPrefix("cycle-") == true)
        #expect(progress.orderedStages.map { $0.stage.state } == ["passed"])
        #expect(progress.documents.last?["anchor_version"]?.intValue == 1)
        #expect(progress.lastSaid == progress.pass?.text, "the last thing said was the pass's")
    }

    @Test("a failing pass trips the breaker: failed, then halted, in the CLI's words")
    func cycleFailed() {
        let progress = folded("train-cycle-run-failed", exit: 2)
        #expect(progress.passes.suffix(2).map(\.state) == ["failed", "halted"])
        #expect(progress.passes.last?.text.contains("the loop is halted") == true)
        #expect(progress.documents.last?["halted"]?.boolValue == true)
        guard case .failed(let words) = progress.status else {
            Issue.record("not failed")
            return
        }
        #expect(words.hasPrefix("cycle FAILED -- pipeline aborted"))
    }

    @Test("a skipped pass is a pass that ended well; the refusals end in their words with no document")
    func cycleSkippedAndRefused() {
        let skipped = folded("train-cycle-run-skipped", exit: 0)
        #expect(skipped.status == .done && skipped.passes.map(\.state) == ["collecting", "skipped"])
        #expect(skipped.pass?.text == "[cycle] no new data -- skipped")
        let halted = folded("train-cycle-run-refused", exit: 1)
        #expect(halted.documents.isEmpty && halted.passes.isEmpty)
        #expect(halted.status == .failed(
            "the cycle is halted: circuit breaker: 1 consecutive failed cycle(s) reached circuit_breaker_k 1. Review the data and the suite, then 'apogee train cycle resume'"))
        let locked = folded("train-cycle-run-locked", exit: 1)
        guard case .failed(let words) = locked.status else {
            Issue.record("not failed")
            return
        }
        #expect(words.hasPrefix("another cycle is already running (lock: /Users/someone/.apogee/training/cycle/cycle.lock)"))
        let unconfigured = folded("train-cycle-run-unconfigured", exit: 1)
        guard case .failed(let missing) = unconfigured.status else {
            Issue.record("not failed")
            return
        }
        #expect(missing.hasPrefix("training.cycle is not configured"))
    }

    @Test("a pass stopped by its Stop is stopped, its stage where it was")
    func cycleStopped() {
        let progress = folded("train-cycle-run-stopped", exit: 130, interrupted: true)
        #expect(progress.status == .stopped)
        #expect(progress.passes.last?.state == "training" && progress.orderedStages.map { $0.stage.state } == ["training"])
    }

    @Test("what was said last: a line, a notice or a pass, whichever came last")
    func lastSaid() {
        var progress = JobProgress()
        progress.apply(.said("prose"))
        #expect(progress.lastSaid == "prose")
        progress.apply(.event(.pass(state: "collecting", text: "[cycle] directory: 1 file(s)")))
        #expect(progress.lastSaid == "[cycle] directory: 1 file(s)")
        progress.apply(.event(.notice("[train] stage 1/1")))
        #expect(progress.lastSaid == "[train] stage 1/1" && progress.notices.last == "[train] stage 1/1")
        progress.apply(.said("  "))
        #expect(progress.lastSaid == "[train] stage 1/1", "a blank line says nothing")
    }
}
