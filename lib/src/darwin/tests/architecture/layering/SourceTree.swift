import Foundation

/// The files the check reads, by path relative to the package root -- the
/// real tree, or a planted one in a test.
protocol SourceTree {
    /// Every `.swift` file under `directory`, recursively, sorted.
    func swiftFiles(under directory: String) -> [String]
    func contents(of path: String) -> String?
    func isDirectory(_ path: String) -> Bool
}

/// The package on disk.
struct DiskTree: SourceTree {
    let root: URL

    func swiftFiles(under directory: String) -> [String] {
        let base = root.appendingPathComponent(directory)
        guard
            let walker = FileManager.default.enumerator(
                at: base, includingPropertiesForKeys: nil)
        else { return [] }
        var found: [String] = []
        for case let url as URL in walker where url.pathExtension == "swift" {
            let relative = url.standardizedFileURL.path.dropFirst(
                root.standardizedFileURL.path.count + 1)
            found.append(String(relative))
        }
        return found.sorted()
    }

    func contents(of path: String) -> String? {
        try? String(contentsOf: root.appendingPathComponent(path), encoding: .utf8)
    }

    func isDirectory(_ path: String) -> Bool {
        var isDirectory: ObjCBool = false
        return FileManager.default.fileExists(
            atPath: root.appendingPathComponent(path).path, isDirectory: &isDirectory)
            && isDirectory.boolValue
    }
}

/// A tree of planted files, for the tests that plant a violation.
struct PlantedTree: SourceTree {
    /// Path -> contents.
    var files: [String: String]

    func swiftFiles(under directory: String) -> [String] {
        files.keys.filter { $0.hasPrefix(directory + "/") && $0.hasSuffix(".swift") }.sorted()
    }

    func contents(of path: String) -> String? { files[path] }

    func isDirectory(_ path: String) -> Bool {
        files.keys.contains { $0.hasPrefix(path + "/") }
    }
}
