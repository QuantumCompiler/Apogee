import Foundation

/// A runtime install the person said yes to -- `train setup --with video`,
/// the command a media verb's refusal named -- as it runs and once it has
/// ended, folded from its job's updates by `JobProgress` and nothing else
/// (GUI 4f): what a screen draws of it, a function of what it said.
///
/// `train setup` speaks prose, never the 52b stream: `installing the 'video'
/// set: torch>=2.6 diffusers>=0.35 … ...` while pip runs, `installed` at its
/// end, and on a failure stderr's `apogee train: pip install for the 'video'
/// set failed (exit code 1):` with pip's own last line under it.
public struct RuntimeInstall: Sendable, Equatable {
    /// How it stands.
    public enum Phase: Sendable, Equatable {
        case running
        /// Ended with the set installed.
        case installed
        /// Stopped by its Stop before it finished.
        case stopped
        /// It failed: `failure` says why.
        case failed
    }

    /// What it runs: `train setup --with video`.
    public let arguments: [String]
    public private(set) var progress = JobProgress()

    public init(arguments: [String]) {
        self.arguments = arguments
    }

    public mutating func apply(_ update: JobUpdate) {
        progress.apply(update)
    }

    public var phase: Phase {
        switch progress.status {
        case .running: .running
        case .done: .installed
        case .stopped: .stopped
        case .failed: .failed
        }
    }

    /// The command as a person would type it: `apogee train setup --with video`.
    public var command: String { (["apogee"] + arguments).joined(separator: " ") }

    /// The set it installs, as the CLI names it: `video`.
    public var set: String {
        arguments.firstIndex(of: "--with").flatMap { arguments.indices.contains($0 + 1) ? arguments[$0 + 1] : nil } ?? "runtime"
    }

    /// The card's headline, as the install stands.
    public var headline: String {
        switch phase {
        case .running: "Installing the \(set) set"
        case .installed: "Installed the \(set) set"
        case .stopped: "The \(set) set's install was stopped before it finished"
        case .failed: "The \(set) set did not install"
        }
    }

    /// What it says now: the line it last printed (or stderr's last, the busy
    /// line) while it runs; its last line once installed; why, once it failed.
    public var detail: String? {
        switch phase {
        case .failed: failure
        case .running: progress.busy ?? progress.notices.last
        case .installed, .stopped: progress.notices.last
        }
    }

    /// Why it failed, in the CLI's words: stderr's own `apogee …` line, and
    /// the line under it last (pip's), when they differ; `nil` unless it failed.
    public var failure: String? {
        guard case .failed(let last) = progress.status else { return nil }
        let lines = progress.stderr.split(whereSeparator: \.isNewline).map { $0.trimmingCharacters(in: .whitespaces) }
            .filter { !$0.isEmpty }
        guard let own = lines.last(where: { $0.hasPrefix("apogee ") }) else { return last }
        let tail = lines.last ?? own
        return own == tail ? own : own + "\n" + tail
    }
}
