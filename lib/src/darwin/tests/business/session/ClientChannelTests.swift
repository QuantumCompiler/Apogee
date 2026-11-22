import BusinessSession
import DataDiscovery
import DataPreferences
import Foundation
import InfrastructureProcess
import Testing

/// A home of its own with both CLIs put where their channels install them,
/// each a script answering `version` as its channel's binary does.
private final class BothChannelsHome {
    let url: URL

    init() throws {
        url = FileManager.default.temporaryDirectory.appendingPathComponent("apogee-channels-\(UUID().uuidString)")
        let bin = url.appendingPathComponent(".local/bin")
        try FileManager.default.createDirectory(at: bin, withIntermediateDirectories: true)
        for (name, line) in [
            ("apogee", "apogee 0.2.0 (b54b25e9, 2026-10-31, macos-arm64)"),
            ("apogee-dev", "apogee 0.2.1 (7ff12a0d, 2026-11-21, macos-arm64)"),
        ] {
            let file = bin.appendingPathComponent(name)
            try "#!/bin/sh\necho \"\(line)\"\n".write(to: file, atomically: true, encoding: .utf8)
            try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: file.path)
        }
    }

    deinit { try? FileManager.default.removeItem(at: url) }

    func client(_ channel: InstallChannel?) -> ApogeeClient {
        let identity = ClientIdentity(version: "test")
        let preferences = Preferences(store: MemoryStore())
        guard let channel else {
            return ApogeeClient(identity: identity, spawner: ProcessSpawner(), preferences: preferences, home: url)
        }
        return ApogeeClient(
            identity: identity, spawner: ProcessSpawner(), preferences: preferences, home: url, children: nil,
            channel: channel)
    }
}

@Suite("The client's channel (GUI M18)", .timeLimit(.minutes(1)))
struct ClientChannelTests {
    @Test("a dev-channel client locates apogee-dev with both CLIs installed")
    func devClient() async throws {
        let home = try BothChannelsHome()
        let client = home.client(.dev)
        #expect(client.channel == .dev)
        guard case .found(let found, _) = await client.locateBinary() else {
            Issue.record("not found")
            return
        }
        #expect(found.url.lastPathComponent == "apogee-dev")
        #expect(found.version == "0.2.1")
    }

    @Test("a release-channel client locates the release binary, and is what a client named no channel is")
    func releaseClient() async throws {
        let home = try BothChannelsHome()
        for client in [home.client(.release), home.client(nil)] {
            #expect(client.channel == .release)
            guard case .found(let found, _) = await client.locateBinary() else {
                Issue.record("not found")
                return
            }
            #expect(found.url.lastPathComponent == "apogee")
            #expect(found.version == "0.2.0")
        }
    }

    @Test("the dev client's override is its own preferences': taken ahead of apogee-dev")
    func devOverride() async throws {
        let home = try BothChannelsHome()
        let preferences = Preferences(store: MemoryStore())
        preferences.binaryOverride = home.url.appendingPathComponent(".local/bin/apogee")
        let client = ApogeeClient(
            identity: ClientIdentity(version: "test"), spawner: ProcessSpawner(), preferences: preferences,
            home: home.url, children: nil, channel: .dev)
        guard case .found(let found, _) = await client.locateBinary() else {
            Issue.record("not found")
            return
        }
        #expect(found.source == .override)
    }
}
