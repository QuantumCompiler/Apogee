import BusinessReads
import DataProtocol
import Foundation
import Testing

private func fixture(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// GUI 4i: the CLI's adapter versions (53b) as `train versions` writes them,
/// recorded from the branch's binary over a sandbox: `based` promoted as an
/// adapter onto an F16 stand-in base, `q4` onto a quantized one and gated
/// there (53c), `tuned` merged.
@Suite("Adapter reads")
struct AdapterReadsTests {
    @Test("an adapter version's file is its adapter, and the ledger says it holds adapters")
    func adapterVersions() throws {
        let ledgers = VersionLedger.list(from: ReadDocument(text: fixture("train-versions-adapters.json"))!)
        #expect(ledgers.map(\.backend) == ["based", "q4", "tuned"])
        let based = try #require(ledgers.first)
        #expect(based.versions.first?.path == "/Users/someone/.apogee/training/versions/based/v1.lora.gguf")
        let tuned = try #require(ledgers.last)
        #expect(tuned.versions.first?.path.hasSuffix("tuned-v1.gguf") == true)
    }
}

/// GUI 4i: a backend's adapters as `config get` lists them (55g) and as the
/// doctor states their pins (53c), and a suite's footprint as `models status
/// --suite` prices it -- each recorded from the branch's binary.
@Suite("Adapters and footprints, read")
struct AdaptersAndFootprintsTests {
    @Test("the ledgers: which hold adapters, where a version's gate scored it")
    func ledgers() throws {
        let ledgers = VersionLedger.list(from: ReadDocument(text: fixture("train-versions-adapters.json"))!)
        #expect(ledgers.map(\.holdsAdapters) == [true, true, false])
        let q4 = try #require(ledgers.first { $0.backend == "q4" })
        #expect(q4.versions.first?.isAdapter == true)
        #expect(q4.versions.first?.evalScoredOn == "tiny-Q4_K_M.gguf (Q4_K_M)")
        #expect(ledgers.last?.versions.first?.isAdapter == false)
    }

    @Test("config get's adapters: a path per line, its scale after the last colon when not 1, written back as --adapters takes it")
    func adapters() {
        let adapters = BackendAdapter.list(fixture("config-get-adapters.txt"))
        #expect(adapters == [
            BackendAdapter(path: "/Users/someone/.apogee/training/versions/based/v1.lora.gguf"),
            BackendAdapter(path: "/Users/someone/extra.lora.gguf", scale: 0.5),
        ])
        #expect(adapters.map(\.argument) == [
            "/Users/someone/.apogee/training/versions/based/v1.lora.gguf", "/Users/someone/extra.lora.gguf:0.5",
        ])
        #expect(adapters.map(\.file) == ["v1.lora.gguf", "extra.lora.gguf"])
        #expect(BackendAdapter.list(fixture("config-get-adapters-none.txt")).isEmpty)
        #expect(BackendAdapter(line: "/Users/someone/odd:name/a.gguf").path == "/Users/someone/odd:name/a.gguf", "only a number is a scale")
        #expect(BackendAdapter(line: "/Users/someone/a.gguf:1.5").scale == 1.5)
    }

    @Test("check's Adapters rows by adapter: pinned ones ok with their scale, one over a changed base failing with its door")
    func pins() throws {
        let check = try #require(JSONValue.parse(fixture("check-adapters.json")))
        let based = BackendAdapter.list(fixture("config-get-adapters.txt"))
        let rows = AdapterCheck.rows(for: based, of: "based", in: check)
        #expect(rows.count == 2 && rows.values.allSatisfy { $0.status == "ok" })
        #expect(rows["/Users/someone/extra.lora.gguf"]?.detail == "applies over tiny-F16.gguf at scale 0.5, pinned to its weights (sha256 ce5065b7f3dc)")
        let q4 = BackendAdapter.list(fixture("config-get-adapters-q4.txt"))
        let failing = try #require(AdapterCheck.rows(for: q4, of: "q4", in: check).values.first)
        #expect(failing.status == "fail")
        #expect(failing.detail.hasPrefix("pinned to other weights (sha256 a81cdd0551b6)"))
        #expect(failing.remedy == "apogee train promote 20261026-065638 --as q4 --adapter --suite <path|name> to promote its run again over these weights")
        #expect(AdapterCheck.rows(for: q4, of: "tuned", in: check).isEmpty, "rows are named by backend and file")
    }

    @Test("a suite's footprint: each member as the CLI priced it, the total, and the verdict it ends in")
    func footprints() throws {
        func footprint(_ name: String) -> SuiteFootprint? {
            ReadDocument(text: fixture("suites-models-status-\(name).json")).flatMap(SuiteFootprint.init(document:))
        }
        let shared = try #require(footprint("shared"))
        #expect(shared.suite == "shared" && shared.verdict == .fits && shared.verdictWords == "fits")
        #expect(shared.members.map(\.backend) == ["root", "scribe", "helper"])
        #expect(shared.members.map(\.roles) == [["chat"], ["extraction"], ["utility"]])
        #expect(shared.members[2].said == "136 MiB -- shares root's 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens, 0 MiB of adapters")
        #expect(shared.total.hasPrefix("suite shared needs 42392 MiB of this machine's 110100 MiB: root 41096 + scribe 136 (shares root's weights, 40960 MiB saved)"))
        #expect(footprint("vast")?.verdict == .doesNotFit)
        #expect(footprint("vast")?.verdictWords == "it does not fit")
        #expect(footprint("fast")?.verdict == .nothingHeld)
        #expect(footprint("cloud")?.members.first?.said == "nothing held here")
        #expect(ReadDocument(text: fixture("suites-models-status.json")).flatMap(SuiteFootprint.init(document:))?.suite == "fast", "the default suite's, unasked")
        #expect(ReadDocument(text: fixture("models-status-kinds-registered.json")).flatMap(SuiteFootprint.init(document:)) == nil, "no suite, no footprint")
        // The other endings, as the CLI's suite_residency_test pins them.
        #expect(SuiteFootprint(text: "suite research\ntotal: suite research needs at least 5692 MiB of this machine's 98304 MiB: root 3830 + helper 838 + embedder unknown (no recorded size) + 1024 margin -- fits as far as known")?.verdict == .fitsAsFarAsKnown)
        #expect(SuiteFootprint(text: "suite research\ntotal: suite research needs 5692 MiB: root 3830 + helper 838 + 1024 margin -- this machine's budget is not known here")?.verdict == .budgetUnknown)
    }
}
