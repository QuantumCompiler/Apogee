import BusinessReads
import DataProtocol
import Foundation
import Testing

/// Recorded from the branch binary (schema 2026-10-26, the CLI's M30) over a
/// sandbox home -- the mock trainer, the two named pipelines 4j recorded
/// (`nightly`, one stage; `skills`, two, the second gated on both suites) and
/// the cycle over a queue in the sandbox -- the home written `/Users/someone`:
/// `train pipeline list | show` in text and JSON, and the cycle's history
/// while a pass held its lock (its stream into a FIFO nobody read), before and
/// after `train cycle halt`, then once the pass had recorded itself.
private func fixture(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name)")
    return try! String(contentsOf: url, encoding: .utf8)
}

private func document(_ name: String) -> ReadDocument {
    ReadDocument(text: fixture("\(name).json"))!
}

@Suite("Named pipelines and the kept halt (the CLI's M30)")
struct PipelineSpecReadsTests {
    @Test("train pipeline list: each named spec, its student and its stages in order -- dataset, own suite, the gate's suites, the tuning as written")
    func listed() throws {
        let specs = PipelineSpec.list(from: document("train-pipeline-list"))
        #expect(specs.map(\.name) == ["nightly", "skills"])
        #expect(specs.map(\.student) == ["tiny", "tiny"])
        #expect(specs.allSatisfy { $0.path == nil }, "training.pipelines entries: no file")
        let skills = specs[1]
        #expect(skills.stages.map(\.index) == [1, 2], "counted from 1, as a run's stage events count")
        #expect(skills.stages.map(\.name) == ["greetings", "refusals"])
        #expect(skills.stages.map(\.dataset) == ["starter", "regress"])
        #expect(skills.stages.map(\.evalSuite) == ["hello", "nope"])
        #expect(skills.stages.map(\.gate) == [["hello"], ["hello", "nope"]], "every earlier stage's suite, then its own")
        #expect(skills.stages.map(\.iters) == [4, 4])
        #expect(skills.stages.allSatisfy {
            $0.method.isEmpty && $0.batchSize == 0 && $0.numLayers == 0 && !$0.gradCheckpoint && !$0.maskPrompt && $0.rehearsalFraction == 0
        })
        #expect(PipelineSpec.list(from: document("train-pipeline-list-none")).isEmpty, "data is [] with none named")
    }

    @Test("the list states what its text view says: the same names, stage counts and students")
    func agreesWithItsText() {
        let rows = fixture("train-pipeline-list.txt").split(separator: "\n").dropFirst()
            .map { $0.split(separator: " ").map(String.init) }
        let specs = PipelineSpec.list(from: document("train-pipeline-list"))
        #expect(rows.map { $0[0] } == specs.map(\.name))
        #expect(rows.map { Int($0[1]) } == specs.map { $0.stages.count })
        #expect(rows.map { $0[2] } == specs.map(\.student))
        #expect(fixture("train-pipeline-list-none.txt").hasPrefix("no named pipelines"))
    }

    @Test("train pipeline show: one spec, as the list states it; any other document is none")
    func shown() throws {
        let skills = try #require(PipelineSpec(document: document("train-pipeline-show-skills")))
        #expect(skills == PipelineSpec.list(from: document("train-pipeline-list"))[1])
        #expect(fixture("train-pipeline-show-skills.txt").contains("2  refusals   regress  hello + nope"), "its text's gate column")
        let nightly = try #require(PipelineSpec(document: document("train-pipeline-show-nightly")))
        #expect(nightly.stages.map(\.name) == ["base"] && nightly.stages.map(\.gate) == [["hello"]])
        #expect(PipelineSpec(document: document("train-pipeline-list")) == nil, "a list is no spec")
        #expect(PipelineSpec(document: document("train-pipeline-status-aborted")) == nil, "nor a run's manifest")
    }

    @Test("fields this client does not know are passed over; an entry with no name is none")
    func tolerant() throws {
        let read = try #require(ReadDocument(text: #"{"object":"list","data":[{"name":"a","later":1,"stages":[{"name":"s","later":true}],"student":"x"},{"stages":[]}]}"#))
        let specs = PipelineSpec.list(from: read)
        #expect(specs.map(\.name) == ["a"] && specs.first?.stages.map(\.name) == ["s"])
        #expect(specs.first?.stages.first?.gate == [] && specs.first?.stages.first?.iters == 0)
    }

    @Test("a halt taken while a pass holds the cycle: halted and active together, then -- the pass recorded -- still halted")
    func haltKept() throws {
        let before = try #require(CycleHistory(document: document("train-cycle-status-held")))
        #expect(before.active && !before.halted)
        let during = try #require(CycleHistory(document: document("train-cycle-status-held-halted")))
        #expect(during.active && during.halted && during.haltReason == "halted by 'apogee train cycle halt'")
        let after = try #require(CycleHistory(document: document("train-cycle-status-halt-kept")))
        #expect(!after.active && after.halted && after.haltReason == during.haltReason)
        #expect(after.runs.count == during.runs.count + 1)
        #expect(after.runs.last?.gate == "pass" && after.runs.last?.promotedVersion == 2, "the pass finished as it would have")
        let held = TrainingStatus(document: document("train-status-cycle-held-halted"))
        #expect(held.cycleActive && held.cycle?.halted == true)
        let kept = TrainingStatus(document: document("train-status-cycle-halt-kept"))
        #expect(!kept.cycleActive && kept.cycle?.halted == true && kept.activePipeline == nil)
        let resumed = try #require(CycleHistory(document: document("train-cycle-status-resumed")))
        #expect(!resumed.halted && resumed.haltReason.isEmpty && resumed.runs.count == after.runs.count)
        #expect(fixture("train-cycle-halt-during-pass.txt")
            == "cycle halted -- 'apogee train cycle run' refuses until 'apogee train cycle resume'\n  the pass running now finishes, and keeps the halt\n")
    }
}
