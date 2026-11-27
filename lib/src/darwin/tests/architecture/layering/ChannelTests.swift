import Foundation
import Testing

/// The app's directory, lib/src/darwin: this file is
/// tests/architecture/layering/ChannelTests.swift.
private let appRoot = URL(fileURLWithPath: #filePath)
    .deletingLastPathComponent().deletingLastPathComponent()
    .deletingLastPathComponent().deletingLastPathComponent()

@Suite("The app's channels, on this project (GUI M18)")
struct ChannelOnThisProjectTests {
    @Test("Release is the release app as it always was, Debug is Apogee Dev on its own identifier")
    func thisProjectHolds() throws {
        let map = try ModuleMap(projectAt: appRoot.appendingPathComponent("Apogee.xcodeproj"))
        let app = try #require(map.targets.first { $0.kind == .application })
        let check = ChannelCheck(
            targetName: app.name, settings: app.settings,
            read: { try? Data(contentsOf: appRoot.appendingPathComponent($0)) })
        let violations = check.violations()
        #expect(violations.isEmpty, "\(violations.joined(separator: "\n"))")
    }
}

/// An application target that holds, as the project sets it, for one test to
/// break.
private struct PlantedApp {
    var settings: [String: [String: String]] = [
        "Debug": [
            "APOGEE_CHANNEL": "dev", "APOGEE_CHANNEL_SUFFIX": ".dev",
            "PRODUCT_BUNDLE_IDENTIFIER": "com.quantumcompiler.apogee$(APOGEE_CHANNEL_SUFFIX)",
            "PRODUCT_NAME": "Apogee Dev", "INFOPLIST_KEY_CFBundleDisplayName": "Apogee Dev",
            "INFOPLIST_FILE": "Info.plist",
        ],
        "Release": [
            "APOGEE_CHANNEL": "release", "APOGEE_CHANNEL_SUFFIX": "",
            "PRODUCT_BUNDLE_IDENTIFIER": "com.quantumcompiler.apogee$(APOGEE_CHANNEL_SUFFIX)",
            "PRODUCT_NAME": "$(TARGET_NAME)", "INFOPLIST_KEY_CFBundleDisplayName": "Apogee",
            "INFOPLIST_FILE": "Info.plist",
        ],
    ]
    var infoPlist: [String: Any]? = ["ApogeeChannel": "$(APOGEE_CHANNEL)"]

    var violations: [String] {
        let data = infoPlist.flatMap {
            try? PropertyListSerialization.data(fromPropertyList: $0, format: .xml, options: 0)
        }
        return ChannelCheck(targetName: "Apogee", settings: settings, read: { $0 == "Info.plist" ? data : nil })
            .violations()
    }
}

@Suite("The channel check, on planted targets (GUI M18)")
struct ChannelPlantedTests {
    @Test("a target as the project sets it holds, so each failure below is its own")
    func aWholeTargetHolds() {
        #expect(PlantedApp().violations == [])
    }

    @Test("each configuration's channel is pinned: Debug the dev channel, Release the release channel")
    func theTable() {
        #expect(ChannelCheck.expected["Debug"]?.channel == "dev")
        #expect(ChannelCheck.expected["Release"]?.channel == "release")
        #expect(ChannelCheck.expected["Release"]?.identifier == "com.quantumcompiler.apogee")
        #expect(ChannelCheck.expected["Release"]?.name == "Apogee")
        #expect(ChannelCheck.expected["Debug"]?.identifier == "com.quantumcompiler.apogee.dev")
        #expect(ChannelCheck.expected["Debug"]?.name == "Apogee Dev")
    }

    @Test("a Release identifier that moves fails: the installed app would lose its settings and its Dock place")
    func releaseIdentifierMoves() {
        var app = PlantedApp()
        app.settings["Release"]?["APOGEE_CHANNEL_SUFFIX"] = ".release"
        #expect(app.violations == [
            "the application target's Release configuration's bundle identifier is com.quantumcompiler.apogee.release: "
                + "the release channel's is com.quantumcompiler.apogee"
        ])
    }

    @Test("a Debug identifier that is the release one fails: a development build would share the installed app's settings")
    func debugSharesTheReleaseIdentifier() {
        var app = PlantedApp()
        app.settings["Debug"]?["APOGEE_CHANNEL_SUFFIX"] = nil
        #expect(app.violations == [
            "the application target's Debug configuration's bundle identifier is com.quantumcompiler.apogee: "
                + "the dev channel's is com.quantumcompiler.apogee.dev"
        ])
    }

    @Test("a product name that changes fails, by configuration")
    func productName() {
        var app = PlantedApp()
        app.settings["Release"]?["PRODUCT_NAME"] = "Apogee Release"
        app.settings["Debug"]?["PRODUCT_NAME"] = "$(TARGET_NAME)"
        #expect(app.violations == [
            "the application target's Debug configuration's product name is Apogee: the dev channel's is Apogee Dev",
            "the application target's Release configuration's product name is Apogee Release: the release channel's is Apogee",
        ])
    }

    @Test("a display name that changes fails")
    func displayName() {
        var app = PlantedApp()
        app.settings["Debug"]?["INFOPLIST_KEY_CFBundleDisplayName"] = "Apogee"
        #expect(app.violations == [
            "the application target's Debug configuration's display name is Apogee: the dev channel's is Apogee Dev"
        ])
    }

    @Test("channels swapped between the configurations fail, each named")
    func swapped() {
        var app = PlantedApp()
        app.settings["Debug"]?["APOGEE_CHANNEL"] = "release"
        app.settings["Release"]?["APOGEE_CHANNEL"] = "dev"
        #expect(app.violations == [
            "the application target's Debug configuration builds the release channel (APOGEE_CHANNEL): it builds dev",
            "the application target's Release configuration builds the dev channel (APOGEE_CHANNEL): it builds release",
        ])
    }

    @Test("a configuration that sets no Info.plist fails: the app could not read its channel")
    func noInfoPlist() {
        var app = PlantedApp()
        app.settings["Release"]?["INFOPLIST_FILE"] = nil
        #expect(app.violations == [
            "the application target's Release configuration sets no INFOPLIST_FILE: it is Info.plist, "
                + "which carries ApogeeChannel"
        ])
    }

    @Test("an Info.plist that does not carry the channel fails")
    func infoPlistWithoutTheChannel() {
        var app = PlantedApp()
        app.infoPlist = ["ApogeeChannel": "dev"]
        #expect(app.violations == [
            "Info.plist's ApogeeChannel is dev: it is $(APOGEE_CHANNEL), so each configuration names its own channel"
        ])
        app.infoPlist = nil
        #expect(app.violations == ["Info.plist is missing or unreadable: it carries ApogeeChannel"])
    }

    @Test("a setting resolves through the configuration's own settings and the target's name, as Xcode expands it")
    func resolution() {
        let settings = ["A": "x$(B)${C}", "B": "-b", "C": "$(TARGET_NAME)"]
        #expect(ChannelCheck.resolve("$(A)", in: settings, targetName: "Apogee") == "x-bApogee")
        #expect(ChannelCheck.resolve("$(UNSET)y", in: settings, targetName: "Apogee") == "y")
    }
}
