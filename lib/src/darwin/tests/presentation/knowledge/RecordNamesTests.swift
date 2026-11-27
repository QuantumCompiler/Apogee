import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import Testing

@testable import PresentationKnowledge

/// The records' lines with every positional after `--` (GUI M23): the
/// record's id -- the CLI's, never dash-led, but the rule holds for every
/// positional -- and a typed ref or branch beside it, after one `--`.
@Suite("Record names", .timeLimit(.minutes(1)))
@MainActor
struct RecordNamesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    /// `knowledge list` holding one record, its id `id`.
    static func listing(_ id: String) -> String {
        let one = fixture("knowledge-list-one")
        let first = (try? JSONSerialization.jsonObject(with: Data(one.utf8)) as? [String: Any]).flatMap { ($0["data"] as? [[String: Any]])?.first?["id"] as? String } ?? ""
        return one.replacingOccurrences(of: first, with: id)
    }

    func records(_ id: String) async -> (RecordsViewModel, ScriptedRunner) {
        let runner = ScriptedRunner()
        runner.reading(["knowledge", "list"], Self.listing(id))
        runner.answering(["knowledge"], .done(stdout: "done\n"))
        runner.answering(["knowledge", "query"], .done(stdout: fixture("knowledge-query")))
        let model = RecordsViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        return (model, runner)
    }

    @Test("a typed -x ref is linked after the record's id, one -- before both; a move and a delete name the record after --")
    func linkMoveDelete() async throws {
        let (model, runner) = await records("kr-1")
        let record = try #require(model.records.first, "records: \(model.records.map(\.id))")
        await model.link(record, to: " -x ")
        #expect(runner.calls.contains("run knowledge|link|--|kr-1|-x"))
        await model.move(record, to: "superseded")
        #expect(runner.calls.contains("run knowledge|status|--|kr-1|superseded"))
        #expect(RecordsViewModel.deleteArguments(record) == ["knowledge", "delete", "--", "kr-1"])
    }

    @Test("every line the records build with -x, -h or --graph reaches the verb: each recorded from the built CLI, none the parser's refusal")
    func sweep() async throws {
        var lines: [[String]] = []
        for name in NameVerdicts.names {
            let (model, runner) = await records(name)
            let record = try #require(model.records.first)
            await model.link(record, to: name)
            await model.move(record, to: "superseded")
            await model.showRaw(record)
            await model.confirmDelete(record)
            model.question = name
            await model.search()
            lines += NameVerdicts.lines(ofCalls: runner.calls)
            var capture = RecordsViewModel.CaptureForm()
            capture.text = name
            capture.link = "PROJ-7"
            lines += [capture.arguments ?? []]
        }
        let (missing, refused) = NameVerdicts.judged(lines)
        #expect(missing.isEmpty, "never recorded -- add these rows to tests/presentation/shell/Fixtures/argv-name-verdicts.txt and run scripts/refresh-argv-name-verdicts.sh <the built apogee>:\n\(missing.joined(separator: "\n"))")
        #expect(refused.isEmpty, "the parser refuses:\n\(refused.joined(separator: "\n"))")
    }
}
