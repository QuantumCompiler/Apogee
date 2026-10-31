import BusinessJobs
import BusinessReads
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationSettings

/// GUI M15, Settings' side of Recall: on a binary dated 2026-10-26 or later
/// what recall holds is `chats recall`'s document, and Forget's question says
/// how many summaries it erases and names them; an older binary, or one that
/// refuses the flag, reads and asks as today.
@Suite("Settings: Forget names the summaries it erases (GUI M15)", .timeLimit(.minutes(1)))
@MainActor
struct RecallListedTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func settings(_ runner: RuleRunner, vocabulary: String?) async -> SettingsViewModel {
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        model.hasTrack52 = { true }
        model.vocabulary = { vocabulary }
        await model.connect(binary: Self.binary, version: "0.2.0")
        return model
    }

    @Test("on an M31 binary the pane reads the listing, and Forget's question names each summary it erases")
    func forgetNamesThem() async {
        let runner = RuleRunner()
        runner.reading(["chats", "recall"], recordedText("chats-recall.json"))
        runner.when(["chats", "recall", "--forget", "--yes"], .done(stdout: recordedText("chats-recall-forget-three.txt")))
        let model = await settings(runner, vocabulary: "2026-10-26")
        #expect(runner.calls.contains("read chats recall") && !runner.calls.contains("run chats recall"))
        #expect(model.recallHolds == "recall holds 3 chat summaries in /Users/someone/.apogee/memory/chats.db")
        #expect(model.recallListing?.summaries.map(\.title) == ["Launch Emails", "Dev Server Port", "Release Checklist"])
        await model.askToForget()
        #expect(model.forgetting)
        #expect(model.forgetMessage.contains("3 chat summaries"))
        for name in ["Launch Emails", "Dev Server Port", "Release Checklist"] {
            #expect(model.forgetMessage.contains("“\(name)”"), "names \(name)")
        }
        #expect(!runner.calls.contains { $0.hasPrefix("run chats recall --forget") })
        await model.forgetRecall()
        #expect(runner.calls.contains("run chats recall --forget --yes"), "as text: the CLI refuses --forget with --output-format")
        runner.reading(["chats", "recall"], recordedText("chats-recall-empty.json"))
        await model.askToForget()
        #expect(model.recallListing?.summaries == [] && !model.canForget && !model.forgetting, "an empty recall asks nothing")
    }

    @Test("an older binary, and one that refuses the flag, read the count line and ask as today")
    func olderAndRefused() async {
        let older = RuleRunner()
        older.when(["chats", "recall"], .done(stdout: recordedText("chats-recall-older.txt")))
        let before = await settings(older, vocabulary: "2026-10-25")
        #expect(!older.calls.contains("read chats recall") && older.calls.contains("run chats recall"))
        #expect(before.recallHolds == "recall holds 3 chat summaries in /Users/someone/.apogee/memory/chats.db")
        #expect(before.recallListing == nil)
        await before.askToForget()
        #expect(before.forgetMessage.hasPrefix("Now: recall holds 3 chat summaries"))

        let refusing = RuleRunner()
        var tail = BoundedTail()
        tail.append(Data(recordedText("chats-recall-json-refused-older.err").utf8))
        refusing.failingRead(["chats", "recall"], .failed(exit: ChildExit(reason: .exited, status: 109), stderrTail: tail))
        refusing.when(["chats", "recall"], .done(stdout: recordedText("chats-recall-older.txt")))
        let fallback = await settings(refusing, vocabulary: "2026-10-26")
        #expect(refusing.calls.contains("read chats recall") && refusing.calls.contains("run chats recall"))
        #expect(fallback.recallHolds == before.recallHolds && fallback.recallListing == nil)
    }
}
