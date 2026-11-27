import Foundation
import InfrastructureProcess

/// Finds the `apogee` binary the app drives.
///
/// A `.app` inherits no shell `PATH` (the GUI spike's W3), so the binary is
/// looked for where the CLI's installs put it, on the channel the build drives
/// (GUI M18, `InstallChannel`), in order: the user's own override, then the
/// channel's places. The release channel looks at `~/.local/bin/apogee`
/// (`install.sh`'s and `make install`'s), then the dev and test channels beside
/// it (`apogee-dev`, `apogee-test`, `make install MODE=…`), as it always has;
/// the dev channel at `apogee-dev` alone, never the release binary. Each
/// candidate is verified by running it -- `apogee version` must answer as
/// Apogee does -- and one that fails is skipped and said. None found is a
/// stated condition, with the channel's install line, never a crash or a
/// silent retry.
public struct BinaryDiscovery: Sendable {
    private let spawner: any ChildSpawner
    private let home: URL
    private let timeout: Duration

    /// `home` is the user's home directory -- a test's own temporary one.
    public init(
        spawner: any ChildSpawner, home: URL = FileManager.default.homeDirectoryForCurrentUser,
        timeout: Duration = .seconds(10)
    ) {
        self.spawner = spawner
        self.home = home
        self.timeout = timeout
    }

    /// Where the binary is looked for on `channel`, first to last: the
    /// override, then the channel's places.
    public func candidates(override: URL?, channel: InstallChannel) -> [BinaryCandidate] {
        let bin = home.appendingPathComponent(".local/bin")
        let places: [BinaryCandidate] =
            switch channel {
            case .release:
                [
                    BinaryCandidate(url: bin.appendingPathComponent("apogee"), source: .release),
                    BinaryCandidate(url: bin.appendingPathComponent("apogee-dev"), source: .dev),
                    BinaryCandidate(url: bin.appendingPathComponent("apogee-test"), source: .test),
                ]
            case .dev:
                [BinaryCandidate(url: bin.appendingPathComponent("apogee-dev"), source: .dev)]
            }
        return (override.map { [BinaryCandidate(url: $0, source: .override)] } ?? []) + places
    }

    /// The first of `channel`'s candidates that answers as Apogee, and every
    /// one passed over; a place not on the channel is never run.
    public func locate(override: URL?, channel: InstallChannel) async -> DiscoveryOutcome {
        var skipped: [SkippedCandidate] = []
        for candidate in candidates(override: override, channel: channel) {
            switch await verify(candidate) {
            case .success(let found):
                return .found(found, skipped: skipped)
            case .failure(let reason):
                skipped.append(SkippedCandidate(candidate: candidate, reason: reason.text))
            }
        }
        return .notFound(skipped: skipped, channel: channel)
    }

    private struct Reason: Error {
        let text: String
    }

    private func verify(_ candidate: BinaryCandidate) async -> Result<FoundBinary, Reason> {
        let path = candidate.url.path
        guard FileManager.default.fileExists(atPath: path) else {
            return .failure(Reason(text: "not installed"))
        }
        guard FileManager.default.isExecutableFile(atPath: path) else {
            return .failure(Reason(text: "not executable"))
        }
        let run: CapturedRun
        do {
            run = try await spawner.capture(
                ChildCommand(executable: candidate.url, arguments: ["version"]), timeout: timeout,
                stdoutLimit: 64 * 1024)
        } catch {
            return .failure(Reason(text: "\(error)"))
        }
        if run.timedOut {
            return .failure(Reason(text: "did not answer `version` within \(timeout)"))
        }
        guard run.exit.succeeded else {
            let said = run.stderrTail.lastLine.map { ": \($0)" } ?? ""
            return .failure(Reason(text: "`version` \(run.exit)\(said)"))
        }
        let first = run.stdout.split(separator: "\n").first.map(String.init) ?? ""
        let words = first.split(separator: " ")
        guard words.count >= 2, words[0] == "apogee" else {
            return .failure(Reason(text: "`version` printed \"\(first)\", not Apogee's version line"))
        }
        return .success(
            FoundBinary(url: candidate.url, source: candidate.source, version: String(words[1]), versionLine: first))
    }
}

/// A place the binary is looked for.
public struct BinaryCandidate: Sendable, Equatable {
    public enum Source: String, Sendable, Equatable {
        /// The path the user set in the app.
        case override
        /// `~/.local/bin/apogee`: the installers' and `make install`'s.
        case release
        /// `~/.local/bin/apogee-dev`: `make install MODE=dev`.
        case dev
        /// `~/.local/bin/apogee-test`: `make install MODE=test`.
        case test
    }

    public let url: URL
    public let source: Source

    public init(url: URL, source: Source) {
        self.url = url
        self.source = source
    }
}

/// The binary found: where, from which place, and the version it reported.
public struct FoundBinary: Sendable, Equatable {
    public let url: URL
    public let source: BinaryCandidate.Source
    /// `0.1.9`.
    public let version: String
    /// The whole first line `version` printed.
    public let versionLine: String

    public init(url: URL, source: BinaryCandidate.Source, version: String, versionLine: String) {
        self.url = url
        self.source = source
        self.version = version
        self.versionLine = versionLine
    }
}

/// A candidate passed over, and why.
public struct SkippedCandidate: Sendable, Equatable {
    public let candidate: BinaryCandidate
    public let reason: String

    public init(candidate: BinaryCandidate, reason: String) {
        self.candidate = candidate
        self.reason = reason
    }
}

public enum DiscoveryOutcome: Sendable, Equatable {
    case found(FoundBinary, skipped: [SkippedCandidate])
    /// No candidate answered: Apogee is not installed where the app looks on
    /// `channel`.
    case notFound(skipped: [SkippedCandidate], channel: InstallChannel = .release)

    /// How to install the release CLI, said where no binary was found on the
    /// release channel.
    public static let installHint = InstallChannel.release.installHint

    /// How to install the binary none was found of -- the channel's own line
    /// -- or `nil` when one was found.
    public var installHint: String? {
        guard case .notFound(_, let channel) = self else { return nil }
        return channel.installHint
    }

    public var skipped: [SkippedCandidate] {
        switch self {
        case .found(_, let skipped), .notFound(let skipped, _): skipped
        }
    }

    /// The person's override, passed over with its reason while another
    /// binary answered: the app runs a binary other than the one it was
    /// pointed at, which is to be said (GUI 4c). `nil` when there is none,
    /// when it is the one found, or when none was found -- where every place
    /// passed over is said already.
    public var skippedOverride: SkippedCandidate? {
        guard case .found(_, let skipped) = self else { return nil }
        return skipped.first { $0.candidate.source == .override }
    }
}
