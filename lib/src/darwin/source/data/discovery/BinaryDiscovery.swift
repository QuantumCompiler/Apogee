import Foundation
import InfrastructureProcess

/// Finds the `apogee` binary the app drives.
///
/// A `.app` inherits no shell `PATH` (the GUI spike's W3), so the binary is
/// looked for where the CLI's installs put it, in order: the user's own
/// override, the release channel (`~/.local/bin/apogee`, `install.sh`'s and
/// `make install`'s), then the dev and test channels beside it
/// (`apogee-dev`, `apogee-test`, `make install MODE=…`). Each candidate is
/// verified by running it -- `apogee version` must answer as Apogee does --
/// and one that fails is skipped and said. None found is a stated condition,
/// never a crash or a silent retry.
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

    /// Where the binary is looked for, first to last.
    public func candidates(override: URL?) -> [BinaryCandidate] {
        let bin = home.appendingPathComponent(".local/bin")
        return (override.map { [BinaryCandidate(url: $0, source: .override)] } ?? []) + [
            BinaryCandidate(url: bin.appendingPathComponent("apogee"), source: .release),
            BinaryCandidate(url: bin.appendingPathComponent("apogee-dev"), source: .dev),
            BinaryCandidate(url: bin.appendingPathComponent("apogee-test"), source: .test),
        ]
    }

    /// The first candidate that answers as Apogee, and every one passed over.
    public func locate(override: URL?) async -> DiscoveryOutcome {
        var skipped: [SkippedCandidate] = []
        for candidate in candidates(override: override) {
            switch await verify(candidate) {
            case .success(let found):
                return .found(found, skipped: skipped)
            case .failure(let reason):
                skipped.append(SkippedCandidate(candidate: candidate, reason: reason.text))
            }
        }
        return .notFound(skipped: skipped)
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
    /// No candidate answered: Apogee is not installed where the app looks.
    case notFound(skipped: [SkippedCandidate])

    /// How to install the CLI, said where no binary was found.
    public static let installHint =
        "Install Apogee with: curl -fsSL https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.sh | bash"

    public var skipped: [SkippedCandidate] {
        switch self {
        case .found(_, let skipped), .notFound(let skipped): skipped
        }
    }
}
