import CoreGraphics
import Foundation
import ImageIO
import Testing

/// The app's directory, lib/src/darwin: this file is
/// tests/architecture/layering/IconSetTests.swift.
private let appRoot = URL(fileURLWithPath: #filePath)
    .deletingLastPathComponent().deletingLastPathComponent()
    .deletingLastPathComponent().deletingLastPathComponent()

@Suite("The app's icon set, on this project")
struct IconSetOnThisProjectTests {
    @Test("the set is complete, made from the current master, and named by the app target")
    func thisSetHolds() throws {
        let map = try ModuleMap(projectAt: appRoot.appendingPathComponent("Apogee.xcodeproj"))
        let app = try #require(map.targets.first { $0.kind == .application })
        let check = IconSetCheck(
            read: { try? Data(contentsOf: appRoot.appendingPathComponent($0)) },
            iconNames: app.appIconNames)
        let violations = check.violations()
        #expect(violations.isEmpty, "\(violations.joined(separator: "\n"))")
    }
}

/// A set that holds, as `icon.sh` would write it, for one test to break.
private struct PlantedSet {
    var files: [String: Data] = [:]
    var iconNames = ["Debug": "AppIcon", "Release": "AppIcon"]

    static let set = IconSetCheck.setPath

    static func png(_ width: Int, _ height: Int) -> Data {
        var bytes: [UInt8] = [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13]
        bytes += Array("IHDR".utf8)
        for value in [width, height] {
            bytes += [24, 16, 8, 0].map { UInt8(value >> $0 & 0xFF) }
        }
        return Data(bytes)
    }

    init() throws {
        let master = Data("the master".utf8)
        files[IconSetCheck.masterPath] = master
        var images: [[String: String]] = []
        var hashes: [String: String] = [:]
        for slot in IconSetCheck.slots {
            let image = Self.png(slot.pixels, slot.pixels)
            files[Self.set + "/" + slot.filename] = image
            hashes[slot.filename] = IconSetCheck.sha256(image)
            images.append([
                "idiom": "mac", "size": "\(slot.points)x\(slot.points)", "scale": "\(slot.scale)x",
                "filename": slot.filename,
            ])
        }
        try write(images: images, master: IconSetCheck.sha256(master), hashes: hashes)
    }

    mutating func write(images: [[String: String]], master: String, hashes: [String: String]) throws {
        let info: [String: Any] = [
            "author": "xcode", "version": 1, "master-sha256": master, "files-sha256": hashes,
        ]
        files[Self.set + "/Contents.json"] = try JSONSerialization.data(
            withJSONObject: ["images": images, "info": info] as [String: Any])
    }

    /// An image replaced, its record updated to match it.
    mutating func replace(_ slot: IconSetCheck.Slot, with image: Data) throws {
        files[Self.set + "/" + slot.filename] = image
        let path = Self.set + "/Contents.json"
        let data = try #require(files[path])
        var contents = try #require(try JSONSerialization.jsonObject(with: data) as? [String: Any])
        var info = try #require(contents["info"] as? [String: Any])
        var hashes = try #require(info["files-sha256"] as? [String: String])
        hashes[slot.filename] = IconSetCheck.sha256(image)
        info["files-sha256"] = hashes
        contents["info"] = info
        files[path] = try JSONSerialization.data(withJSONObject: contents)
    }

    var violations: [String] {
        let files = files
        return IconSetCheck(read: { files[$0] }, iconNames: iconNames).violations()
    }
}

@Suite("The icon-set check, on planted sets")
struct IconSetPlantedTests {
    private let small = IconSetCheck.Slot(points: 16, scale: 2)

    @Test("a set as icon.sh writes it holds, so each failure below is its own")
    func aWholeSetHolds() throws {
        #expect(try PlantedSet().violations == [])
    }

    @Test("macOS's ten sizes run from 16 to 1024 pixels, each named as the set names it")
    func theSizes() {
        #expect(IconSetCheck.slots.count == 10)
        #expect(IconSetCheck.slots.map(\.pixels).sorted().first == 16)
        #expect(IconSetCheck.slots.map(\.pixels).sorted().last == 1024)
        #expect(small.filename == "icon_16x16@2x.png")
        #expect(IconSetCheck.Slot(points: 16, scale: 1).filename == "icon_16x16.png")
    }

    @Test("a master changed without re-running icon.sh fails, naming the stale set")
    func staleMaster() throws {
        var set = try PlantedSet()
        set.files[IconSetCheck.masterPath] = Data("a new master".utf8)
        #expect(set.violations == [
            "AppIcon.appiconset is stale: it was not made from the current lib/design/assets/Logo.png: "
                + "run lib/src/darwin/scripts/icon.sh generate"
        ])
    }

    @Test("a missing master is said, rather than read as a stale set")
    func missingMaster() throws {
        var set = try PlantedSet()
        set.files[IconSetCheck.masterPath] = nil
        #expect(set.violations == ["the master lib/design/assets/Logo.png is missing: the icon set is made from it"])
    }

    @Test("a declared size whose image is gone fails, naming it")
    func missingImage() throws {
        var set = try PlantedSet()
        set.files[PlantedSet.set + "/" + small.filename] = nil
        #expect(set.violations == [
            "AppIcon.appiconset/icon_16x16@2x.png is missing, for 16x16@2x: run lib/src/darwin/scripts/icon.sh generate"
        ])
    }

    @Test("a size macOS asks for that the set stops declaring fails, naming it")
    func undeclaredSize() throws {
        var set = try PlantedSet()
        let images = IconSetCheck.slots.filter { $0 != small }.map { slot in
            [
                "idiom": "mac", "size": "\(slot.points)x\(slot.points)", "scale": "\(slot.scale)x",
                "filename": slot.filename,
            ]
        }
        let hashes = Dictionary(
            uniqueKeysWithValues: IconSetCheck.slots.map {
                ($0.filename, IconSetCheck.sha256(PlantedSet.png($0.pixels, $0.pixels)))
            })
        try set.write(images: images, master: IconSetCheck.sha256(Data("the master".utf8)), hashes: hashes)
        #expect(set.violations == [
            "AppIcon.appiconset does not declare 16x16@2x, which macOS asks for: run lib/src/darwin/scripts/icon.sh generate"
        ])
    }

    @Test("an image of the wrong pixel size fails, whatever its record says")
    func wrongPixels() throws {
        var set = try PlantedSet()
        try set.replace(small, with: PlantedSet.png(31, 32))
        #expect(set.violations == ["AppIcon.appiconset/icon_16x16@2x.png is 31x32 pixels: 16x16@2x is 32x32"])
    }

    @Test("an image of the right width and the wrong height fails")
    func wrongHeight() throws {
        var set = try PlantedSet()
        set.files[PlantedSet.set + "/" + small.filename] = PlantedSet.png(32, 31)
        #expect(set.violations.contains("AppIcon.appiconset/icon_16x16@2x.png is 32x31 pixels: 16x16@2x is 32x32"))
    }

    @Test("an image of the right size that differs from its recorded hash fails")
    func handReplacedImage() throws {
        var set = try PlantedSet()
        set.files[PlantedSet.set + "/" + small.filename] = PlantedSet.png(32, 32) + Data([0])
        #expect(set.violations == [
            "AppIcon.appiconset/icon_16x16@2x.png does not match the hash icon.sh recorded: run lib/src/darwin/scripts/icon.sh generate"
        ])
    }

    @Test("bytes that are not a PNG fail as that")
    func notAPNG() throws {
        var set = try PlantedSet()
        set.files[PlantedSet.set + "/" + small.filename] = Data("not an image".utf8)
        #expect(set.violations.contains(
            "AppIcon.appiconset/icon_16x16@2x.png is not a PNG: run lib/src/darwin/scripts/icon.sh generate"))
    }

    @Test("a set with no Contents.json fails, and says what to run")
    func noContents() throws {
        var set = try PlantedSet()
        set.files[PlantedSet.set + "/Contents.json"] = nil
        #expect(set.violations == [
            "AppIcon.appiconset has no readable Contents.json: run lib/src/darwin/scripts/icon.sh generate"
        ])
    }

    private static func targetViolation(_ configuration: String) -> String {
        "the application target's \(configuration) configuration does not name AppIcon "
            + "(ASSETCATALOG_COMPILER_APPICON_NAME): the app would show the generic icon"
    }

    @Test("an app target that stops naming the icon fails, by configuration", arguments: ["Debug", "Release"])
    func targetNamesNoIcon(configuration: String) throws {
        var set = try PlantedSet()
        set.iconNames[configuration] = nil
        #expect(set.violations == [Self.targetViolation(configuration)])
    }

    @Test("an app target naming another icon fails, by configuration", arguments: ["Debug", "Release"])
    func targetNamesAnotherIcon(configuration: String) throws {
        var set = try PlantedSet()
        set.iconNames[configuration] = "Other"
        #expect(set.violations == [Self.targetViolation(configuration)])
    }
}

