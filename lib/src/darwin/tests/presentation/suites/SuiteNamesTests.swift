import BusinessReads
import DataProtocol
import Foundation
import Testing

@testable import PresentationSuites

/// A suite and its members reach their verbs by their names (GUI M23). A
/// config keeps a suite named `-x`, hand-written, whose chat member is a
/// backend named `-x` (recorded from the built CLI over such a sandbox):
/// `config set-default-suite -x` exited 106, and `models info -x` too. Each
/// line before `--` is scripted to answer CLI11's refusal.
@Suite("Suite names", .timeLimit(.minutes(1)))
@MainActor
struct SuiteNamesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func scripted() -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.saying(["config", "get", "suites"], "--graph\n-h\n-x\n")
        for name in NameVerdicts.names {
            runner.saying(["config", "get", "suites.\(name)"], "chat: \(name)\n")
            runner.reading(["models", "status", "--suite", name], #"{"fields":[{"field":"suite","value":"\#(name)   (dash)"},{"field":"chat","value":"\#(name)   (via suite \#(name))"}]}"#)
            runner.reading(["models", "info", "--", name], #"{"fields":[{"field":"backend","value":"\#(name)"},{"field":"type","value":"mock"},{"field":"source","value":"remote (no local file to inspect)"}],"name":"\#(name)"}"#)
        }
        runner.reading(["models", "status"], #"{"fields":[{"field":"chat","value":"mock   (via models.default)"}]}"#)
        runner.reading(["models", "list"], #"{"object":"list","data":[{"backend":"-x","backend_type":"mock"},{"backend":"-h","backend_type":"mock"},{"backend":"--graph","backend_type":"mock"}]}"#)
        runner.reading(["system"], #"{"gpu":{"model_budget_bytes":8589934592}}"#)
        return runner
    }

    @Test("Make default on a -x suite runs config set-default-suite -- -x, and its member's memory is read by models info -- -x")
    func dashSuite() async throws {
        let runner = scripted()
        let suites = SuitesViewModel(runner: runner)
        var said: [String] = []
        suites.say = { said.append($0) }
        await suites.connect(binary: Self.binary)
        let suite = try #require(suites.suites.first { $0.name == "-x" }, "suites: \(suites.suites.map(\.name))")
        #expect(runner.calls.contains("read models info -- -x"), "calls: \(runner.calls)")
        await suites.makeDefault(suite)
        #expect(runner.calls.contains("run config set-default-suite -- -x"))
        #expect(!said.contains { $0.hasPrefix("That didn't work") }, "said: \(said)")
    }

    @Test("every line Suites builds with -x, -h or --graph reaches the verb: each recorded from the built CLI, none the parser's refusal")
    func sweep() async {
        let runner = scripted()
        let suites = SuitesViewModel(runner: runner)
        await suites.connect(binary: Self.binary)
        for suite in suites.suites { await suites.makeDefault(suite) }
        let (missing, refused) = NameVerdicts.judged(NameVerdicts.lines(ofCalls: runner.calls, separator: " "))
        #expect(missing.isEmpty, "never recorded -- add these rows to tests/presentation/shell/Fixtures/argv-name-verdicts.txt and run scripts/refresh-argv-name-verdicts.sh <the built apogee>:\n\(missing.joined(separator: "\n"))")
        #expect(refused.isEmpty, "the parser refuses:\n\(refused.joined(separator: "\n"))")
    }
}
