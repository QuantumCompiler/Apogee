import BusinessJobs
import BusinessReads
import Foundation
import Testing

@testable import PresentationKnowledge

/// A person's text is never an option (GUI 4g): on the branch binary
/// `knowledge capture "-x text"` was "not expected", and `knowledge query
/// "-x q"` and `embed query c "-x q"` each said their text "is required";
/// after `--` each is read as the text.
@Suite("Knowledge text after --", .timeLimit(.minutes(1)))
@MainActor
struct KnowledgeTextTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    @Test("a capture's text after --, its overrides before it")
    func capture() {
        var form = RecordsViewModel.CaptureForm()
        form.text = "- Ada: drop it? Bob: yes"
        form.status = "rejected"
        #expect(form.arguments == ["knowledge", "capture", "--status", "rejected", "--", "- Ada: drop it? Bob: yes"])
    }

    @Test("a question to the records and to a collection after --, the branch and the collection before it")
    func questions() async {
        let runner = ScriptedRunner()
        runner.reading(["knowledge", "list"], fixture("knowledge-list"))
        runner.answering(["knowledge", "query"], .done(stdout: fixture("knowledge-query")))
        let records = RecordsViewModel(runner: runner, starter: ScriptedJobs([]))
        await records.connect(binary: Self.binary)
        records.question = "--why was cancel removed"
        await records.search()
        #expect(runner.calls.last == "run knowledge|query|--status||--json|--|--why was cancel removed")
        runner.reading(["embed", "list"], fixture("collections-list"))
        runner.reading(["embed", "info"], fixture("collection-info"))
        runner.answering(["embed", "query"], .done(stdout: fixture("collection-query", "txt")))
        let collections = CollectionsViewModel(runner: runner, starter: ScriptedJobs([]))
        await collections.connect(binary: Self.binary)
        await collections.select(collections.collections[1])
        collections.question = "-how does a heat pump work"
        await collections.search()
        #expect(runner.calls.last == "run embed|query|notes|--|-how does a heat pump work")
    }
}
