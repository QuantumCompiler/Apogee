import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization
import Testing

/// What a binary printed over a sandbox home, the home written
/// `/Users/someone` (`Fixtures/`): the branch binary (schema 2026-10-26) and,
/// where a name says `older`, the installed 07e26c6d (schema 2026-10-25).
private func printed(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

private func document(_ name: String) throws -> ReadDocument {
    try #require(ReadDocument(text: printed(name)), "\(name) is one JSON document")
}

/// A refusal the binary wrote: `status` its exit, the recorded stderr its tail.
private func refusal(_ name: String, status: Int32) -> ReadOutcome {
    var tail = BoundedTail()
    tail.append(Data(printed(name).utf8))
    return .failed(exit: ChildExit(reason: .exited, status: status), stderrTail: tail)
}

/// The text footprint the same document carries in its `fields`.
private func textFootprint(_ document: ReadDocument) -> SuiteFootprint? {
    Field.list(from: document).first { $0.label == "footprint" }.flatMap { SuiteFootprint(text: $0.value) }
}

/// GUI M15: the five reads the CLI's M31 gave a document, decoded from the
/// branch binary's recordings -- each beside the text the same binary, or
/// the older one, printed of the same facts.
@Suite("The reads M31 made documents (GUI M15)")
struct ProseReadsTests {
    // MARK: - chats info

    @Test("chats info's document is chat.info typed: its facts under the text's own labels, the text's every line among them")
    func chatInfoTyped() throws {
        let details = try #require(ChatDetails(document: try document("chats-info.json")))
        #expect(details.id == "20261027-015932-30ca" && details.name == "Dev Server Port" && details.title == "Dev Server Port")
        #expect(details.backend == "actor-port" && details.model == "" && details.customName == "")
        #expect(details.turns == 2 && details.messages == 4 && details.compactions == 0)
        #expect(details.temperature == nil && details.maxTokens == nil && details.schemaVersion == 2)
        #expect(details.started == "2026-10-27T01:59:32Z" && details.updated == "2026-10-27T01:59:32Z")
        // The same chat, as the same binary prints it in text: every line
        // the text says, the typed facts say alike.
        let text = Field.lines(printed("chats-info-text.txt"))
        #expect(text.map(\.label) == ["id", "title", "backend", "started", "updated", "turns", "messages"])
        for line in text {
            #expect(details.fields.contains(line), "\(line.label) agrees")
        }
        #expect(details.fields.first { $0.label == "compacted" }?.value == "never", "a fact only the document states")
        let compacted = try #require(ChatDetails(document: try document("chats-info-compacted.json")))
        #expect(compacted.compactions == 1 && compacted.messages == 2)
        #expect(compacted.fields.first { $0.label == "compacted" }?.value == "1 time(s)", "as the text says it")
        #expect(ChatDetails(document: try document("chats-list-recall.json")) == nil, "a list is no chat's details")
    }

    // MARK: - chats recall

    @Test("chats recall's listing: each summary, newest first -- one an older binary wrote among them -- and the count line the text says")
    func recallListed() throws {
        let listing = try #require(RecallListing(document: try document("chats-recall.json")))
        #expect(listing.summaries.map(\.name) == ["Launch Emails", "Dev Server Port", "Release Checklist"])
        #expect(listing.summaries.map(\.chat) == ["20261027-015933-2264", "20261027-015932-30ca", "20261027-015931-79d5"])
        #expect(listing.summaries[1].summary == "Asked the dev server's port (8080) and confirmed the launch on 2026-11-12.")
        // Written by the installed binary, with no summary field: the CLI
        // rebuilt its text from the record, nothing migrated.
        #expect(listing.summaries[2].summary.hasPrefix("Asked what was left on the release checklist"))
        #expect(listing.summaries[2].updated == (try? Date("2026-10-27T01:59:31Z", strategy: .iso8601)))
        #expect(listing.index == "/Users/someone/.apogee/memory/chats.db" && listing.pending == 0)
        #expect(listing.countLine == RecallListing.countLine(in: printed("chats-recall-listing.txt")), "the branch's text says the same count")
        #expect(listing.countLine == RecallListing.countLine(in: printed("chats-recall-older.txt")), "and the older binary's, over the same index")
        #expect(listing.countLine == "recall holds 3 chat summaries in /Users/someone/.apogee/memory/chats.db")
        #expect(listing.holdsAny)
        #expect(listing.forgetting == "3 chat summaries: “Launch Emails”, “Dev Server Port” and “Release Checklist”")
        let empty = try #require(RecallListing(document: try document("chats-recall-empty.json")))
        #expect(empty.summaries.isEmpty && !empty.holdsAny)
        #expect(empty.countLine == RecallListing.countLine(in: printed("chats-recall-none.txt")))
        #expect(RecallListing(document: try document("chats-list-recall.json")) == nil, "chats list is no recall listing")
    }

    @Test("the count line is read from the text whatever follows it: an M31 binary lists a line per summary after it")
    func recallCountLineInText() {
        #expect(RecallListing.countLine(in: printed("chats-recall-listing.txt")) == "recall holds 3 chat summaries in /Users/someone/.apogee/memory/chats.db")
        #expect(RecallListing.countLine(in: printed("chats-recall.txt")) == "recall holds 2 chat summaries in /Users/someone/.apogee/memory/chats.db")
        #expect(RecallListing.countLine(in: "") == nil)
    }

    // MARK: - graph dedupe

    @Test("graph dedupe's body: the groups, each kept entity and what folds into it -- the same groups its text names")
    func dedupeGroups() throws {
        let plan = try #require(DedupePlan(document: try document("graph-dedupe-dry-run-groups.json")))
        #expect(plan.groups.map(\.kept) == ["K8s", "Scheduler"])
        #expect(plan.groups.map(\.keptType) == ["system", "system"])
        #expect(plan.groups.map(\.merged) == [["Cache", "Gateway", "Store"], ["Load Balancer"]])
        #expect(plan.mergedNodes == 4 && plan.threshold == 0.92 && plan.dryRun && plan.merges)
        #expect(plan.codeEntities == 0 && plan.proseSkipped == nil)
        #expect(plan.summary == "2 groups, 4 entities would merge at threshold 0.92.")
        let text = printed("graph-dedupe-dry-run-groups.txt").split(separator: "\n").map(String.init).filter { $0.hasPrefix("Would merge into ") }
        #expect(text == plan.groups.map { "Would merge into \($0.kept) (\($0.keptType)): \($0.merged.joined(separator: ", "))" })
    }

    @Test("a code graph's body names no group, its entities merged by name as built, and why the vector pass compared nothing")
    func dedupeNothing() throws {
        let code = try #require(DedupePlan(document: try document("graph-dedupe-dry-run.json")))
        #expect(code.groups.isEmpty && !code.merges && code.mergedNodes == 0)
        #expect(code.codeEntities == 9 && code.codeIdentityMerges == 0)
        #expect(code.proseSkipped == "no prose entities -- nothing to compare by vector")
        #expect(code.summary == "Nothing would merge at threshold 0.92.")
        let after = try #require(DedupePlan(document: try document("graph-dedupe-after-merge.json")))
        #expect(!after.merges, "after the merge, a dry run finds nothing")
        #expect(DedupePlan(document: try document("chats-recall.json")) == nil)
    }

    // MARK: - knowledge info --raw

    @Test("knowledge info --raw's document carries raw: the conversation the text prints after its marker, null when unreadable, absent before M31")
    func rawFromTheDocument() throws {
        let raw = try #require(RawConversation(document: try document("knowledge-info-raw.json")))
        #expect(raw == RawConversation(printed: printed("knowledge-info-raw-text.txt")), "the same conversation the text shows")
        #expect(RawConversation(document: try document("knowledge-info-raw-null.json")) == .unavailable)
        #expect(RawConversation(document: try document("knowledge-info-raw-older.json")) == nil, "the older binary drops it: read from the text instead")
    }

    // MARK: - models status --suite: the typed footprint

    @Test("the typed footprint: each member's exact bytes, the base it shares, the margin, the budget -- the bytes summing to the total")
    func typedFootprint() throws {
        let doc = try document("suites-models-status-shared-typed.json")
        let typed = try #require(doc.value["footprint"].flatMap(SuiteFootprint.init(typed:)))
        #expect(typed.isTyped && typed.suite == "shared" && typed.verdict == .fits)
        #expect(typed.members.map(\.backend) == ["root", "scribe", "helper"])
        #expect(typed.members.map(\.roles) == [["chat"], ["extraction"], ["utility"]])
        #expect(typed.members.map(\.bytes) == [43_092_279_296, 142_606_336, 142_606_688])
        #expect(typed.members.map(\.shares) == ["", "root", "root"])
        #expect(typed.members.allSatisfy { $0.known == true && $0.local == true })
        #expect(typed.marginBytes == 1_073_741_824 && typed.budgetBytes == 115_448_725_504)
        #expect(typed.totalBytes == typed.members.compactMap(\.bytes).reduce(0, +) + (typed.marginBytes ?? 0), "M31: exact, summing")
        #expect(typed.verdictWords == "fits")
        #expect(typed.members[1].said.contains("on root's weights"))
        let partial = try #require(try document("suites-models-status-partial-typed.json").value["footprint"].flatMap(SuiteFootprint.init(typed:)))
        #expect(partial.verdict == .fitsAsFarAsKnown && partial.members[1].known == false)
        #expect(partial.members[1].said == "unknown -- no recorded size")
        #expect(partial.total.contains("needs at least"))
        let cloud = try #require(try document("suites-models-status-cloud-typed.json").value["footprint"].flatMap(SuiteFootprint.init(typed:)))
        #expect(cloud.members.first?.local == false && cloud.members.first?.said == "nothing held here")
    }

    @Test("the guardrail: for every recorded suite the typed footprint and the text footprint give the same verdict -- the text in the same document, and the older binary's")
    func typedAndTextAgree() throws {
        let older = ["shared", "vast", "cloud", "fast", "pair"]
        for suite in older + ["partial"] {
            let doc = try document("suites-models-status-\(suite)-typed.json")
            let typed = try #require(doc.value["footprint"].flatMap(SuiteFootprint.init(typed:)), "\(suite) is typed")
            let text = try #require(textFootprint(doc), "\(suite)'s text footprint")
            #expect(typed.verdict == text.verdict, "\(suite): typed \(typed.verdict), text \(text.verdict)")
            #expect(typed.members.map(\.backend) == text.members.map(\.backend) && typed.members.map(\.roles) == text.members.map(\.roles))
            #expect(typed.suite == text.suite && !text.isTyped)
            if older.contains(suite) {
                let before = try #require(textFootprint(try document("suites-models-status-\(suite).json")), "\(suite) recorded before M31")
                #expect(typed.verdict == before.verdict, "\(suite): the older binary's text says the same")
            }
        }
        let verdicts = try (older + ["partial"]).map {
            try #require(try document("suites-models-status-\($0)-typed.json").value["footprint"].flatMap(SuiteFootprint.init(typed:))).verdict
        }
        #expect(verdicts == [.fits, .doesNotFit, .fits, .nothingHeld, .nothingHeld, .fitsAsFarAsKnown], "every verdict a recording holds")
    }

    @Test("the CLI's five verdict words (footprint_verdict) read as the text's five endings do")
    func verdictWords() throws {
        // `budget not known here` has no recording: no sandbox hides this
        // machine's memory. The word is the CLI's own (suite_residency.cpp).
        let words = ["fits", "fits as far as known", "does not fit", "budget not known here", "no member holds memory", "something else"]
        let verdicts = try words.map { word in
            try #require(SuiteFootprint(typed: JSONValue.parse(#"{"suite":"s","members":[],"total_bytes":0,"verdict":"\#(word)"}"#)!)).verdict
        }
        #expect(verdicts == [.fits, .fitsAsFarAsKnown, .doesNotFit, .budgetUnknown, .nothingHeld, .unread])
        #expect(SuiteFootprint(typed: JSONValue.parse(#"{"suite":"s"}"#)!) == nil, "no members, no total: not one")
    }

    // MARK: - Asking: the document first, the text on a refusal

    @Test("a binary dated M31 or later is asked for the document; an older one, or one that hasn't said, for the text")
    func asksByDate() {
        #expect(ProseReads.documentsSince == "2026-10-26")
        #expect(ProseReads.asksDocuments(vocabulary: "2026-10-26") && ProseReads.asksDocuments(vocabulary: "2026-11-02"))
        #expect(!ProseReads.asksDocuments(vocabulary: "2026-10-25") && !ProseReads.asksDocuments(vocabulary: nil))
    }

    @Test("the older binary's refusal of the flag is told apart from a refusal of the read itself")
    func refusalsToldApart() {
        for name in ["chats-info-json-refused-older.err", "chats-recall-json-refused-older.err", "graph-dedupe-json-refused-older.err"] {
            #expect(refusal(name, status: 109).refusedTheFormat, "\(name)")
        }
        #expect(!refusal("chats-info-missing-json.err", status: 1).refusedTheFormat, "a chat the CLI can't find")
        #expect(!refusal("chats-recall-forget-json-refused.err", status: 1).refusedTheFormat)
        #expect(!ReadOutcome.document(ReadDocument(value: .object([:]))).refusedTheFormat)
    }

    @Test("asked for the document: its facts, with nothing else run")
    func documentFirst() async throws {
        let runner = Scripted(read: .document(try document("chats-info.json")), command: .done(stdout: printed("chats-info-text.txt")))
        let answer = await runner.documentOrText(binary: Scripted.binary, ["chats", "info", "c"], asksDocument: true, ChatDetails.init(document:))
        guard case .document(let details) = answer else { Issue.record("not typed: \(answer)"); return }
        #expect(details.messages == 4)
        #expect(runner.calls == ["read chats info c"])
    }

    @Test("a refused flag, whatever the date, falls back to the text: today's reading")
    func refusedFallsBack() async {
        let runner = Scripted(read: refusal("chats-info-json-refused-older.err", status: 109), command: .done(stdout: printed("chats-info.txt")))
        let answer = await runner.documentOrText(binary: Scripted.binary, ["chats", "info", "c"], asksDocument: true, ChatDetails.init(document:))
        #expect(answer == .text(printed("chats-info.txt")))
        #expect(runner.calls == ["read chats info c", "run chats info c"])
    }

    @Test("a document without the field falls back to the text; a refusal of the read itself is said, nothing run again")
    func missingFieldAndRefusedRead() async throws {
        let older = Scripted(read: .document(try document("knowledge-info-raw-older.json")), command: .done(stdout: printed("knowledge-info-raw-text.txt")))
        let raw = await older.documentOrText(binary: Scripted.binary, ["knowledge", "info", "k", "--raw"], asksDocument: true, RawConversation.init(document:))
        #expect(raw == .text(printed("knowledge-info-raw-text.txt")))
        #expect(older.calls == ["read knowledge info k --raw", "run knowledge info k --raw"])
        let missing = Scripted(read: refusal("chats-info-missing-json.err", status: 1), command: .done(stdout: "unused"))
        let answer = await missing.documentOrText(binary: Scripted.binary, ["chats", "info", "no-such-chat"], asksDocument: true, ChatDetails.init(document:))
        #expect(answer == .failed("apogee chats: no chat named 'no-such-chat' (exited with status 1)"))
        #expect(missing.calls == ["read chats info no-such-chat"])
    }

    @Test("not asked for the document, the read runs as the command, once")
    func textOnly() async {
        let runner = Scripted(read: .couldNotStart("never asked"), command: .done(stdout: printed("chats-recall.txt")))
        let answer = await runner.documentOrText(binary: Scripted.binary, ["chats", "recall"], asksDocument: false, RecallListing.init(document:))
        #expect(answer == .text(printed("chats-recall.txt")))
        #expect(runner.calls == ["run chats recall"])
    }

    @Test("over a real child: a binary that refuses --output-format as the older one does is read as text, the flag never in the text's argv")
    func realChildFallsBack() async throws {
        let fake = try FakeRead()
        // The older binary's refusal, word for word, then the text read.
        let refused = printed("chats-recall-json-refused-older.err").replacingOccurrences(of: "'", with: "'\\''")
        let text = printed("chats-recall-older.txt").replacingOccurrences(of: "'", with: "'\\''")
        let binary = try fake.binary("""
            case "$*" in
            *--output-format*) printf '%s' '\(refused)' >&2; exit 109 ;;
            "chats recall") printf '%s' '\(text)' ;;
            *) echo "unexpected: $*" >&2; exit 2 ;;
            esac
            """)
        let answer = await ReadRunner().documentOrText(binary: binary, ["chats", "recall"], asksDocument: true, RecallListing.init(document:))
        // The capture keeps stdout as printed but for its last newline.
        #expect(answer == .text(printed("chats-recall-older.txt").trimmingCharacters(in: .newlines)))
    }
}

/// A runner answering every read and every command with one outcome each,
/// every call recorded.
private final class Scripted: CommandRunning, @unchecked Sendable {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    let readOutcome: ReadOutcome
    let commandOutcome: CommandOutcome
    private let log = Mutex<[String]>([])

    init(read: ReadOutcome, command: CommandOutcome) {
        readOutcome = read
        commandOutcome = command
    }

    var calls: [String] { log.withLock { $0 } }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        log.withLock { $0.append("read " + arguments.joined(separator: " ")) }
        return readOutcome
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        log.withLock { $0.append("run " + arguments.joined(separator: " ")) }
        return commandOutcome
    }
}
