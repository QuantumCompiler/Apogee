import DataDiscovery
import Foundation
import InfrastructureProcess
import Testing

/// A home directory of its own, with fake `apogee` binaries put where the
/// install channels put the real one.
final class FakeHome {
    let url: URL

    init() throws {
        url = FileManager.default.temporaryDirectory.appendingPathComponent("apogee-discovery-\(UUID().uuidString)")
        try FileManager.default.createDirectory(
            at: url.appendingPathComponent(".local/bin"), withIntermediateDirectories: true)
    }

    deinit { try? FileManager.default.removeItem(at: url) }

    /// A shell script at `relativePath`, executable unless said otherwise.
    @discardableResult
    func binary(_ relativePath: String, _ script: String, executable: Bool = true) throws -> URL {
        let file = url.appendingPathComponent(relativePath)
        try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        try ("#!/bin/sh\n" + script + "\n").write(to: file, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes(
            [.posixPermissions: executable ? 0o755 : 0o644], ofItemAtPath: file.path)
        return file
    }

    static let apogee = #"echo "apogee 0.1.9 (7072bdf, 2026-10-19, macos-arm64)"; echo "channel: release""#
    static let apogeeDev = #"echo "apogee 0.2.1 (7ff12a0d, 2026-11-21, macos-arm64)"; echo "channel: dev""#
}

@Suite("BinaryDiscovery", .timeLimit(.minutes(1)))
struct BinaryDiscoveryTests {
    func discovery(_ home: FakeHome, timeout: Duration = .seconds(10)) -> BinaryDiscovery {
        BinaryDiscovery(spawner: ProcessSpawner(), home: home.url, timeout: timeout)
    }

    @Test("the release channel's places, in order: the override, then the release, dev and test channels")
    func candidates() throws {
        let home = try FakeHome()
        let override = URL(fileURLWithPath: "/opt/tools/apogee")
        let candidates = discovery(home).candidates(override: override, channel: .release)
        #expect(candidates.map(\.source) == [.override, .release, .dev, .test])
        #expect(candidates.map { $0.url.lastPathComponent } == ["apogee", "apogee", "apogee-dev", "apogee-test"])
        #expect(candidates[1].url.path.hasSuffix("/.local/bin/apogee"))
        #expect(discovery(home).candidates(override: nil, channel: .release).map(\.source) == [.release, .dev, .test])
    }

    @Test("the dev channel's places: the override, then apogee-dev alone -- never the release binary, no test fallback")
    func devCandidates() throws {
        let home = try FakeHome()
        let override = URL(fileURLWithPath: "/opt/tools/apogee")
        let candidates = discovery(home).candidates(override: override, channel: .dev)
        #expect(candidates.map(\.source) == [.override, .dev])
        #expect(candidates.map(\.url.path) == ["/opt/tools/apogee", home.url.path + "/.local/bin/apogee-dev"])
        #expect(discovery(home).candidates(override: nil, channel: .dev).map(\.source) == [.dev])
    }

    @Test("with both CLIs installed, the release channel drives the release binary, as before channels")
    func releaseWithBothInstalled() async throws {
        let home = try FakeHome()
        let release = try home.binary(".local/bin/apogee", FakeHome.apogee)
        try home.binary(".local/bin/apogee-dev", FakeHome.apogeeDev)
        guard case .found(let found, let skipped) = await discovery(home).locate(override: nil, channel: .release) else {
            Issue.record("not found")
            return
        }
        #expect(found.url == release)
        #expect(found.source == .release)
        #expect(skipped.isEmpty)
    }

    @Test("with both CLIs installed, the dev channel drives apogee-dev and never runs the release binary")
    func devWithBothInstalled() async throws {
        let home = try FakeHome()
        let ran = home.url.appendingPathComponent("release-ran")
        try home.binary(".local/bin/apogee", "touch '\(ran.path)'; " + FakeHome.apogee)
        let dev = try home.binary(".local/bin/apogee-dev", FakeHome.apogeeDev)
        guard case .found(let found, let skipped) = await discovery(home).locate(override: nil, channel: .dev) else {
            Issue.record("not found")
            return
        }
        #expect(found.url == dev)
        #expect(found.source == .dev)
        #expect(found.version == "0.2.1")
        #expect(skipped.isEmpty)
        #expect(!FileManager.default.fileExists(atPath: ran.path), "the release binary was run")
    }

