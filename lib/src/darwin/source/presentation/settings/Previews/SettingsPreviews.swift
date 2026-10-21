#if DEBUG
import DataPreferences
import Foundation

/// The Settings window's view-model for its previews (ADR 0004), over the
/// recorded store and the documents the module's tests hold, with
/// preferences held in memory -- never the person's own defaults, and nothing
/// starts the binary.
@MainActor
enum SettingsPreview {
    static let runner = PreviewRunner(
        reads: [
            (["models", "list"], SettingsDocuments.modelsList),
            (["auth", "list"], authList),
            (["providers", "scan"], providersScan),
            (["check"], check),
        ],
        commands: [(["config", "get", "permissions"], permissions)],
        refused: [["config", "get"]])

    /// The documents tests/presentation/settings/SettingsViewModelTests.swift
    /// reads its panes from: a key stored for one provider (names only), a
    /// vendor CLI found, a check with a failing row and its remedy, and the
    /// gate levels -- every other `config get` key unset.
    static let authList = #"{"object":"list","data":[{"provider":"anthropic","source":"keychain"}],"backends":[]}"#
    static let providersScan = #"{"object":"list","data":[{"provider":"claude","label":"Claude CLI","tier":"credentials found","backend":"not registered","type":"claude-cli"}]}"#
    static let check = #"{"rows":[{"section":"Config","name":"config.json","status":"ok","detail":"parses cleanly"},{"section":"Runtimes","name":"llama-server","status":"fail","detail":"not installed","remedy":"apogee check --fix"}],"ok":false,"failures":1,"warnings":0}"#
    static let permissions = "run_command: ask\nwrite_file: allow\n"

    /// The view-model, not yet read; `track52` when the build has 52e.
    static func settings(_ pane: SettingsViewModel.Pane = .general, track52: Bool = false) -> SettingsViewModel {
        let model = SettingsViewModel(runner: runner, starter: PreviewJobs(), preferences: Preferences(store: MemoryStore()))
        model.pane = pane
        model.hasTrack52 = { track52 }
        return model
    }

    /// Reads every pane, as the window does on opening -- the version the
    /// module's tests give.
    static func read(_ model: SettingsViewModel) async {
        await model.connect(binary: previewBinary, version: "0.1.9")
    }
}
#endif
