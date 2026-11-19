import CryptoKit
import Foundation

/// GUI 11a, mechanically: the app's icon set is complete and was made from the
/// one master, `lib/design/assets/Logo.png`, by `scripts/icon.sh`.
///
/// The set is committed, so a build needs no script run, which is why it can
/// go stale. `icon.sh generate` records the master's SHA-256 and each image's
/// in the set's `Contents.json`, and the rules read those back, each a
/// violation naming what broke it. The records catch a set that has drifted
/// from its master, not one whose records were edited to match:
///
/// - every size macOS asks for is declared, and every size declared is a PNG of
///   exactly its pixels;
/// - the recorded master is the master on disk;
/// - each image matches the hash the script recorded;
/// - the application target names the icon in every configuration.
struct IconSetCheck {
    /// One image macOS asks for: a size in points at a scale.
    struct Slot: Equatable {
        let points: Int
        let scale: Int

        var pixels: Int { points * scale }
        var label: String { "\(points)x\(points)@\(scale)x" }
        var filename: String {
            "icon_\(points)x\(points)\(scale == 1 ? "" : "@\(scale)x").png"
        }
    }

    /// The macOS icon sizes: 16 through 512 points, each at 1x and 2x.
    static let slots: [Slot] = [16, 32, 128, 256, 512].flatMap { points in
        [1, 2].map { Slot(points: points, scale: $0) }
    }

    static let iconName = "AppIcon"
    static let configurations = ["Debug", "Release"]
    /// Relative to the app's directory, lib/src/darwin.
    static let setPath = "source/main/Assets.xcassets/AppIcon.appiconset"
    static let masterPath = "../../design/assets/Logo.png"
    static let masterName = "lib/design/assets/Logo.png"
    static let remedy = "run lib/src/darwin/scripts/icon.sh generate"

    /// The bytes at a path relative to the app's directory; nil when absent.
    let read: (String) -> Data?
    /// The application target's icon, by build configuration.
    let iconNames: [String: String]

    /// Every violation, in a stable order; empty when the set holds.
    func violations() -> [String] {
        target() + set()
    }

    private func target() -> [String] {
        Self.configurations.compactMap { configuration in
            iconNames[configuration] == Self.iconName
                ? nil
                : "the application target's \(configuration) configuration does not name \(Self.iconName) "
                    + "(ASSETCATALOG_COMPILER_APPICON_NAME): the app would show the generic icon"
        }
    }

    private func set() -> [String] {
        guard let master = read(Self.masterPath) else {
            return ["the master \(Self.masterName) is missing: the icon set is made from it"]
        }
        guard let data = read(Self.setPath + "/Contents.json"),
            let contents = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
            let images = contents["images"] as? [[String: Any]],
            let info = contents["info"] as? [String: Any]
        else {
            return ["\(Self.iconName).appiconset has no readable Contents.json: \(Self.remedy)"]
        }

        var found: [String] = []
        let recorded = info["master-sha256"] as? String
        if recorded != Self.sha256(master) {
            found.append(
                "\(Self.iconName).appiconset is stale: it was not made from the current \(Self.masterName): \(Self.remedy)")
        }

        let hashes = info["files-sha256"] as? [String: String] ?? [:]
        var declared: [Slot] = []
        for image in images {
            guard let slot = Self.slot(of: image), let filename = image["filename"] as? String else {
                found.append("\(Self.iconName).appiconset declares an image it does not fully name: \(Self.remedy)")
                continue
            }
            declared.append(slot)
            found += problems(with: filename, for: slot, recorded: hashes[filename])
        }
        for slot in Self.slots where !declared.contains(slot) {
            found.append("\(Self.iconName).appiconset does not declare \(slot.label), which macOS asks for: \(Self.remedy)")
        }
        return found
    }

    private func problems(with filename: String, for slot: Slot, recorded: String?) -> [String] {
        let name = "\(Self.iconName).appiconset/\(filename)"
        guard let image = read(Self.setPath + "/" + filename) else {
            return ["\(name) is missing, for \(slot.label): \(Self.remedy)"]
        }
        var found: [String] = []
        if let size = Self.pngSize(image) {
            if size.width != slot.pixels || size.height != slot.pixels {
                found.append(
                    "\(name) is \(size.width)x\(size.height) pixels: \(slot.label) is \(slot.pixels)x\(slot.pixels)")
            }
        } else {
            found.append("\(name) is not a PNG: \(Self.remedy)")
        }
        if recorded != Self.sha256(image) {
            found.append("\(name) does not match the hash icon.sh recorded: \(Self.remedy)")
        }
        return found
    }

    /// A `Contents.json` image's size and scale ("16x16", "2x").
    private static func slot(of image: [String: Any]) -> Slot? {
        guard image["idiom"] as? String == "mac",
            let size = (image["size"] as? String)?.split(separator: "x"), size.count == 2,
            let width = Int(size[0]), width == Int(size[1]),
            let scale = (image["scale"] as? String)?.dropLast(), let factor = Int(scale)
        else { return nil }
        return Slot(points: width, scale: factor)
    }

    /// A PNG's pixel size, from its header; nil when the bytes are not a PNG.
    static func pngSize(_ data: Data) -> (width: Int, height: Int)? {
        let bytes = [UInt8](data.prefix(24))
        let signature: [UInt8] = [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]
        guard bytes.count == 24, Array(bytes[0..<8]) == signature, Array(bytes[12..<16]) == Array("IHDR".utf8)
        else { return nil }
        let number = { (range: Range<Int>) in bytes[range].reduce(0) { $0 << 8 | Int($1) } }
        return (number(16..<20), number(20..<24))
    }

    static func sha256(_ data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }
}