    @Test("the dev channel honours the person's override, ahead of apogee-dev")
    func devOverride() async throws {
        let home = try FakeHome()
        try home.binary(".local/bin/apogee-dev", FakeHome.apogeeDev)
        let mine = try home.binary("tools/apogee", #"echo "apogee 0.2.2 (mine)""#)
        guard case .found(let found, _) = await discovery(home).locate(override: mine, channel: .dev) else {
            Issue.record("not found")
            return
        }
        #expect(found.source == .override)
        #expect(found.version == "0.2.2")
    }

    @Test("apogee-dev absent: the dev channel finds nothing -- the release binary and apogee-test passed over unrun -- and names the dev install line")
    func devNotFound() async throws {
        let home = try FakeHome()
        let ran = home.url.appendingPathComponent("something-ran")
        try home.binary(".local/bin/apogee", "touch '\(ran.path)'; " + FakeHome.apogee)
        try home.binary(".local/bin/apogee-test", "touch '\(ran.path)'; " + FakeHome.apogee)
        let outcome = await discovery(home).locate(override: nil, channel: .dev)
        guard case .notFound(let skipped, let channel) = outcome else {
            Issue.record("found something: \(outcome)")
            return
        }
        #expect(channel == .dev)
        #expect(skipped.map(\.candidate.source) == [.dev])
        #expect(skipped.map(\.reason) == ["not installed"])
        #expect(!FileManager.default.fileExists(atPath: ran.path), "a binary of another channel was run")
        #expect(outcome.installHint == "Install the dev channel with: make -C lib/src/cli install MODE=dev")
        #expect(InstallChannel.dev.installHint.contains("make -C lib/src/cli install MODE=dev"))
    }

    @Test("a bundle's ApogeeChannel names the channel: dev is the dev channel, anything else or nothing the release")
    func channelFromTheBundle() {
        #expect(InstallChannel(bundleValue: "dev") == .dev)
        #expect(InstallChannel(bundleValue: "release") == .release)
        #expect(InstallChannel(bundleValue: nil) == .release)
        #expect(InstallChannel(bundleValue: "") == .release)
        #expect(InstallChannel(bundleValue: "test") == .release)
        #expect(InstallChannel(bundleValue: 1) == .release)
        #expect(InstallChannel.allCases.map(\.rawValue) == ["release", "dev"])
    }

    @Test("the release channel's binary is found, verified by its version line")
    func release() async throws {
        let home = try FakeHome()
        let release = try home.binary(".local/bin/apogee", FakeHome.apogee)
        let outcome = await discovery(home).locate(override: nil, channel: .release)
        guard case .found(let found, let skipped) = outcome else {
            Issue.record("not found: \(outcome)")
            return
        }
        #expect(found.url == release)
        #expect(found.source == .release)
        #expect(found.version == "0.1.9")
        #expect(found.versionLine == "apogee 0.1.9 (7072bdf, 2026-10-19, macos-arm64)")
        #expect(skipped.isEmpty)
    }

    @Test("the override comes first, ahead of an installed release")
    func overrideFirst() async throws {
        let home = try FakeHome()
        try home.binary(".local/bin/apogee", FakeHome.apogee)
        let mine = try home.binary("tools/apogee", #"echo "apogee 0.2.0 (dev)""#)
        guard case .found(let found, _) = await discovery(home).locate(override: mine, channel: .release) else {
            Issue.record("not found")
            return
        }
        #expect(found.source == .override)
        #expect(found.version == "0.2.0")
    }

    @Test("a candidate that fails verification is skipped and said, and the next one found")
    func skippedAndSaid() async throws {
        let home = try FakeHome()
        try home.binary(".local/bin/apogee", "echo hello")
        try home.binary(".local/bin/apogee-dev", "echo 'error: broken install' >&2; exit 1")
        try home.binary(".local/bin/apogee-test", FakeHome.apogee)
        let missing = home.url.appendingPathComponent("nowhere/apogee")
        guard case .found(let found, let skipped) = await discovery(home).locate(override: missing, channel: .release) else {
            Issue.record("not found")
            return
        }
        #expect(found.source == .test)
        #expect(skipped.map(\.candidate.source) == [.override, .release, .dev])
        #expect(skipped.map(\.reason) == [
            "not installed",
            "`version` printed \"hello\", not Apogee's version line",
            "`version` exited with status 1: error: broken install",
        ])
    }

    @Test("a file that is not executable, or a binary that does not answer, is skipped")
    func notExecutableOrSilent() async throws {
        let home = try FakeHome()
        try home.binary(".local/bin/apogee", FakeHome.apogee, executable: false)
        try home.binary(".local/bin/apogee-dev", "sleep 30")
        let outcome = await discovery(home, timeout: .milliseconds(300)).locate(override: nil, channel: .release)
        #expect(outcome.skipped.map(\.reason).prefix(2) == ["not executable", "did not answer `version` within 0.3 seconds"])
    }

    @Test("none found is the typed condition, every place said, with the install hint")
    func notFound() async throws {
        let home = try FakeHome()
        let outcome = await discovery(home).locate(override: nil, channel: .release)
        guard case .notFound(let skipped, let channel) = outcome else {
            Issue.record("found something in an empty home: \(outcome)")
            return
        }
        #expect(channel == .release)
        #expect(skipped.map(\.candidate.source) == [.release, .dev, .test])
        #expect(skipped.allSatisfy { $0.reason == "not installed" })
        #expect(DiscoveryOutcome.installHint.contains("lib/scripts/install.sh"))
        #expect(outcome.installHint == DiscoveryOutcome.installHint)
        #expect(InstallChannel.release.installHint == DiscoveryOutcome.installHint)
    }
}
