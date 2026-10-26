import Foundation

/// A binary's identity on disk -- the file a path resolves to, its size and
/// when it was last written -- so the app notices another build put where the
/// one it found stood (an install or a `make install` while it runs) and asks
/// that build what it can do (GUI 4k). Read from the file's attributes alone:
/// nothing is run.
public struct BinaryFingerprint: Sendable, Equatable {
    /// The file number of the file the path resolves to, past any link.
    public let fileNumber: UInt64
    public let size: UInt64
    public let modified: Date

    public init(fileNumber: UInt64, size: UInt64, modified: Date) {
        self.fileNumber = fileNumber
        self.size = size
        self.modified = modified
    }

    /// The fingerprint of the file at `url`, a link followed to its file;
    /// `nil` when there is none to read.
    public init?(of url: URL) {
        let path = url.resolvingSymlinksInPath().path
        guard let attributes = try? FileManager.default.attributesOfItem(atPath: path),
            let number = (attributes[.systemFileNumber] as? NSNumber)?.uint64Value,
            let size = (attributes[.size] as? NSNumber)?.uint64Value,
            let modified = attributes[.modificationDate] as? Date
        else { return nil }
        self.init(fileNumber: number, size: size, modified: modified)
    }
}
