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
}

@Suite("BinaryDiscovery", .timeLimit(.minutes(1)))
struct BinaryDiscoveryTests {
    func discovery(_ home: FakeHome, timeout: Duration = .seconds(10)) -> BinaryDiscovery {
        BinaryDiscovery(spawner: ProcessSpawner(), home: home.url, timeout: timeout)
    }

    @Test("the places looked, in order: the override, then the release, dev and test channels")
    func candidates() throws {
        let home = try FakeHome()
        let override = URL(fileURLWithPath: "/opt/tools/apogee")
        let candidates = discovery(home).candidates(override: override)
        #expect(candidates.map(\.source) == [.override, .release, .dev, .test])
        #expect(candidates.map { $0.url.lastPathComponent } == ["apogee", "apogee", "apogee-dev", "apogee-test"])
        #expect(candidates[1].url.path.hasSuffix("/.local/bin/apogee"))
        #expect(discovery(home).candidates(override: nil).map(\.source) == [.release, .dev, .test])
    }

    @Test("the release channel's binary is found, verified by its version line")
    func release() async throws {
        let home = try FakeHome()
        let release = try home.binary(".local/bin/apogee", FakeHome.apogee)
        let outcome = await discovery(home).locate(override: nil)
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
        guard case .found(let found, _) = await discovery(home).locate(override: mine) else {
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
        guard case .found(let found, let skipped) = await discovery(home).locate(override: missing) else {
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
        let outcome = await discovery(home, timeout: .milliseconds(300)).locate(override: nil)
        #expect(outcome.skipped.map(\.reason).prefix(2) == ["not executable", "did not answer `version` within 0.3 seconds"])
    }

    @Test("none found is the typed condition, every place said, with the install hint")
    func notFound() async throws {
        let home = try FakeHome()
        let outcome = await discovery(home).locate(override: nil)
        guard case .notFound(let skipped) = outcome else {
            Issue.record("found something in an empty home: \(outcome)")
            return
        }
        #expect(skipped.map(\.candidate.source) == [.release, .dev, .test])
        #expect(skipped.allSatisfy { $0.reason == "not installed" })
        #expect(DiscoveryOutcome.installHint.contains("lib/scripts/install.sh"))
    }
}
