import BusinessJobs
import BusinessReads
import DataPreferences
import Foundation
import InfrastructureProcess
import Observation

/// Settings: General, Appearance, Backends & keys, Video & voice,
/// Permissions -- each over the verbs a person types, each change read back.
///
/// SwiftUI-free (ADR 0002). **A key never touches the app's state**: it is
/// written to `auth add`'s standard input and dropped -- no property, no
/// preference, no log holds it (the CLI's secrets law, kept here too).
@MainActor
@Observable
public final class SettingsViewModel {
    public enum Pane: String, CaseIterable, Identifiable, Sendable {
        case general
        case appearance
        case backends
        case media
        case permissions

        public var id: String { rawValue }

        public var title: String {
            switch self {
            case .general: "General"
            case .appearance: "Appearance"
            case .backends: "Backends & keys"
            case .media: "Video & voice"
            case .permissions: "Permissions"
            }
        }

        public var symbol: String {
            switch self {
            case .general: "gearshape"
            case .appearance: "paintpalette"
            case .backends: "key"
            case .media: "film"
            case .permissions: "hand.raised"
            }
        }
    }

    /// One row of `check --output-format json`.
    public struct CheckRow: Equatable, Sendable, Identifiable {
        public let section: String
        public let name: String
        /// `ok`, `warn`, `fail`, `skipped`.
        public let status: String
        public let detail: String
        public let remedy: String?
        public var id: String { section + "/" + name }
    }

    /// One provider as `providers scan` found it.
    public struct Provider: Equatable, Sendable, Identifiable {
        public let provider: String
        public let label: String
        public let tier: String
        /// What the scan says of its backend: `not registered` for one it
        /// would register, `backend: <names>` for one registered, empty for
        /// one it would not offer.
        public let backend: String
        /// The backend type it registers as: `claude-cli`, `ollama-cli`, ….
        public let type: String
        public var id: String { provider }

        public init(provider: String, label: String, tier: String, backend: String, type: String = "") {
            self.provider = provider
            self.label = label
            self.tier = tier
            self.backend = backend
            self.type = type
        }

        /// Whether the scan offers it for registering: detected, with no
        /// backend yet -- the CLI's own `not registered` (`providers_cmd.cpp`).
        public var registers: Bool { backend == "not registered" && !type.isEmpty }

        /// Whether a backend of its type runs only with a model named: the
        /// Ollama CLI's (the CLI's provider table, `needs_model`).
        public var needsModel: Bool { type == "ollama-cli" }

        /// What registering it runs, as a person types it: the entry the
        /// TUI's `r` writes -- named for the provider, of its type -- through
        /// the binary's own `config add-backend`, the model where its type
        /// needs one.
        public func registration(model: String = "") -> [String] {
            let model = model.trimmingCharacters(in: .whitespaces)
            return ["config", "add-backend", provider, "--type", type] + (model.isEmpty ? [] : ["--model", model])
        }
    }

    /// A backend's recorded consent for its vendor to run tools of its own
    /// (the CLI's 47a `server_tools`), as `config get` reads it (55g).
    public enum ServerToolsConsent: Equatable, Sendable {
        /// The tools consented to, in order; none when the list is empty.
        case consented([String])
        /// This apogee could not read it, in its own words -- a binary before
        /// the CLI's 55g has no such key.
        case unreadable(String)
    }

