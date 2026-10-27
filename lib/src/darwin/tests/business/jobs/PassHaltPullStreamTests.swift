import BusinessJobs
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

/// The CLI's M30 streams, recorded from the branch binary (schema 2026-10-26)
/// over a sandbox home on the mock trainer, the home written `/Users/someone`
/// (`Fixtures/train-cycle-run-halted.jsonl` and the rest): a cycle pass held
/// on its lock by a FIFO nobody read while `train cycle halt` was taken, then
/// drained -- it finished, promoted, and said the halt was kept -- and the
/// next pass refused; `datasets pull` refused for a bad ref, and stopped by a
/// Ctrl-C while its listing waited on a loopback proxy that never answers, so
/// nothing reached the network. `datasets-pull-handmade.jsonl` is hand-made
/// from `datasets.cpp`'s lines and M30's test: a pull that lands needs the
/// network.
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

@Suite("A pass's kept halt and the pull's stream (the CLI's M30)")
struct PassHaltPullStreamTests {
    static let haltKept =
        "[cycle] halted while this pass ran (halted by 'apogee train cycle halt') -- the halt is kept: 'apogee train cycle run' refuses until 'apogee train cycle resume'"

    @Test("every M30 stream is read typed, line by line: nothing left unknown")
    func everyLineTyped() {
        for name in [
            "train-cycle-run-halted", "train-cycle-run-halt-kept-refused", "datasets-pull-bad-ref", "datasets-pull-stopped",
            "datasets-pull-handmade",
        ] {
            let lines = stream(name)
            #expect(!lines.isEmpty, "\(name) is recorded")
            for line in lines {
                if case .other(let type, _) = JobEvent.decode(line: line) { Issue.record("\(name): \(type) left unknown") }
            }
        }
    }

    @Test("a pass halted while it ran finishes as it would have -- passed -- and says the halt is kept, a notice before its document; never a pass of state halted")
    func passHalted() {
        let progress = folded("train-cycle-run-halted", exit: 0)
        #expect(progress.status == .done)
        #expect(progress.pass?.state == "passed")
        #expect(!progress.passes.contains { $0.state == "halted" }, "halted is the breaker's state")
        #expect(progress.notices.last == Self.haltKept)
        #expect(progress.documents.count == 1 && progress.documents.last?["halted"]?.boolValue == true)
        #expect(progress.documents.last?["halt_reason"]?.stringValue == "halted by 'apogee train cycle halt'")
        #expect(progress.iteration?.iteration == 200 && progress.iteration?.iterations == 200)
    }

    @Test("the next pass is refused while the halt stands, in the CLI's words, with no document")
    func nextRefused() {
        let progress = folded("train-cycle-run-halt-kept-refused", exit: 1)
        #expect(progress.status
            == .failed("the cycle is halted: halted by 'apogee train cycle halt'. Review the data and the suite, then 'apogee train cycle resume'"))
        #expect(progress.documents.isEmpty && progress.passes.isEmpty)
    }

    @Test("a pull that lands folds to its bytes, file by file, its lines, and its document naming the folder")
    func pullLands() {
        let progress = folded("datasets-pull-handmade", exit: 0)
        #expect(progress.status == .done)
        #expect(progress.bytes?.done == 40000 && progress.bytes?.total == 40000 && progress.bytes?.fraction == 1)
        #expect(progress.bytes?.file == "data/test.jsonl" && progress.bytes?.index == 2 && progress.bytes?.count == 2)
        #expect(progress.notices.first == "downloading 2 file(s), 136 KiB, into /Users/someone/.apogee/training/datasets/raw/someone--heat-notes")
        #expect(progress.notices.last == "Convert it with:  apogee datasets prepare /Users/someone/.apogee/training/datasets/raw/someone--heat-notes")
        let document = progress.documents.last
        #expect(document?["object"]?.stringValue == "dataset.pull" && document?["ref"]?.stringValue == "someone/heat-notes")
        #expect(document?["path"]?.stringValue == "/Users/someone/.apogee/training/datasets/raw/someone--heat-notes")
        #expect(document?["files"]?.intValue == 2 && document?["bytes"]?.intValue == 140000)
    }

    @Test("a pull its Stop reached ends stopped, the CLI's cancel kept as its error, with no document")
    func pullStopped() {
        let progress = folded("datasets-pull-stopped", exit: 130, interrupted: true)
        #expect(progress.status == .stopped)
        #expect(progress.error == "cancelled -- nothing was written")
        #expect(progress.documents.isEmpty && progress.bytes == nil)
    }

    @Test("a pull refused before it starts is its job and its error, in the CLI's words")
    func pullRefused() {
        let progress = folded("datasets-pull-bad-ref", exit: 1)
        #expect(progress.status == .failed("'nonsense' is not a Hugging Face dataset ref (expected owner/name)"))
        #expect(progress.documents.isEmpty && progress.notices.isEmpty)
    }
}
