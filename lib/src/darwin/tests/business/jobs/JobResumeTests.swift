import BusinessJobs
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

/// A chained pull's stream, as the CLI's own test pins it
/// (`lib/src/cli/tests/presentation/cli/model_chain_test.cpp`, "as a stream":
/// the stage events and the notices of a chain that finished), the `job` line
/// as the branch's binary writes it. Hand-made: a real chain pulls and
/// converts real weights.
func chainStream() -> [JobUpdate] {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("Fixtures/pull-chain-done-handmade.jsonl")
    let text = (try? String(contentsOf: url, encoding: .utf8)) ?? ""
    return text.split(whereSeparator: \.isNewline).map { .event(JobEvent.decode(line: String($0))) }
}

private func ended(_ status: Int32, interrupted: Bool = false) -> JobUpdate {
    .ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: BoundedTail(), interrupted: interrupted))
}

/// A resume command is for a run that did not finish (GUI 4e): the CLI says
/// "from here on, if this stops, resume with: …" mid-run, on success too
/// (`model_chain.cpp`), and a finished chain has nothing to resume.
@Suite("JobResume")
struct JobResumeTests {
    static let resume = ["models", "convert", "org--model/safetensors/a", "--register-with", "Q4_K_M"]

    @Test("a chain that finished offers no resume, though it named one on the way; stopped or failed, it carries on by it")
    func resumeOnlyOnStops() {
        let stream = chainStream()
        #expect(stream.count == 16, "the fixture is the CLI's whole pinned stream")
        var done = JobProgress()
        stream.forEach { done.apply($0) }
        #expect(done.notices.contains("  apogee models convert org--model/safetensors/a --register-with Q4_K_M"), "the CLI said it")
        #expect(done.resumeCommand == nil, "running: nothing has stopped")
        done.apply(ended(0))
        #expect(done.status == .done)
        #expect(done.orderedStages.allSatisfy { $0.stage.state == "done" })
        #expect(done.resumeCommand == nil, "a finished chain has nothing to resume")

        // The same chain stopped once its pull is done: the command it named.
        var stopped = JobProgress()
        stream.prefix(6).forEach { stopped.apply($0) }
        stopped.apply(ended(130, interrupted: true))
        #expect(stopped.status == .stopped)
        #expect(stopped.resumeCommand == Self.resume)

        // Failed at its conversion, in the CLI's words (the same test's failing section).
        var failed = JobProgress()
        stream.prefix(6).forEach { failed.apply($0) }
        failed.apply(.event(.stage(index: 2, count: 4, label: "convert to F16", state: "failed")))
        failed.apply(.event(.notice("stopped at [2/4] convert to F16 -- what the stages before it made is kept.")))
        failed.apply(.event(.notice("resume with:")))
        failed.apply(.event(.notice("  apogee models convert org--model/safetensors/a --register-with Q4_K_M")))
        failed.apply(.event(.error("convert failed")))
        failed.apply(ended(1))
        #expect(failed.status == .failed("convert failed"))
        #expect(failed.resumeCommand == Self.resume)
    }

    @Test("a standalone conversion that finished names its next step, which is no resume")
    func doneConversionOffersNone() {
        // `models convert` alone ends by saying how to make it smaller
        // (`models_pull.cpp`, convert_model): a suggestion, not a stop.
        var progress = JobProgress()
        progress.apply(.said("Make it smaller:"))
        progress.apply(.said("  apogee models quantize Llama3.2-1B-Instruct --type Q4_K_M"))
        progress.apply(ended(0))
        #expect(progress.status == .done && progress.resumeCommand == nil)
    }

    @Test("a run that never started fails in the words its supervisor gives, with nothing to resume")
    func notStarted() {
        var progress = JobProgress()
        progress.apply(.ended(.notStarted("Couldn't read the store first, so nothing ran")))
        #expect(progress.status == .failed("Couldn't read the store first, so nothing ran"))
        #expect(progress.resumeCommand == nil)
    }
}