    public var pane: Pane = .general
    public private(set) var binary: String?
    public private(set) var version: String?
    public private(set) var backends: [ModelRow] = []
    /// The providers with a key stored (`auth list`): names only, never keys.
    public private(set) var keyed: Set<String> = []
    public private(set) var providers: [Provider] = []
    /// Each vendor backend's `server_tools`, by its name (GUI 4k): only an
    /// anthropic, openai or google entry holds one.
    public private(set) var serverTools: [String: ServerToolsConsent] = [:]
    /// The provider whose registering waits on the person's word.
    public var registering: Provider?
    public private(set) var checks: [CheckRow] = []
    public private(set) var checkSummary: String?
    /// What Health draws of `check`, in order: what fails, what to look at,
    /// then each row skipped with what brings it -- an optional ability not
    /// set up, its remedy the command (GUI 4f; the captions' `ffmpeg-full`
    /// among them, the CLI's 52e). A passing row, and a skipped one naming
    /// nothing to do, is not drawn.
    public var healthRows: [CheckRow] {
        checks.filter { $0.status == "fail" } + checks.filter { $0.status == "warn" }
            + checks.filter { $0.status == "skipped" && $0.remedy != nil }
    }
    /// Each tool's gate level, as `config get permissions` says it.
    public private(set) var permissions: [(tool: String, level: String)] = []
    public private(set) var pipelines: [String] = []
    /// Config values read through `config get`: absent is the default.
    public private(set) var webSearch: String?
    public private(set) var recall: String?
    public private(set) var working: String?
    /// A long fix running now: `check --fix`, `bootstrap`.
    public private(set) var fixing: JobProgress?
    public private(set) var failure: String?
    public var say: (@MainActor (String) -> Void)?
    /// Whether the build has the CLI's 52e: `config set` and `chats recall --forget`.
    public var hasTrack52: @MainActor () -> Bool = { false }
    /// Whether the binary has said what it can do -- its vocabulary read at
    /// launch, or a session's word (GUI 4k). Until it has, Settings asserts
    /// nothing of what it lacks.
    public var capabilitiesKnown: @MainActor () -> Bool = { false }
    /// Asks the binary again when its file changed since it was asked: run
    /// before every read, so the panes follow the binary that runs now.
    public var checkCapabilities: @MainActor () async -> Void = {}
    /// What recall holds, as `chats recall` says it, on a build with 52e;
    /// `nil` unread or on one without.
    public private(set) var recallHolds: String?
    /// Forgetting recall waits on the person's word, its holdings shown.
    public var forgetting = false

    public let theme: ThemeSettings
    private let runner: any CommandRunning
    private let starter: any JobStarting
    private let preferences: Preferences
    private var binaryURL: URL?

    public static let keyProviders = ["anthropic", "openai", "google"]

    /// A key provider's name as its company writes it; any other, as the CLI
    /// says it.
    public static func providerName(_ provider: String) -> String {
        ["anthropic": "Anthropic", "openai": "OpenAI", "google": "Google"][provider] ?? provider
    }
    public static let levels = ["ask", "allow", "deny"]

    public init(runner: any CommandRunning, starter: any JobStarting, preferences: Preferences) {
        self.runner = runner
        self.starter = starter
        self.preferences = preferences
        theme = ThemeSettings(preferences: preferences)
        binaryOverride = preferences.binaryOverride?.path
    }

    /// The binary this launch found, and -- when discovery passed the
    /// person's override over for it -- that override's path and why (4c).
    public func connect(binary: URL?, version: String?, skippedOverride: (path: String, reason: String)? = nil) async {
        binaryURL = binary
        self.binary = binary?.path
        self.version = version
        self.skippedOverride = skippedOverride
        await refresh()
    }

    // MARK: - General

    /// The binary the person points the app at; the next launch looks there first.
    public private(set) var binaryOverride: String?

    /// The override this launch's discovery passed over while another binary
    /// answered, and why: the app runs another binary than the one it was
    /// pointed at (4c). `nil` when the override was used or is unset.
    public private(set) var skippedOverride: (path: String, reason: String)?

    /// What General says of a passed-over override, beside the binary in use.
    public var skippedOverrideLine: String? {
        skippedOverride.map { skipped in
            let reason = skipped.reason.hasSuffix(".") ? String(skipped.reason.dropLast()) : skipped.reason
            return "Not using \(skipped.path): \(reason). This launch runs the one above."
        }
    }

    /// The override chosen since this launch looked -- neither the binary in
    /// use nor the one it passed over -- which the next launch tries first.
    public var pendingOverride: String? {
        guard let binaryOverride, binaryOverride != binary, binaryOverride != skippedOverride?.path else { return nil }
        return binaryOverride
    }

    public func setBinaryOverride(_ url: URL?) {
        preferences.binaryOverride = url
        binaryOverride = url?.path
        say?(url == nil ? "The app looks for apogee where it is installed, from its next launch" : "The app uses \(url!.path) from its next launch")
    }

    // MARK: - General: the children's PATH (M6)

    /// The `PATH` the app's sessions -- and every other child it starts -- run
    /// with, and where it came from: the person's login shell, or the
    /// fallback. Read only; `nil` until it is known.
    public private(set) var childPath: ShellPath.Resolution?

    /// Where it is learned: the composition hands in the app's resolver,
    /// whose answer waits at most for the shell's deadline.
    public var childPathSource: @MainActor () async -> ShellPath.Resolution? = { nil }

