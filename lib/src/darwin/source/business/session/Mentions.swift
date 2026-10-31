import Foundation

/// What `@` offers as a person types a path: the entries of the folder the
/// typed part names, in the session's working folder, that begin with its
/// last part. A `user` line's `@path` is the CLI's to attach; this only helps
/// type it.
public enum Mentions {
    /// At most `limit` completions of `typed` (the text after `@`), folders
    /// with a trailing `/`, sorted, hidden entries only when asked for.
    public static func candidates(for typed: String, in folder: URL, limit: Int = 12) -> [String] {
        let slash = typed.lastIndex(of: "/")
        let directory = slash.map { String(typed[...$0]) } ?? ""
        let stem = slash.map { String(typed[typed.index(after: $0)...]) } ?? typed
        let base = directory.hasPrefix("/") ? URL(fileURLWithPath: directory) : folder.appendingPathComponent(directory)
        guard
            let names = try? FileManager.default.contentsOfDirectory(
                at: base, includingPropertiesForKeys: [.isDirectoryKey], options: [])
        else { return [] }
        return names.compactMap { url -> String? in
            let name = url.lastPathComponent
            guard name.lowercased().hasPrefix(stem.lowercased()) else { return nil }
            guard stem.hasPrefix(".") || !name.hasPrefix(".") else { return nil }
            let isFolder = (try? url.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) ?? false
            return directory + name + (isFolder ? "/" : "")
        }
        .sorted()
        .prefix(limit)
        .map { $0 }
    }

    /// The `@` mention being typed at the end of `draft` -- its text after the
    /// `@` -- or `nil` when the last word is not one.
    public static func typing(in draft: String) -> String? {
        guard let at = draft.lastIndex(of: "@") else { return nil }
        if at != draft.startIndex {
            let before = draft[draft.index(before: at)]
            guard before == " " || before == "\n" else { return nil }
        }
        let typed = draft[draft.index(after: at)...]
        return typed.contains(where: { $0 == " " || $0 == "\n" }) ? nil : String(typed)
    }
}
