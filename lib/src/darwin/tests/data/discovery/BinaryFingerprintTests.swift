import DataDiscovery
import Foundation
import Testing

/// A binary's file told apart from another build put where it stood (GUI 4k),
/// over files of the test's own in a temporary folder.
@Suite("BinaryFingerprint (GUI 4k)")
struct BinaryFingerprintTests {
    @Test("the same file reads the same; another written in its place reads otherwise; a link reads as its file; none, nil")
    func fingerprint() throws {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("apogee-fingerprint-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: folder) }
        let binary = folder.appendingPathComponent("apogee")
        try Data("#!/bin/sh\necho one\n".utf8).write(to: binary)
        let first = try #require(BinaryFingerprint(of: binary))
        #expect(BinaryFingerprint(of: binary) == first)
        let link = folder.appendingPathComponent("apogee-link")
        try FileManager.default.createSymbolicLink(at: link, withDestinationURL: binary)
        #expect(BinaryFingerprint(of: link) == first, "a link is its file")
        let rebuilt = folder.appendingPathComponent("apogee.new")
        try Data("#!/bin/sh\necho two, longer\n".utf8).write(to: rebuilt)
        _ = try FileManager.default.replaceItemAt(binary, withItemAt: rebuilt)
        #expect(BinaryFingerprint(of: binary) != first, "another build installed where it stood")
        #expect(BinaryFingerprint(of: folder.appendingPathComponent("absent")) == nil)
    }
}