    /// Learns it -- at once once the shell has answered, else when it does.
    /// Apart from `refresh`, so no read of the binary waits on the shell.
    public func readChildPath() async {
        childPath = await childPathSource()
    }

    /// Its directories, in order; none until it is known.
    public var childPathDirectories: [String] { childPath?.directories ?? [] }

    /// Whether the person's login shell gave it.
    public var childPathFromShell: Bool { childPath?.source == .shell }

    /// Where it came from, in a sentence; `nil` until it is known.
    public var childPathOrigin: String? {
        guard let childPath else { return nil }
        switch childPath.source {
        case .shell:
            return "From your login shell, \(childPath.shell), as your terminal has it."
        case .fallback:
            let wellKnown = ShellPath.wellKnownDirectories
            let added = wellKnown.dropLast().joined(separator: ", ") + " and " + (wellKnown.last ?? "")
            return "Your login shell, \(childPath.shell), \(childPath.reason ?? "didn't answer"), so this is \(added), then the PATH the app started with."
        }
    }

    // MARK: - Reading

    /// Every pane's reads, again.
    public func refresh() async {
        guard let binary = binaryURL else {
            failure = "Apogee isn't installed, so there are no settings to read."
            return
        }
        failure = nil
        await checkCapabilities()
        if let list = await runner.read(binary: binary, ["models", "list"]).document {
            let rows = ModelRow.list(from: list)
            // The configured backends: a roster's models (`<backend>:<id>`,
            // "built in" or "roster") are a backend's choices, not entries.
            // Every entry whatever its MLX kind: the pane shows backends and
            // offers none to chat with (M4).
            backends = rows.filter {
                !$0.isStored && $0.format != "pipeline" && $0.format != "voice" && !Self.rosterSources.contains($0.source)
            }
            pipelines = rows.filter { $0.format == "pipeline" }.map(\.name)
        }
        await readServerTools(binary)
        if let auth = await runner.read(binary: binary, ["auth", "list"]).document {
            keyed = Set((auth.entries ?? []).compactMap { $0["provider"]?.stringValue })
        }
        if let scan = await runner.read(binary: binary, ["providers", "scan"]).document {
            providers = (scan.entries ?? []).compactMap { p in
                guard let id = p["provider"]?.stringValue else { return nil }
                return Provider(
                    provider: id, label: p["label"]?.stringValue ?? id, tier: p["tier"]?.stringValue ?? "",
                    backend: p["backend"]?.stringValue ?? "", type: p["type"]?.stringValue ?? "")
            }
        }
        await readCheck(binary)
        if case .done(let text) = await runner.command(binary: binary, ["config", "get", "permissions"]) {
            permissions = Self.levels(in: text)
        }
        webSearch = await value(binary, "tools.search.url")
        recall = await value(binary, "memory.recall")
        await readRecallHolds(binary)
    }

    /// The provenance `models list` gives a roster's rows.
    static let rosterSources: Set<String> = ["built in", "roster"]

    /// Each vendor backend's consent: `config get backends.<name>.server_tools`,
    /// one tool a line, none when unset (the CLI's 55g). A binary that
    /// refuses the key says so in its words, never read as "none".
    private func readServerTools(_ binary: URL) async {
        var read: [String: ServerToolsConsent] = [:]
        for row in backends where ServerTools.held(by: row.backendType) {
            switch await runner.command(binary: binary, ["config", "get", ServerTools.key(of: row.backend)]) {
            case .done(let text): read[row.backend] = .consented(ServerTools.list(text))
            case let other: read[row.backend] = .unreadable(other.failure ?? "it could not be read")
            }
        }
        serverTools = read
    }

    /// What a backend's row says of its vendor-run tools; `nil` for a type
    /// whose vendor runs none.
    public func serverToolsLine(_ row: ModelRow) -> String? {
        guard ServerTools.held(by: row.backendType) else { return nil }
        switch serverTools[row.backend] {
        case .consented(let tools)?:
            return tools.isEmpty
                ? "Vendor-run tools: none consented"
                : "Vendor-run tools consented: \(tools.joined(separator: ", ")) -- the vendor runs them under your key"
        case .unreadable(let reason)?:
            return "Vendor-run tools: this apogee can't read them -- \(reason)"
        case nil:
            return nil
        }
    }

