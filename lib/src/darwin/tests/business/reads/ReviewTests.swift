import BusinessReads
import DataProtocol
import Foundation
import Testing

private func document(_ name: String) -> ReadDocument {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name).json")
    return ReadDocument(text: try! String(contentsOf: url, encoding: .utf8))!
}

/// Recorded from the installed binary over a scratch home, a fake `gh` first
/// on PATH answering for the forge (the CLI's review e2e's way) and a mock
/// reviewer: a dry run, a run refused posting on a pipe, a posted run, a
/// silent one; the targets, a dry sweep, the skills.
@Suite("Review")
struct ReviewTests {
    @Test("a review's document: what it read, how, the finding with its anchor, the counts in the CLI's order, the batch's fate")
    func run() throws {
        let run = try #require(ReviewRun(document: document("review-run-dry")))
        #expect(run.url == "https://github.com/acme/widget/pull/12" && run.head == "cafe2222cafe2222")
        #expect(run.skills == ["security"] && run.plan == "named" && run.model == "reviewer" && run.chunks == 1)
        #expect(run.counts.map(\.0) == ["candidates", "off_change", "not_new", "known", "duplicates", "dismissed", "tightened"])
        #expect(run.counts.first?.1 == 1)
        let finding = try #require(run.findings.first)
        #expect(finding.location == "8-10" && finding.foundBy == ["security"] && finding.inline && !finding.tightened)
        #expect(run.inlineFindings.count == 1 && run.overCap.isEmpty)
        #expect(run.dryRun)
        #expect(ReviewRun(document: document("review-targets")) == nil, "a list is no review")
    }

    @Test("posted, refused and silent: the batch's three fates as the CLI words them")
    func fates() throws {
        #expect(try #require(ReviewRun(document: document("review-run-posted"))).posted == .posted(comments: 1, summary: true, batched: true))
        let refused = try #require(ReviewRun(document: document("review-run-refused")))
        #expect(refused.posted == .notPosted(reason: "nobody to ask -- run with --yes to post, or --dry-run to review only"))
        let silent = try #require(ReviewRun(document: document("review-run-silent")))
        #expect(silent.isSilent && silent.findings.isEmpty)
    }

    @Test("targets, a sweep and the skills read whole")
    func reads() throws {
        let targets = ReviewTarget.list(from: document("review-targets"))
        #expect(targets.map(\.name) == ["acme-all", "widget"] && targets[0].forge == "github" && targets[0].repo == nil)
        let sweep = try #require(ReviewSweep(document: document("review-sweep")))
        #expect(sweep.cap == 20 && !sweep.allowPost && sweep.targets.count == 1)
        #expect(sweep.targets[0].projects[0].watermarkBefore == nil && sweep.targets[0].projects[0].skippedDrafts == 0)
        let skills = ReviewSkill.list(from: document("review-skills"))
        #expect(skills.count == 7 && skills.allSatisfy { $0.source == "shipped" && $0.prompt == nil })
        let shown = try #require(ReviewSkill(document: document("review-skill-show")))
        #expect(shown.name == "security" && shown.prompt != nil)
    }
}
