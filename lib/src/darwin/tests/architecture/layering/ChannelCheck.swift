import Foundation

/// GUI M18, mechanically: the application target builds one channel per
/// configuration, and the release channel's identity never moves.
///
/// Development runs on the dev channel (the user's call, 2026-10-31). A Debug
/// build -- Xcode's Run and `build.sh open` -- is "Apogee Dev" on its own
/// bundle identifier, so its own `UserDefaults` domain and its own Dock
/// identity; the Release build -- `build.sh build` and `package`, CI and the
/// release -- is the app as it always was. Each configuration's settings are
/// read from `project.pbxproj` as written and expanded as Xcode expands them,
/// and the rules, each a violation naming what broke it, are:
///
/// - each configuration builds its channel (`APOGEE_CHANNEL`);
/// - its bundle identifier, product name and display name are its channel's,
///   the Release ones exactly what shipped before channels existed;
/// - it names `Info.plist`, and `Info.plist` carries `ApogeeChannel` as
///   `$(APOGEE_CHANNEL)`, the one key the composition root reads.
///
/// Its icon is `IconSetCheck`'s to hold.
struct ChannelCheck {
    /// What a configuration's build is.
    struct Identity: Equatable {
        let channel: String
        let identifier: String
        let name: String
    }

    /// The two channels, by the configuration that builds each.
    static let expected: [String: Identity] = [
        "Debug": Identity(channel: "dev", identifier: "com.quantumcompiler.apogee.dev", name: "Apogee Dev"),
        "Release": Identity(channel: "release", identifier: "com.quantumcompiler.apogee", name: "Apogee"),
    ]

    /// Relative to the app's directory, lib/src/darwin; outside every
    /// synchronized folder, so no target copies it as a resource.
    static let infoPlistPath = "Info.plist"
    static let channelKey = "ApogeeChannel"
    static let channelValue = "$(APOGEE_CHANNEL)"

    /// The application target's name, which `$(TARGET_NAME)` expands to.
    let targetName: String
    /// The application target's settings, by build configuration.
    let settings: [String: [String: String]]
    /// The bytes at a path relative to the app's directory; nil when absent.
    let read: (String) -> Data?

    /// Every violation, in a stable order; empty when the channels hold.
    func violations() -> [String] {
        Self.expected.keys.sorted().flatMap(configuration) + infoPlist()
    }

    private func configuration(_ name: String) -> [String] {
        guard let identity = Self.expected[name] else { return [] }
        let settings = settings[name] ?? [:]
        let value = { (key: String) in Self.resolve(settings[key] ?? "", in: settings, targetName: targetName) }
        let target = "the application target's \(name) configuration"
        var found: [String] = []
        let channel = value("APOGEE_CHANNEL")
        if channel != identity.channel {
            found.append("\(target) builds the \(channel) channel (APOGEE_CHANNEL): it builds \(identity.channel)")
        }
        let facts = [
            ("bundle identifier", value("PRODUCT_BUNDLE_IDENTIFIER"), identity.identifier),
            ("product name", value("PRODUCT_NAME"), identity.name),
            ("display name", value("INFOPLIST_KEY_CFBundleDisplayName"), identity.name),
        ]
        for (fact, actual, wanted) in facts where actual != wanted {
            found.append("\(target)'s \(fact) is \(actual): the \(identity.channel) channel's is \(wanted)")
        }
        if settings["INFOPLIST_FILE"] != Self.infoPlistPath {
            found.append(
                "\(target) sets no INFOPLIST_FILE: it is \(Self.infoPlistPath), which carries \(Self.channelKey)")
        }
        return found
    }

    private func infoPlist() -> [String] {
        guard let data = read(Self.infoPlistPath),
            let plist = (try? PropertyListSerialization.propertyList(from: data, format: nil)) as? [String: Any]
        else {
            return ["\(Self.infoPlistPath) is missing or unreadable: it carries \(Self.channelKey)"]
        }
        let named = plist[Self.channelKey] as? String
        guard named == Self.channelValue else {
            return [
                "\(Self.infoPlistPath)'s \(Self.channelKey) is \(named ?? "absent"): it is \(Self.channelValue), "
                    + "so each configuration names its own channel"
            ]
        }
        return []
    }

    /// `value` with every `$(NAME)` and `${NAME}` expanded from `settings`, and
    /// `TARGET_NAME` the target's: an unset name expands to nothing, as in
    /// Xcode. Expansion repeats until nothing is left to expand.
    static func resolve(_ value: String, in settings: [String: String], targetName: String) -> String {
        var settings = settings
        settings["TARGET_NAME"] = targetName
        var current = value
        for _ in 0..<8 {
            var expanded = ""
            var rest = Substring(current)
            while let dollar = rest.firstIndex(of: "$") {
                let open = rest.index(after: dollar)
                let close: Character? =
                    open < rest.endIndex ? (rest[open] == "(" ? ")" : rest[open] == "{" ? "}" : nil) : nil
                guard let close, let end = rest[open...].firstIndex(of: close) else {
                    expanded += rest[...dollar]
                    rest = rest[open...]
                    continue
                }
                expanded += rest[..<dollar]
                expanded += settings[String(rest[rest.index(after: open)..<end])] ?? ""
                rest = rest[rest.index(after: end)...]
            }
            expanded += rest
            if expanded == current { break }
            current = expanded
        }
        return current
    }
}