    /// `chats recall` -- what recall holds, as the CLI says it -- on a build
    /// with 52e.
    private func readRecallHolds(_ binary: URL) async {
        guard hasTrack52() else {
            recallHolds = nil
            return
        }
        if case .done(let text) = await runner.command(binary: binary, ["chats", "recall"]) {
            recallHolds = text.split(whereSeparator: \.isNewline).last.map { $0.trimmingCharacters(in: .whitespaces) }
        } else {
            recallHolds = nil
        }
    }

    /// What Web search and Recall say under their values where the build
    /// can't set them here; `nil` where it can. Only a binary that has said
    /// what it can do is said to lack `config set` (GUI 4k).
    public var configSetHint: String? {
        if hasTrack52() { return nil }
        return capabilitiesKnown()
            ? "Read here, set in the config file: this apogee has no config set."
            : "Read here, set in the config file: this apogee hasn't said what it can do, so setting it here isn't offered."
    }

    private func readCheck(_ binary: URL) async {
        // check exits non-zero when a row fails; its document is what it printed.
        let outcome = await runner.report(binary: binary, ["check"])
        if let document = outcome.document {
            checks = (document.value["rows"]?.arrayValue ?? []).map { r in
                CheckRow(
                    section: r["section"]?.stringValue ?? "", name: r["name"]?.stringValue ?? "",
                    status: r["status"]?.stringValue ?? "", detail: r["detail"]?.stringValue ?? "",
                    remedy: r["remedy"]?.stringValue)
            }
            let failures = document.value["failures"]?.intValue ?? 0
            let warnings = document.value["warnings"]?.intValue ?? 0
            checkSummary = failures == 0 && warnings == 0 ? "Everything checks out" : "\(failures) failing, \(warnings) to look at"
        }
    }

    /// A config value, or `nil` when the config does not set it (the default).
    private func value(_ binary: URL, _ key: String) async -> String? {
        if case .done(let text) = await runner.command(binary: binary, ["config", "get", key]) {
            return text.trimmingCharacters(in: .whitespacesAndNewlines)
        }
        return nil
    }

    /// `write_file: ask` lines.
    static func levels(in text: String) -> [(tool: String, level: String)] {
        text.split(whereSeparator: \.isNewline).compactMap { line in
            let parts = line.split(separator: ":", maxSplits: 1).map { $0.trimmingCharacters(in: .whitespaces) }
            guard parts.count == 2, !parts[0].isEmpty else { return nil }
            return (parts[0], parts[1])
        }
    }

    // MARK: - Changing

    /// Stores a provider's key: `auth add <provider> --stdin`, the key on its
    /// standard input and nowhere else. The key is a parameter, used once and
    /// dropped: nothing here keeps it.
    public func addKey(_ key: String, for provider: String) async {
        guard let binary = binaryURL, Self.keyProviders.contains(provider) else { return }
        let trimmed = key.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        await change(
            binary, ["auth", "add", provider, "--stdin"], input: Data((trimmed + "\n").utf8),
            done: "\(provider) key saved. It won't be shown again.")
    }

    /// Removes a stored key: `auth clear <provider>`.
    public func removeKey(for provider: String) async {
        guard let binary = binaryURL else { return }
        await change(binary, ["auth", "clear", provider], done: "\(provider) key removed")
    }

    public func setPermission(_ tool: String, _ level: String) async {
        guard let binary = binaryURL else { return }
        await change(binary, ["config", "set-permission", tool, level], done: "\(tool): \(level)")
    }

    /// The allowed websites have no read in this build: the command's own
    /// words are the confirmation.
    public func allowHost(_ host: String) async {
        guard let binary = binaryURL, !host.trimmingCharacters(in: .whitespaces).isEmpty else { return }
        await change(binary, ["config", "add-allowed-host", host.trimmingCharacters(in: .whitespaces)], done: nil)
    }

    public func forbidHost(_ host: String) async {
        guard let binary = binaryURL, !host.trimmingCharacters(in: .whitespaces).isEmpty else { return }
        await change(binary, ["config", "delete-allowed-host", host.trimmingCharacters(in: .whitespaces)], done: nil)
    }

    /// `config set-default-video <pipeline>`, or `off`.
    public func setDefaultVideo(_ pipeline: String?) async {
        guard let binary = binaryURL else { return }
        await change(binary, ["config", "set-default-video", pipeline ?? "off"], done: nil)
    }

