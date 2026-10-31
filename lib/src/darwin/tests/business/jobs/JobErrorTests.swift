import BusinessJobs
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

/// GUI M13: a job's fold keeps the stream's own `error` apart from stderr's
/// last line -- since the CLI's M29 a media runtime's refusal ends its stream
/// on one -- over the branch binary's recordings and 4f's from a binary
/// before M29, whose stream says only its `job` line.
@Suite("JobError (M13)")
struct JobErrorTests {
    /// A recorded media refusal folded as a job folds it: its stream, then
    /// its end -- exit 1, its stderr the tail.
    static func folded(_ name: String) -> JobProgress {
        func recorded(_ file: String) -> String {
            let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
                .appendingPathComponent("../media/Fixtures/\(file)")
            return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
        }
        var progress = JobProgress()
        for line in recorded("\(name).jsonl").split(whereSeparator: \.isNewline) {
            progress.apply(.event(JobEvent.decode(line: String(line))))
        }
        var tail = BoundedTail()
        tail.append(Data(recorded("\(name).err").utf8))
        progress.apply(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail, interrupted: false)))
        return progress
    }

    @Test("a stream ending on its error: the error kept, the failure in its words, stderr beside it")
    func errorKept() {
        let progress = Self.folded("generate-refused-runtime-m29")
        let words = "torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video"
        #expect(progress.error == words)
        #expect(progress.status == .failed(words))
        #expect(progress.stderr == "apogee video generate: \(words)\n")
    }

    @Test("a stream that says no error: none kept, the failure stderr's last line, as before")
    func olderNone() {
        let progress = Self.folded("generate-refused-runtime")
        #expect(progress.error == nil)
        #expect(progress.status == .failed("apogee video generate: torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video"))
    }
}
