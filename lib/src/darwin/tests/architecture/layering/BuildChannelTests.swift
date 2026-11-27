import Foundation
import Testing

/// The app's directory, lib/src/darwin: this file is
/// tests/architecture/layering/BuildChannelTests.swift.
private let appRoot = URL(fileURLWithPath: #filePath)
    .deletingLastPathComponent().deletingLastPathComponent()
    .deletingLastPathComponent().deletingLastPathComponent()

/// `scripts/build.sh` with `arguments`, its output and errors together. Every
/// call runs build.sh copied into a temporary app directory, so a refusal that
/// regresses cleans or builds there and never acts on lib/src/darwin/build,
/// where the suite itself runs.
private func buildScript(_ arguments: [String], in app: URL) throws -> (status: Int32, said: String) {
    let process = Process()
    let output = Pipe()
    process.executableURL = URL(fileURLWithPath: "/bin/bash")
    process.arguments = [app.appendingPathComponent("scripts/build.sh").path] + arguments
    process.environment = ["PATH": "/usr/bin:/bin"]
    process.standardOutput = output
    process.standardError = output
    try process.run()
    let said = String(decoding: output.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
    process.waitUntilExit()
    return (process.terminationStatus, said)
}

@Suite("build.sh's channel, refused before it builds (GUI M18)")
struct BuildChannelTests {
    /// An app directory holding a copy of build.sh alone, with no project: a
    /// refusal that regresses cleans or builds this, never the tree the suite runs from.
    private func plantedScript() throws -> URL {
        let app = FileManager.default.temporaryDirectory.appendingPathComponent("apogee-build-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: app.appendingPathComponent("scripts"), withIntermediateDirectories: true)
        try FileManager.default.copyItem(
            at: appRoot.appendingPathComponent("scripts/build.sh"), to: app.appendingPathComponent("scripts/build.sh"))
        return app
    }

    @Test("a channel the app has not is refused, naming the two")
    func unknownChannel() throws {
        let app = try plantedScript()
        defer { try? FileManager.default.removeItem(at: app) }
        let result = try buildScript(["build", "--channel", "test"], in: app)
        #expect(result.status != 0)
        #expect(result.said.contains("unknown channel 'test' (release, dev)"), "\(result.said)")
    }

    @Test("--channel with no value is refused")
    func noValue() throws {
        let app = try plantedScript()
        defer { try? FileManager.default.removeItem(at: app) }
        let result = try buildScript(["open", "--channel"], in: app)
        #expect(result.status != 0)
        #expect(result.said.contains("--channel needs release or dev"), "\(result.said)")
    }

    @Test("package packages the release channel alone: a dev build is never zipped as the release's asset")
    func packageIsRelease() throws {
        let app = try plantedScript()
        defer { try? FileManager.default.removeItem(at: app) }
        let result = try buildScript(["package", "--channel", "dev"], in: app)
        #expect(result.status != 0)
        #expect(result.said.contains("package makes what a release carries, the release channel"), "\(result.said)")
    }

    /// A copy of build.sh (`plantedScript`) beside a built `Apogee.app` whose
    /// Info.plist is `plist`, for `package` to read back.
    private func plantedBuild(_ plist: [String: String]) throws -> URL {
        let app = try plantedScript()
        let contents = app.appendingPathComponent("build/macos-arm64/Apogee.app/Contents")
        try FileManager.default.createDirectory(at: contents, withIntermediateDirectories: true)
        try PropertyListSerialization.data(fromPropertyList: plist, format: .xml, options: 0)
            .write(to: contents.appendingPathComponent("Info.plist"))
        return app
    }

    private let release = [
        "CFBundleIdentifier": "com.quantumcompiler.apogee", "CFBundleName": "Apogee", "ApogeeChannel": "release",
    ]

    @Test("a built release app whose identifier moved is refused before its signature is asked")
    func identifierMoved() throws {
        var plist = release
        plist["CFBundleIdentifier"] = "com.quantumcompiler.apogee.dev"
        let app = try plantedBuild(plist)
        defer { try? FileManager.default.removeItem(at: app) }
        let result = try buildScript(["package"], in: app)
        #expect(result.status != 0)
        #expect(
            result.said.contains(
                "Apogee.app's CFBundleIdentifier is com.quantumcompiler.apogee.dev: the release channel's is com.quantumcompiler.apogee"),
            "\(result.said)")
    }

    @Test("a built release app that names another channel, or a name of its own, is refused", arguments: [
        ("ApogeeChannel", "dev", "Apogee.app's ApogeeChannel is dev: the release channel's is release"),
        ("CFBundleName", "Apogee Dev", "Apogee.app's CFBundleName is Apogee Dev: the release channel's is Apogee"),
    ])
    func identityRefused(key: String, value: String, said: String) throws {
        var plist = release
        plist[key] = value
        let app = try plantedBuild(plist)
        defer { try? FileManager.default.removeItem(at: app) }
        let result = try buildScript(["package"], in: app)
        #expect(result.status != 0)
        #expect(result.said.contains(said), "\(result.said)")
    }

    @Test("a release app naming its identity goes on past the read-back, to its signature")
    func identityHolds() throws {
        let app = try plantedBuild(release)
        defer { try? FileManager.default.removeItem(at: app) }
        let result = try buildScript(["package"], in: app)
        #expect(result.status != 0, "a planted bundle has no signature")
        #expect(result.said.contains("the signature does not verify"), "\(result.said)")
        #expect(!result.said.contains("channel's is"), "\(result.said)")
    }

    @Test("the suite and clean take no channel: the suite's bundles are hosted by nothing", arguments: ["test", "clean"])
    func noChannelForTestOrClean(command: String) throws {
        let app = try plantedScript()
        defer { try? FileManager.default.removeItem(at: app) }
        let result = try buildScript([command, "--channel", "dev"], in: app)
        #expect(result.status != 0)
        #expect(result.said.contains("--channel is for build, open and package"), "\(result.said)")
    }
}
