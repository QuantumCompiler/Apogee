import BusinessReads
import DataProtocol
import Foundation
import Testing

@testable import PresentationSuites

/// A recorded read, by its file name under the reads' fixtures.
func recorded(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// GUI 4i: the Suites place over the reads recorded from the branch's binary
/// over a sandbox -- `shared`, three llama.cpp members on one 40 GiB base
/// (the helper with an adapter), which the CLI's footprint counts once (53d);
/// `vast`, over a 200 GiB base, which does not fit; `cloud`; and two mock
/// suites. The bases are sparse GGUF headers with their size records, as the
/// CLI's own tests write them, so `models info` and the footprint agree.
@Suite("A shared base fits as the CLI says", .timeLimit(.minutes(1)))
@MainActor
struct SharedBaseFitTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let names = ["cloud", "fast", "pair", "shared", "vast"]

    /// Every read the place makes, answered by its recorded document; the
    /// general `models status` first, so each suite's own read wins.
    static func runner() -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.saying(["config", "get", "suites"], recorded("suites-config-get.txt"))
        for name in names {
            runner.saying(["config", "get", "suites.\(name)"], recorded("suites-config-get-\(name).txt"))
        }
        runner.reading(["models", "status"], recorded("suites-models-status.json"))
        for name in names {
            runner.reading(["models", "status", "--suite", name], recorded("suites-models-status-\(name).json"))
        }
        runner.reading(["models", "list"], recorded("suites-models-list.json"))
        runner.reading(["system"], recorded("suites-system.json"))
        for backend in ["root", "helper", "scribe", "vast"] {
            runner.reading(["models", "info", backend], recorded("suites-models-info-\(backend).json"))
        }
        return runner
    }

    @Test("a suite on one shared base, which the CLI admits, is never shown as does not fit")
    func sharedBaseAdmitted() async throws {
        let suites = SuitesViewModel(runner: Self.runner())
        await suites.connect(binary: Self.binary)
        let shared = try #require(suites.suites.first { $0.name == "shared" })
        let said = SuitesViewModel.words(suites.fit(shared))
        #expect(!said.contains("does not fit"), "the CLI's footprint counts root's weights once: \(said)")
        #expect(said.hasSuffix("-- fits"))
    }

    @Test("the CLI's verdict on each: its footprint's members and words, a suite that does not fit and one holding nothing said as the CLI says them")
    func theCLIsVerdicts() async throws {
        let suites = SuitesViewModel(runner: Self.runner())
        await suites.connect(binary: Self.binary)
        #expect(suites.suites.map(\.name) == Self.names)
        #expect(suites.defaultSuite == "fast")
        let shared = try #require(suites.suites.first { $0.name == "shared" })
        #expect(suites.footprint(shared)?.members.map(\.backend) == ["root", "scribe", "helper"])
        #expect(SuitesViewModel.summary(suites.fit(shared)) == "fits")
        #expect(!SuitesViewModel.isEstimate(suites.fit(shared)) && suites.members(shared).isEmpty, "nothing estimated where the CLI priced it")
        let vast = try #require(suites.suites.first { $0.name == "vast" })
        #expect(SuitesViewModel.overBudget(suites.fit(vast)) && SuitesViewModel.summary(suites.fit(vast)) == "it does not fit")
        let fast = try #require(suites.suites.first { $0.name == "fast" })
        #expect(SuitesViewModel.summary(suites.fit(fast)) == "no member holds memory on this machine")
    }

    @Test("with no read stating the CLI's verdict, the estimate counts a weights file two members share once, and says it is an estimate")
    func estimateSharesTheBase() async throws {
        let runner = Self.runner()
        // A read that states no footprint: a binary before the CLI's 27e.
        for name in Self.names { runner.reading(["models", "status", "--suite", name], #"{"fields":[]}"#) }
        let suites = SuitesViewModel(runner: runner)
        await suites.connect(binary: Self.binary)
        let shared = try #require(suites.suites.first { $0.name == "shared" })
        let mib = 1 << 20
        #expect(suites.fit(shared) == .fits(total: (40960 + 136 * 3) * mib, budget: 115_448_725_504), "root pays the base; scribe and helper their caches")
        #expect(SuitesViewModel.summary(suites.fit(shared)).hasPrefix("estimated here: fits: "))
        let vast = try #require(suites.suites.first { $0.name == "vast" })
        #expect(suites.fit(vast) == .doesNotFit(total: (40960 + 136 + 204800 + 136) * mib, budget: 115_448_725_504))
    }

    @Test("the estimate prices a member's adapters by their files, as the CLI's footprint does; one it cannot read leaves the member unknown")
    func estimatePricesAdapters() async throws {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("suites-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: folder) }
        let adapter = folder.appendingPathComponent("helper.lora.gguf")
        try Data(count: 3 << 20).write(to: adapter)
        let runner = Self.runner()
        for name in Self.names { runner.reading(["models", "status", "--suite", name], #"{"fields":[]}"#) }
        runner.saying(["config", "get", "backends.helper.adapters"], adapter.path + "\n")
        let suites = SuitesViewModel(runner: runner)
        await suites.connect(binary: Self.binary)
        let shared = try #require(suites.suites.first { $0.name == "shared" })
        let mib = 1 << 20
        #expect(suites.fit(shared) == .fits(total: (40960 + 136 * 3 + 3) * mib, budget: 115_448_725_504))
        runner.saying(["config", "get", "backends.helper.adapters"], folder.appendingPathComponent("gone.gguf").path + "\n")
        await suites.refresh()
        #expect(suites.fit(shared) == .unknown(members: ["helper"]))
    }

    @Test("Open a chat under it and Switch the session to it reach the library, forced only where the CLI says it does not fit")
    func doorsToTheSession() async throws {
        let suites = SuitesViewModel(runner: Self.runner())
        let doors = Doors()
        suites.openChat = { doors.opened.append("\($0) \($1)") }
        suites.switchSession = { doors.switched.append("\($0) \($1)") }
        suites.sessionSwitchable = { doors.open }
        await suites.connect(binary: Self.binary)
        let shared = try #require(suites.suites.first { $0.name == "shared" })
        let vast = try #require(suites.suites.first { $0.name == "vast" })
        suites.openChat(under: shared)
        suites.openChat(under: vast)
        #expect(doors.opened == ["shared false", "vast true"])
        suites.switchSession(to: shared)
        #expect(doors.switched.isEmpty && !suites.canSwitchSession, "no session open: nothing to switch")
        doors.open = true
        suites.switchSession(to: shared)
        #expect(doors.switched == ["shared false"])
    }
}

/// What the composition's doors were asked, and whether a session is open.
@MainActor
final class Doors {
    var opened: [String] = []
    var switched: [String] = []
    var open = false
}
