#if DEBUG
import DataPreferences
import Foundation
import InfrastructureProcess

/// The Settings window's view-model for its previews (ADR 0004), over the
/// recorded store and the documents the module's tests hold, with
/// preferences held in memory -- never the person's own defaults, and nothing
/// starts the binary.
@MainActor
enum SettingsPreview {
    static let runner = runner(check: check)

    /// The panes' reads, `check` answering with `check`'s document.
    static func runner(check: String) -> PreviewRunner {
        PreviewRunner(
            reads: [
                (["models", "list"], SettingsDocuments.modelsList),
                (["auth", "list"], authList),
                (["providers", "scan"], providersScan),
                (["check"], check),
            ],
            commands: [(["config", "get", "permissions"], permissions)],
            refused: [["config", "get"]])
    }

    /// The documents tests/presentation/settings/SettingsViewModelTests.swift
    /// reads its panes from: a key stored for one provider (names only), a
    /// vendor CLI found, a check with a failing row and its remedy, and the
    /// gate levels -- every other `config get` key unset.
    static let authList = #"{"object":"list","data":[{"provider":"anthropic","source":"keychain"}],"backends":[]}"#
    static let providersScan = #"{"object":"list","data":[{"provider":"claude","label":"Claude CLI","tier":"credentials found","backend":"not registered","type":"claude-cli"}]}"#
    static let check = #"{"rows":[{"section":"Config","name":"config.json","status":"ok","detail":"parses cleanly"},{"section":"Runtimes","name":"llama-server","status":"fail","detail":"not installed","remedy":"apogee check --fix"}],"ok":false,"failures":1,"warnings":0}"#
    static let permissions = "run_command: ask\nwrite_file: allow\n"

    /// The children's `PATH` as a login shell answers it: the user's case
    /// (2026-10-22), the vendor CLIs in ~/.local/bin, Homebrew and
    /// /usr/local/bin ahead of the system's.
    static let shellPath = ShellPath.Resolution(
        path: "/Users/someone/.local/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
        source: .shell, shell: "/bin/zsh")

    /// The fallback, shaped by the resolver itself from launchd's `PATH` --
    /// never asked, so no shell starts -- for a shell past its deadline.
    static let fallbackPath = ShellPath.Resolution(
        path: ShellPath(environment: ["HOME": "/Users/someone", "PATH": ShellPath.launchdPath, "SHELL": "/bin/zsh"])
            .fallback.path,
        source: .fallback, shell: "/bin/zsh", reason: "didn't answer within 5 seconds")

    /// The person's override as discovery passed it over: a build of their
    /// own whose `version` fails, in the words discovery says it in
    /// (tests/data/discovery/BinaryDiscoveryTests.swift).
    static let skippedOverride = (path: "/Users/someone/tools/apogee", reason: "`version` exited with status 1: error: broken install")

    /// The view-model, not yet read; `track52` when the build has 52e,
    /// `childPath` what the app's resolver answers, `override` the binary
    /// the person pointed the app at, and `check` the document `check`
    /// answers with.
    static func settings(
        _ pane: SettingsViewModel.Pane = .general, track52: Bool = false,
        childPath: ShellPath.Resolution = shellPath, override: String? = nil, check: String = check
    ) -> SettingsViewModel {
        let preferences = Preferences(store: MemoryStore())
        preferences.binaryOverride = override.map { URL(fileURLWithPath: $0) }
        let model = SettingsViewModel(runner: runner(check: check), starter: PreviewJobs(), preferences: preferences)
        model.pane = pane
        model.hasTrack52 = { track52 }
        model.childPathSource = { childPath }
        return model
    }

    /// Reads every pane, as the window does on opening -- the version the
    /// module's tests give -- and the children's `PATH`.
    static func read(_ model: SettingsViewModel, skippedOverride: (path: String, reason: String)? = nil) async {
        await model.connect(binary: previewBinary, version: "0.1.9", skippedOverride: skippedOverride)
        await model.readChildPath()
    }
}
#endif
