import BusinessReads
import DataProtocol
import Foundation
import Testing

@testable import PresentationModels

private func recorded(_ name: String, _ ext: String = "json") -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name).\(ext)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// GUI M16: the Backends table marks drafters from `models list` alone where
/// it states them -- each `mlx` row's `draft: {path, can_draft, refusal?}`,
/// the CLI's M32 -- where M5 read every `mlx` entry's `models info` on each
/// refresh. Recorded from the branch binary (schema 2026-10-26) over M5's
/// tiny fake MLX directories, the home written `/Users/someone`, every base
/// registered by `config scan --register`:
/// - `rows-drafts-*`: the E4B paired with its drafter, the 12B paired with its
///   own and that drafter's directory then deleted, Qwen3 unpaired; the
///   installed binary's list over the same home (`-older`) states none;
/// - `remove-list`: the same bases, nothing paired: no row carries `draft`.
@Suite("Drafters from the list (GUI M16)", .timeLimit(.minutes(1)))
@MainActor
struct DraftersFromTheListTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let drafter = "Gemma4-E4B-Assistant/mlx/4d5e6f708192"
    static let gone = "/Users/someone/.apogee/models/Gemma4-12B-Assistant/mlx/8192a3b4c5d6"
    static let bases = ["Gemma4-E4B-4bit", "Gemma4-12B-4bit", "Qwen3-8B-4bit"]

    /// The paired home, every backend's `models info` answered too -- so a
    /// per-row read, were one made, would mark the table as M5's did.
    static func paired() -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("rows-drafts-list"))
        runner.reading(["models", "status"], recorded("rows-drafts-status"))
        for base in bases { runner.reading(["models", "info", base], recorded("rows-drafts-info-\(base)")) }
        return runner
    }

    func connected(_ runner: ScriptedRunner, vocabulary: String?) async -> ModelsViewModel {
        let model = ModelsViewModel(runner: runner)
        model.vocabulary = { vocabulary }
        await model.connect(binary: Self.binary)
        return model
    }

    func row(_ backend: String, in model: ModelsViewModel) throws -> ModelRow {
        try #require(model.rows.first { $0.backend == backend && !$0.isStored }, "rows: \(model.rows.map(\.name))")
    }

    @Test("over M32's rows the table marks each drafter from the list alone: the paired base drafting, the one whose drafter is gone in the CLI's words, the unpaired one none -- and no models info is read for them")
    func draftersFromTheList() async throws {
        let runner = Self.paired()
        let model = await connected(runner, vocabulary: "2026-10-26")
        let e4b = try #require(model.drafterMark(try row("Gemma4-E4B-4bit", in: model)), "the E4B drafts")
        #expect(e4b == DrafterMark(drafter: Self.drafter, drafts: true, said: ""), "named by its store handle")
        #expect(e4b.help == "Drafter: \(Self.drafter)")
        let wider = try #require(model.drafterMark(try row("Gemma4-12B-4bit", in: model)), "the 12B is paired")
        #expect(!wider.drafts, "drawn in the warning colour")
        #expect(wider.drafter == Self.gone, "its path, the store no longer listing it")
        #expect(wider.said == "cannot draft beside it: there is no drafter at \(Self.gone) -- 'apogee models convert <a draft head> --mlx' makes one")
        #expect(model.drafterMark(try row("Qwen3-8B-4bit", in: model)) == nil)
        #expect(!runner.calls.contains { $0.hasPrefix("read models info") }, "no per-row read: \(runner.calls)")
        #expect(!model.offersDrafterPairing(try row("Gemma4-E4B-4bit", in: model)), "paired already")
    }

    @Test("a row carrying draft is believed whatever the date: a binary that hasn't said still reads no models info for a list that states them")
    func theFieldDecides() async throws {
        let runner = Self.paired()
        let model = await connected(runner, vocabulary: nil)
        #expect(model.drafterMark(try row("Gemma4-12B-4bit", in: model))?.drafts == false)
        #expect(!runner.calls.contains { $0.hasPrefix("read models info") }, "\(runner.calls)")
    }

    @Test("the details still read the selected base's models info, and say its refusal as the table does")
    func detailsSayTheSame() async throws {
        let runner = Self.paired()
        let model = await connected(runner, vocabulary: "2026-10-26")
        await model.select(try row("Gemma4-12B-4bit", in: model))
        #expect(runner.calls.filter { $0.hasPrefix("read models info") } == ["read models info Gemma4-12B-4bit"], "the selection's, alone")
        #expect(model.draft?.refusal == "there is no drafter at \(Self.gone) -- 'apogee models convert <a draft head> --mlx' makes one")
        #expect(model.drafterMark(try row("Gemma4-12B-4bit", in: model))?.drafts == false)
    }

    @Test("on M32's binary a list with nothing paired reads no models info either: no base marked, Pair a drafter… offered on the vision ones")
    func nothingPairedReadsNothing() async throws {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("remove-list"))
        runner.reading(["models", "status"], recorded("remove-status"))
        let model = await connected(runner, vocabulary: "2026-10-26")
        #expect(model.rows.filter { !$0.isStored }.allSatisfy { model.drafterMark($0) == nil })
        #expect(!runner.calls.contains { $0.hasPrefix("read models info") }, "\(runner.calls)")
        #expect(model.offersDrafterPairing(try row("Gemma4-E4B-4bit", in: model)))
        #expect(!model.offersDrafterPairing(try row("Qwen3-8B-4bit", in: model)), "mlx-lm's build takes no drafter")
    }

    @Test("older rows keep today's per-row read: the installed binary's list, and a binary that hasn't said, read each mlx entry's models info once")
    func olderRowsReadEachInfo() async throws {
        for vocabulary in ["2026-10-25", nil] as [String?] {
            let runner = Self.paired()
            runner.reading(["models", "list"], recorded("rows-drafts-list-older"))
            let model = await connected(runner, vocabulary: vocabulary)
            for base in Self.bases {
                #expect(runner.calls.filter { $0 == "read models info \(base)" }.count == 1, "\(base), \(vocabulary ?? "unsaid")")
            }
            let e4b = try #require(model.drafterMark(try row("Gemma4-E4B-4bit", in: model)))
            #expect(e4b.drafts && e4b.said.hasPrefix("drafts beside it through mlx-vlm's speculative path"), "the CLI's words from its draft line")
            #expect(model.drafterMark(try row("Gemma4-12B-4bit", in: model))?.drafts == false)
        }
    }
}