/// Runs a program, its output and errors together.
private func run(_ program: String, _ arguments: [String]) throws -> (status: Int32, said: String) {
    let process = Process()
    let output = Pipe()
    process.executableURL = URL(fileURLWithPath: program)
    process.arguments = arguments
    var environment = ["PATH": "/usr/bin:/bin"]
    for name in ["DEVELOPER_DIR", "HOME"] {
        environment[name] = ProcessInfo.processInfo.environment[name]
    }
    process.environment = environment
    process.standardOutput = output
    process.standardError = output
    try process.run()
    let said = String(decoding: output.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
    process.waitUntilExit()
    return (process.terminationStatus, said)
}

/// A new temporary folder, for one test to remove.
private func temporaryFolder() throws -> URL {
    let folder = FileManager.default.temporaryDirectory.appendingPathComponent("apogee-icon-\(UUID().uuidString)")
    try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    return folder
}

private let realSet = appRoot.appendingPathComponent(IconSetCheck.setPath)
private let realMaster = appRoot.appendingPathComponent(IconSetCheck.masterPath)

@Suite("icon.sh verify, on planted bundles")
struct IconVerifyTests {
    /// What a planted bundle's Assets.car is compiled from.
    private enum Catalog {
        case none, notACatalog
        /// The real set under `name`, cut to its first `sizes` images when given.
        case iconSet(name: String, sizes: Int?)
        /// An ordinary image that happens to be called AppIcon.
        case imageNamedAppIcon
    }

    private func write(_ json: [String: Any], to url: URL) throws {
        try JSONSerialization.data(withJSONObject: json).write(to: url)
    }

    private func compile(_ catalog: Catalog, into resources: URL, in folder: URL) throws {
        let info = ["author": "xcode", "version": 1] as [String: Any]
        let assets = folder.appendingPathComponent("Planted.xcassets")
        try FileManager.default.createDirectory(at: assets, withIntermediateDirectories: true)
        try write(["info": info], to: assets.appendingPathComponent("Contents.json"))
        var iconArguments: [String] = []
        switch catalog {
        case .none:
            return
        case .notACatalog:
            try Data("not a catalog".utf8).write(to: resources.appendingPathComponent("Assets.car"))
            return
        case .iconSet(let name, let sizes):
            let set = assets.appendingPathComponent("\(name).appiconset")
            try FileManager.default.copyItem(at: realSet, to: set)
            if let sizes {
                let contents = set.appendingPathComponent("Contents.json")
                var json = try #require(
                    try JSONSerialization.jsonObject(with: Data(contentsOf: contents)) as? [String: Any])
                json["images"] = Array(try #require(json["images"] as? [[String: Any]]).prefix(sizes))
                try write(json, to: contents)
            }
            iconArguments = ["--app-icon", name]
        case .imageNamedAppIcon:
            let set = assets.appendingPathComponent("AppIcon.imageset")
            try FileManager.default.createDirectory(at: set, withIntermediateDirectories: true)
            try FileManager.default.copyItem(
                at: realSet.appendingPathComponent("icon_128x128.png"), to: set.appendingPathComponent("image.png"))
            try write(
                ["images": [["idiom": "universal", "filename": "image.png"]], "info": info],
                to: set.appendingPathComponent("Contents.json"))
        }
        let compiled = try run(
            "/usr/bin/xcrun",
            ["actool", "--compile", resources.path, "--platform", "macosx", "--minimum-deployment-target", "27.0"]
                + iconArguments
                + ["--output-partial-info-plist", folder.appendingPathComponent("partial.plist").path, assets.path])
        #expect(compiled.status == 0, "\(compiled.said)")
    }

    /// `scripts/icon.sh verify` on a bundle holding `plist` and `catalog`.
    private func verify(plist: [String: String], catalog: Catalog = .none) throws -> (status: Int32, said: String) {
        let folder = try temporaryFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let app = folder.appendingPathComponent("Apogee.app")
        let resources = app.appendingPathComponent("Contents/Resources")
        try FileManager.default.createDirectory(at: resources, withIntermediateDirectories: true)
        try PropertyListSerialization.data(fromPropertyList: plist, format: .xml, options: 0)
            .write(to: app.appendingPathComponent("Contents/Info.plist"))
        try compile(catalog, into: resources, in: folder)
        return try run("/bin/bash", [appRoot.appendingPathComponent("scripts/icon.sh").path, "verify", app.path])
    }

    private let namesIcon = ["CFBundleIconName": "AppIcon"]

    @Test("a bundle whose catalog holds the whole icon passes")
    func wholeIcon() throws {
        let result = try verify(plist: namesIcon, catalog: .iconSet(name: "AppIcon", sizes: nil))
        #expect(result.status == 0, "\(result.said)")
    }

    @Test("a bundle whose Info.plist names no icon is refused")
    func namesNoIcon() throws {
        let result = try verify(plist: ["CFBundleName": "Apogee"], catalog: .iconSet(name: "AppIcon", sizes: nil))
        #expect(result.status != 0)
        #expect(result.said.contains("names no icon"), "\(result.said)")
    }

    @Test("a bundle naming another icon is refused")
    func namesAnotherIcon() throws {
        let result = try verify(plist: ["CFBundleIconName": "Other"], catalog: .iconSet(name: "AppIcon", sizes: nil))
        #expect(result.status != 0)
        #expect(result.said.contains("names the icon Other"), "\(result.said)")
    }

    @Test("a bundle naming the icon without a compiled catalog is refused")
    func noCatalog() throws {
        let result = try verify(plist: namesIcon)
        #expect(result.status != 0)
        #expect(result.said.contains("Assets.car"), "\(result.said)")
    }

    @Test("a file that is no catalog is refused")
    func notACatalog() throws {
        let result = try verify(plist: namesIcon, catalog: .notACatalog)
        #expect(result.status != 0)
        #expect(result.said.contains("does not hold AppIcon"), "\(result.said)")
    }

    @Test("a real catalog holding only another icon is refused")
    func anotherIconInTheCatalog() throws {
        let result = try verify(plist: namesIcon, catalog: .iconSet(name: "Other", sizes: nil))
        #expect(result.status != 0)
        #expect(result.said.contains("does not hold AppIcon"), "\(result.said)")
    }

    @Test("an ordinary image called AppIcon is not the icon")
    func imageNamedAppIcon() throws {
        let result = try verify(plist: namesIcon, catalog: .imageNamedAppIcon)
        #expect(result.status != 0)
        #expect(result.said.contains("does not hold AppIcon"), "\(result.said)")
        #expect(!result.said.contains("at every size"), "\(result.said)")
    }

    @Test("an icon compiled with one size of ten is refused")
    func oneSizeOfTen() throws {
        let result = try verify(plist: namesIcon, catalog: .iconSet(name: "AppIcon", sizes: 1))
        #expect(result.status != 0)
        #expect(result.said.contains("does not hold AppIcon at every size"), "\(result.said)")
    }
}

/// `icon.sh generate` in a copy of the tree it writes to, the real tree untouched.
@Suite("icon.sh generate, in a planted tree")
struct IconGenerateTests {
    private struct Tree {
        let root: URL
        var app: URL { root.appendingPathComponent("lib/src/darwin") }
        var assets: URL { root.appendingPathComponent("lib/design/assets") }
        var master: URL { assets.appendingPathComponent("Logo.png") }
        var readmeLogo: URL { assets.appendingPathComponent("Logo-320.png") }
        var iconSet: URL { app.appendingPathComponent(IconSetCheck.setPath) }
        var contents: URL { iconSet.appendingPathComponent("Contents.json") }

        /// A tree holding the script, the real master, and an earlier run's
        /// files for a refusal to leave alone.
        init() throws {
            root = try temporaryFolder()
            let manager = FileManager.default
            for folder in [app.appendingPathComponent("scripts"), assets, iconSet] {
                try manager.createDirectory(at: folder, withIntermediateDirectories: true)
            }
            for script in ["icon.sh", "icon-small.swift"] {
                try manager.copyItem(
                    at: appRoot.appendingPathComponent("scripts/\(script)"),
                    to: app.appendingPathComponent("scripts/\(script)"))
            }
            try manager.copyItem(at: realMaster, to: master)
            try Data("the earlier set".utf8).write(to: contents)
            try Data("the earlier copy".utf8).write(to: readmeLogo)
        }

        func generate() throws -> (status: Int32, said: String) {
            try run("/bin/bash", [app.appendingPathComponent("scripts/icon.sh").path, "generate"])
        }

        /// The script refused, and everything it would write is as it was.
        func expectRefused(_ result: (status: Int32, said: String), saying words: String) throws {
            #expect(result.status != 0)
            #expect(result.said.contains(words), "\(result.said)")
            #expect(try Data(contentsOf: contents) == Data("the earlier set".utf8))
            #expect(try Data(contentsOf: readmeLogo) == Data("the earlier copy".utf8))
        }
    }

    @Test("what it writes holds the check, and the README's copy is 320 pixels")
    func writesAWholeSet() throws {
        let tree = try Tree()
        defer { try? FileManager.default.removeItem(at: tree.root) }
        let result = try tree.generate()
        #expect(result.status == 0, "\(result.said)")
        let check = IconSetCheck(
            read: { try? Data(contentsOf: tree.app.appendingPathComponent($0)) },
            iconNames: ["Debug": "AppIcon", "Release": "AppIcon"])
        #expect(check.violations() == [])
        let copy = try Data(contentsOf: tree.readmeLogo)
        #expect(IconSetCheck.pngSize(copy)?.width == 320)
    }

    /// An image's pixels as 8-bit gray, row by row.
    private func grayPixels(of url: URL) throws -> (side: Int, pixels: [UInt8]) {
        let source = try #require(CGImageSourceCreateWithURL(url as CFURL, nil))
        let image = try #require(CGImageSourceCreateImageAtIndex(source, 0, nil))
        let side = image.width
        var pixels = [UInt8](repeating: 0, count: side * side)
        let drawn = pixels.withUnsafeMutableBytes { bytes -> Bool in
            guard
                let context = CGContext(
                    data: bytes.baseAddress, width: side, height: side, bitsPerComponent: 8, bytesPerRow: side,
                    space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.none.rawValue)
            else { return false }
            context.draw(image, in: CGRect(x: 0, y: 0, width: side, height: side))
            return true
        }
        #expect(drawn)
        return (side, pixels)
    }

    @Test("at 32 pixels or fewer the rocket stands out on black; larger sizes are the whole artwork")
    func smallSizesShowTheRocket() throws {
        let tree = try Tree()
        defer { try? FileManager.default.removeItem(at: tree.root) }
        let result = try tree.generate()
        #expect(result.status == 0, "\(result.said)")
        for slot in IconSetCheck.slots {
            let written = tree.iconSet.appendingPathComponent(slot.filename)
            if slot.pixels <= 32 {
                let (side, pixels) = try grayPixels(of: written)
                let bright = pixels.filter { $0 >= 128 }.count
                #expect(bright * 100 >= side * side * 15, "\(slot.label): \(bright) bright pixels of \(side * side)")
                let last = side - 1
                for (x, y) in [(0, 0), (last, 0), (0, last), (last, last)] {
                    #expect(pixels[y * side + x] < 16, "\(slot.label): the field shows at its corner")
                }
            } else {
                let out = tree.root.appendingPathComponent("expected-\(slot.filename)")
                let size = String(slot.pixels), density = String(72 * slot.scale)
                let made = try run(
                    "/usr/bin/sips",
                    ["-s", "format", "png", "-s", "dpiWidth", density, "-s", "dpiHeight", density, "-z", size, size,
                        tree.master.path, "--out", out.path])
                #expect(made.status == 0, "\(made.said)")
                #expect(try Data(contentsOf: written) == (try Data(contentsOf: out)), "\(slot.label)")
            }
        }
    }

    @Test("a master cut short is refused, though sips would still read its header")
    func truncatedMaster() throws {
        let tree = try Tree()
        defer { try? FileManager.default.removeItem(at: tree.root) }
        try Data(contentsOf: realMaster).prefix(200_000).write(to: tree.master)
        try tree.expectRefused(try tree.generate(), saying: "cut short")
    }

    @Test("a master that stops cleanly between two chunks, its end marker gone, is refused")
    func masterWithoutItsEnd() throws {
        let tree = try Tree()
        defer { try? FileManager.default.removeItem(at: tree.root) }
        try Data(contentsOf: realMaster).dropLast(12).write(to: tree.master)
        try tree.expectRefused(try tree.generate(), saying: "cut short")
    }

    @Test("a master with a damaged byte is refused")
    func damagedMaster() throws {
        let tree = try Tree()
        defer { try? FileManager.default.removeItem(at: tree.root) }
        var bytes = try Data(contentsOf: realMaster)
        bytes[bytes.count / 2] ^= 0xFF
        try bytes.write(to: tree.master)
        try tree.expectRefused(try tree.generate(), saying: "damaged")
    }

    @Test("a master that is not square is refused, not stretched")
    func wideMaster() throws {
        let tree = try Tree()
        defer { try? FileManager.default.removeItem(at: tree.root) }
        let cropped = try run(
            "/usr/bin/sips", ["-s", "format", "png", "-c", "1024", "2048", realMaster.path, "--out", tree.master.path])
        #expect(cropped.status == 0, "\(cropped.said)")
        try tree.expectRefused(try tree.generate(), saying: "2048x1024")
    }

    @Test("a square master of another size is refused, the rocket's crop being measured on 2048")
    func smallerMaster() throws {
        let tree = try Tree()
        defer { try? FileManager.default.removeItem(at: tree.root) }
        let scaled = try run(
            "/usr/bin/sips", ["-s", "format", "png", "-z", "1024", "1024", realMaster.path, "--out", tree.master.path])
        #expect(scaled.status == 0, "\(scaled.said)")
        try tree.expectRefused(try tree.generate(), saying: "1024x1024")
    }
}
