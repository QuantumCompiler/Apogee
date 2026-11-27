import Foundation
import Testing

@testable import PresentationSettings

/// The previews' recorded reads answer the lines the view-models build (GUI
/// M23): a key path held in a variable, a provider registered and the Xcode
/// preset's server are keyed by prefixes of their lines, so a word that
/// moves after `--` would otherwise leave the preview reading unset, and
/// fail nothing.
@Suite("Settings preview reads", .timeLimit(.minutes(1)))
@MainActor
struct PreviewReadsTests {
    @Test("every read the Settings previews ask is answered, and every prefix holding a name after -- is one a line reaches")
    func previewReads() async {
        let log = PreviewReadLog()
        await PreviewReadLog.$current.withValue(log) {
            for runner in [SettingsPreview.unsetAddressRunner, SettingsPreview.vendorRunner(), SettingsPreview.vendorRunner(older: true)] {
                await SettingsPreview.read(SettingsPreview.settings(runner: runner, vocabulary: "2026-10-26"))
            }
            for provider in ["codex", "ollama"] {
                await SettingsPreview.register(SettingsPreview.settings(runner: SettingsPreview.m33Runner(), vocabulary: "2026-10-26"), provider)
            }
            for registered in [false, true] {
                let settings = SettingsPreview.settings(.xcode, runner: SettingsPreview.xcodeRunner(registered: registered))
                await SettingsPreview.read(settings)
                await settings.xcode.read()
                if !registered { await settings.xcode.register() }
            }
        }
        #expect(log.unanswered.isEmpty, "reads no recorded document answers: \(log.unanswered)")
        #expect(log.unreached.isEmpty, "prefixes no line reaches: \(log.unreached)")
    }
}
