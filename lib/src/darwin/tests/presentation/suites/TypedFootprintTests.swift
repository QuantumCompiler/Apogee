import BusinessReads
import DataProtocol
import Foundation
import Testing

@testable import PresentationSuites

/// GUI M15, the Suites fit: on a binary carrying the CLI's M31, `models status
/// --suite`'s typed `footprint` object is read where the document has it --
/// exact bytes, the base a member shares, the margin and the budget -- and the
/// text footprint only where it hasn't. Recorded from the branch binary over
/// 4i's sandbox shape again (a 40 GiB base three members share, a 200 GiB one,
/// a cloud member, mock members), with `partial`, whose second member's
/// weights are gone.
@Suite("Suites: the fit from the typed footprint (GUI M15)", .timeLimit(.minutes(1)))
@MainActor
struct TypedFootprintTests {
    static let names = ["cloud", "fast", "pair", "partial", "shared", "vast"]

    /// The place's reads over the branch binary's typed recordings; the
    /// general reads -- each suite's members, the kinds, the budget -- 4i's,
    /// which M15's sandbox repeats byte for byte.
    static func runner() -> ScriptedRunner {
        let runner = SharedBaseFitTests.runner()
        runner.saying(["config", "get", "suites"], recorded("suites-config-get-typed.txt"))
        runner.saying(["config", "get", "suites.partial"], recorded("suites-config-get-partial.txt"))
        runner.reading(["models", "status"], recorded("suites-models-status-typed.json"))
        for name in names {
            runner.reading(["models", "status", "--suite", name], recorded("suites-models-status-\(name)-typed.json"))
        }
        return runner
    }

    @Test("each suite's fit is the typed footprint: exact bytes, the shared base said, the CLI's verdict word")
    func typedFit() async throws {
        let suites = SuitesViewModel(runner: Self.runner())
        await suites.connect(binary: SharedBaseFitTests.binary)
        #expect(suites.suites.map(\.name) == Self.names)
        let shared = try #require(suites.suites.first { $0.name == "shared" })
        let footprint = try #require(suites.footprint(shared))
        #expect(footprint.isTyped)
        #expect(footprint.members.map(\.bytes) == [43_092_279_296, 142_606_336, 142_606_688])
        #expect(footprint.totalBytes == 44_451_234_144 && footprint.budgetBytes == 115_448_725_504)
        #expect(footprint.members.map(\.shares) == ["", "root", "root"])
        #expect(footprint.members[2].said.contains("on root's weights"), "a shared base said, never counted twice")
        #expect(SuitesViewModel.summary(suites.fit(shared)) == "fits" && !SuitesViewModel.isEstimate(suites.fit(shared)))
        #expect(SuitesViewModel.words(suites.fit(shared)).hasSuffix("-- fits"))
        let vast = try #require(suites.suites.first { $0.name == "vast" })
        #expect(SuitesViewModel.overBudget(suites.fit(vast)) && SuitesViewModel.summary(suites.fit(vast)) == "does not fit")
        let partial = try #require(suites.suites.first { $0.name == "partial" })
        #expect(suites.footprint(partial)?.verdict == .fitsAsFarAsKnown)
        #expect(suites.footprint(partial)?.members.last?.said == "unknown -- no recorded size")
        let pair = try #require(suites.suites.first { $0.name == "pair" })
        #expect(SuitesViewModel.summary(suites.fit(pair)) == "no member holds memory")
    }

    @Test("the guardrail, on the screen: the typed fit and the text fit give each suite the same verdict, and the same doors")
    func sameVerdictEitherWay() async throws {
        let typed = SuitesViewModel(runner: Self.runner())
        await typed.connect(binary: SharedBaseFitTests.binary)
        let text = SuitesViewModel(runner: SharedBaseFitTests.runner())
        await text.connect(binary: SharedBaseFitTests.binary)
        for suite in text.suites {
            let new = try #require(typed.footprint(suite), "\(suite.name) typed")
            let old = try #require(text.footprint(suite), "\(suite.name) as text")
            #expect(new.isTyped && !old.isTyped)
            #expect(new.verdict == old.verdict, "\(suite.name): \(new.verdict) and \(old.verdict)")
            #expect(SuitesViewModel.overBudget(typed.fit(suite)) == SuitesViewModel.overBudget(text.fit(suite)), "\(suite.name) forced alike")
        }
    }

    @Test("a document whose footprint carries no typed object is read from its text, as today")
    func textWhereNoObject() async throws {
        let suites = SuitesViewModel(runner: SharedBaseFitTests.runner())
        await suites.connect(binary: SharedBaseFitTests.binary)
        let shared = try #require(suites.suites.first { $0.name == "shared" })
        #expect(suites.footprint(shared)?.isTyped == false)
        #expect(SuitesViewModel.words(suites.fit(shared)).hasPrefix("suite shared needs 42392 MiB"), "the CLI's text, as said")
    }
}
