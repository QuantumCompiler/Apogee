import BusinessReads
import DataProtocol
import Foundation
import Testing

private func document(_ name: String) -> ReadDocument {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name).json")
    return ReadDocument(text: try! String(contentsOf: url, encoding: .utf8))!
}

/// Recorded from the installed binary over a scratch home: a run on the mock
/// trainer, gated on a kit; the seeded regimes and kits; a dataset from the
/// template. A promoted version needs a real conversion, so the ledger here
/// is the CLI's own shape (`ledger_to_json`), written out.
@Suite("Training")
struct TrainingTests {
    @Test("a run: what it trained on what, its loss, and the gate's verdict and score once gated")
    func status() throws {
        let status = TrainingStatus(document: document("train-status"))
        #expect(status.runs == 1 && status.running.isEmpty && !status.cycleActive)
        let run = try #require(status.recent.first)
        #expect(run.status == "complete" && run.trainer == "mock" && run.method == "lora" && run.iterations == 4)
        #expect(run.finalLoss == 0.42)
        #expect(run.evalPassed == false && abs((run.evalScore ?? 0) - 0.4583) < 0.001)
    }

    @Test("regimes, kits and datasets as the CLI lists them")
    func lists() {
        let regimes = TrainingRegime.list(from: document("train-regimes"))
        #expect(regimes.map(\.name) == ["foundation", "harness-helpers", "knowledge"])
        #expect(regimes[0].kits == ["instruction-following", "structured-output", "summarization", "reasoning"])
        #expect(regimes[0].source == "seeded" && regimes[0].teacher.isEmpty)
        #expect(TrainingKit.list(from: document("datasets-kits")).allSatisfy { $0.evalItems > 0 })
        let datasets = TrainingDataset.list(from: document("datasets-list"))
        #expect(datasets.map(\.name) == ["heat-notes"] && datasets[0].lines == 2 && datasets[0].shape == "chat")
    }

    @Test("a backend's promoted versions: the active one, each run, GGUF or MLX, pruned or not")
    func versions() throws {
        let doc = ReadDocument(text: #"{"object":"list","data":[{"backend_name":"helper","active_version":2,"versions":[{"version":1,"run_id":"20261019-101500","gguf_path":"/m/helper-v1.gguf","promoted_at":"2026-10-19T10:30:00Z","eval_score":0.92,"eval_passed":true,"pruned_at":"2026-10-20T09:00:00Z"},{"version":2,"run_id":"20261020-111500","mlx_path":"/m/helper-v2","promoted_at":"2026-10-20T11:30:00Z","eval_passed":true}]}]}"#)!
        let ledger = try #require(VersionLedger.list(from: doc).first)
        #expect(ledger.backend == "helper" && ledger.active == 2)
        #expect(ledger.versions.map(\.path) == ["/m/helper-v1.gguf", "/m/helper-v2"])
        #expect(ledger.versions.map(\.pruned) == [true, false])
    }
}