    /// A plain config key (52e's `config set`): the web search address, recall.
    public func setConfig(_ key: String, _ value: String) async {
        guard let binary = binaryURL, hasTrack52() else { return }
        await change(binary, ["config", "set", key, value], done: "\(key) is \(value)")
    }

    /// Asks before forgetting: what recall holds is read first, and shown.
    public func askToForget() async {
        guard let binary = binaryURL, hasTrack52() else { return }
        await readRecallHolds(binary)
        forgetting = true
    }

    /// Forgets every recalled summary once the person said yes -- 52e's
    /// `chats recall --forget --yes`: the CLI asks nothing on a pipe without
    /// `--yes`, and refuses (GUI 4k). Every transcript stays.
    public func forgetRecall() async {
        forgetting = false
        guard let binary = binaryURL, hasTrack52() else { return }
        await change(binary, ["chats", "recall", "--forget", "--yes"], done: nil)
    }

    /// Registers one scanned provider once the person said yes -- `config
    /// add-backend <provider> --type <type>`, the entry the TUI's `r` writes
    /// (the CLI has no verb registering one provider; `providers scan
    /// --register` registers every one) -- read back by the scan, the CLI's
    /// words said.
    public func register(_ provider: Provider, model: String = "") async {
        registering = nil
        guard let binary = binaryURL, provider.registers else { return }
        guard !provider.needsModel || !model.trimmingCharacters(in: .whitespaces).isEmpty else { return }
        await change(binary, provider.registration(model: model), done: nil)
    }

    private func change(_ binary: URL, _ arguments: [String], input: Data? = nil, done: String?) async {
        guard working == nil else { return }
        working = arguments.prefix(3).joined(separator: " ")
        let outcome = await runner.command(binary: binary, arguments, input: input)
        working = nil
        await refresh()
        if let failure = outcome.failure {
            self.failure = failure
            say?("That didn't work: \(failure)")
        } else if let done {
            say?(done)
        } else if case .done(let stdout) = outcome {
            say?(stdout.split(separator: "\n").last.map(String.init) ?? "Done")
        }
    }

    /// `check --fix` or `bootstrap`: what a remedy says to run, supervised.
    public func fix(_ arguments: [String]) {
        guard let binary = binaryURL, fixing == nil else { return }
        fixing = JobProgress()
        let job = starter.startJob(binary: binary, arguments: arguments, streaming: false)
        Task {
            for await update in job.updates { fixing?.apply(update) }
            let status = fixing?.status
            await refresh()
            if case .failed(let message)? = status { failure = message } else { fixing = nil }
            say?(status == .done ? "\(arguments.joined(separator: " ")) finished" : "\(arguments.joined(separator: " ")) stopped")
        }
    }

    public func dismissFailure() {
        failure = nil
    }
}

/// The Appearance setting: a theme of the design's three, or the system's,
/// and the text size -- kept in the app's preferences, applied by `styles`.
@MainActor
@Observable
public final class ThemeSettings {
    public enum Choice: String, CaseIterable, Identifiable, Sendable {
        case system
        case light
        case dark
        case contrast

        public var id: String { rawValue }

        public var title: String {
            switch self {
            case .system: "Follow the system"
            case .light: "Graphite Light"
            case .dark: "Graphite Dark"
            case .contrast: "High contrast"
            }
        }
    }

    public enum Size: String, CaseIterable, Identifiable, Sendable {
        case small
        case medium
        case large
        case extraLarge

        public var id: String { rawValue }

        public var title: String {
            switch self {
            case .small: "Small"
            case .medium: "Medium"
            case .large: "Large"
            case .extraLarge: "Extra large"
            }
        }

        /// tokens.json -> type.textSizes.
        public var factor: Double {
            switch self {
            case .small: 12.0 / 13.0
            case .medium: 1
            case .large: 14.0 / 13.0
            case .extraLarge: 15.0 / 13.0
            }
        }
    }

    public var choice: Choice {
        didSet { preferences.theme = choice.rawValue }
    }

    public var size: Size {
        didSet { preferences.textSize = size.rawValue }
    }

    private let preferences: Preferences

    public init(preferences: Preferences) {
        self.preferences = preferences
        choice = preferences.theme.flatMap(Choice.init(rawValue:)) ?? .system
        size = preferences.textSize.flatMap(Size.init(rawValue:)) ?? .medium
    }
}
