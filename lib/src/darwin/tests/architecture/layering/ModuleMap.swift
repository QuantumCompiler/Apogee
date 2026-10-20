import Foundation

/// The app's module map, as `Apogee.xcodeproj` declares it: every target, the
/// folder it builds, what it depends on and links, what it embeds -- read from
/// `project.pbxproj` itself, so the check reads exactly the graph Xcode
/// builds -- and which test bundles the shared scheme runs.
struct ModuleMap {
    enum Kind: Equatable {
        case framework, application, test
        case other(String)
    }

    struct Dependency: Equatable {
        let name: String
        /// A product of a Swift package, rather than a target of this project.
        let isProduct: Bool
    }

    struct Target {
        let name: String
        let kind: Kind
        /// The folders the target builds (its file-system synchronized
        /// groups), relative to the project's directory. One, by the rules.
        let paths: [String]
        /// What it is built after or links: this project's targets, and any
        /// package product.
        let dependencies: [Dependency]
        /// This project's frameworks it links.
        let linked: [String]
        /// This project's frameworks it embeds (an application's Frameworks/).
        let embedded: [String]
        /// Build settings it sets that reach outside the project
        /// (`OTHER_LDFLAGS`, `FRAMEWORK_SEARCH_PATHS` ...).
        let outsideSettings: [String]
        /// Whether a synchronized folder carves out membership exceptions.
        let hasMembershipExceptions: Bool

        init(
            name: String, kind: Kind, paths: [String], dependencies: [Dependency] = [],
            linked: [String] = [], embedded: [String] = [], outsideSettings: [String] = [],
            hasMembershipExceptions: Bool = false
        ) {
            self.name = name
            self.kind = kind
            self.paths = paths
            self.dependencies = dependencies
            self.linked = linked
            self.embedded = embedded
            self.outsideSettings = outsideSettings
            self.hasMembershipExceptions = hasMembershipExceptions
        }

        /// The one folder it builds, when it builds exactly one.
        var path: String? { paths.count == 1 ? paths[0] : nil }
    }

    let targets: [Target]
    /// Swift packages the project references.
    let packageReferences: Int
    /// Build settings the project itself sets that reach outside it.
    let projectOutsideSettings: [String]
    /// The test bundles the shared scheme `Apogee` runs; `nil` without the scheme.
    let schemeTestables: [String]?

    init(
        targets: [Target], packageReferences: Int = 0, projectOutsideSettings: [String] = [],
        schemeTestables: [String]?
    ) {
        self.targets = targets
        self.packageReferences = packageReferences
        self.projectOutsideSettings = projectOutsideSettings
        self.schemeTestables = schemeTestables
    }

    /// Settings that would let a target find or link something this project
    /// does not build.
    static let outsideSettingNames = [
        "OTHER_LDFLAGS", "OTHER_SWIFT_FLAGS", "LIBRARY_SEARCH_PATHS", "FRAMEWORK_SEARCH_PATHS",
        "SYSTEM_FRAMEWORK_SEARCH_PATHS", "HEADER_SEARCH_PATHS", "SWIFT_INCLUDE_PATHS",
    ]

    enum MapError: Error, CustomStringConvertible {
        case malformed(String)
        var description: String {
            switch self {
            case .malformed(let what): "project.pbxproj not understood: \(what)"
            }
        }
    }

    /// The project at `project` (an `.xcodeproj` directory).
    init(projectAt project: URL) throws {
        let data = try Data(contentsOf: project.appendingPathComponent("project.pbxproj"))
        guard let plist = try PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any]
        else { throw MapError.malformed("not a property list") }
        let scheme = try? String(
            contentsOf: project.appendingPathComponent("xcshareddata/xcschemes/Apogee.xcscheme"), encoding: .utf8)
        try self.init(projectPlist: plist, schemeXML: scheme)
    }

    /// The project from its parsed property list, and the shared scheme's XML.
    init(projectPlist plist: [String: Any], schemeXML: String?) throws {
        guard let objects = plist["objects"] as? [String: [String: Any]],
            let rootID = plist["rootObject"] as? String, let root = objects[rootID]
        else { throw MapError.malformed("no objects or root object") }

        func object(_ id: Any?) -> [String: Any]? { (id as? String).flatMap { objects[$0] } }
        func ids(_ value: Any?) -> [String] { (value as? [String]) ?? [] }

        // Each group's parent, to spell a folder's whole path from the main group down.
        var parent: [String: String] = [:]
        for (id, entry) in objects where entry["isa"] as? String == "PBXGroup" {
            for child in ids(entry["children"]) { parent[child] = id }
        }
        func fullPath(of id: String) -> String {
            var parts: [String] = []
            var current: String? = id
            while let id = current, let entry = objects[id] {
                if let path = entry["path"] as? String { parts.insert(path, at: 0) }
                current = parent[id]
            }
            return parts.joined(separator: "/")
        }

        func outside(_ configurationList: Any?) -> [String] {
            let configurations = ids(object(configurationList)?["buildConfigurations"]).compactMap { objects[$0] }
            let keys = Set(configurations.flatMap { (($0["buildSettings"] as? [String: Any]) ?? [:]).keys })
            return Self.outsideSettingNames.filter { keys.contains($0) }
        }

        // Which target makes which product file.
        var productOwner: [String: String] = [:]
        let nativeTargets = ids(root["targets"]).compactMap { id in objects[id].map { (id, $0) } }
        for (_, target) in nativeTargets {
            if let product = target["productReference"] as? String, let name = target["name"] as? String {
                productOwner[product] = name
            }
        }

        var targets: [Target] = []
        for (_, target) in nativeTargets {
            guard let name = target["name"] as? String else { throw MapError.malformed("a target without a name") }
            let kind: Kind =
                switch target["productType"] as? String {
                case "com.apple.product-type.framework": .framework
                case "com.apple.product-type.application": .application
                case "com.apple.product-type.bundle.unit-test": .test
                case let other: .other(other ?? "unknown")
                }
            let groups = ids(target["fileSystemSynchronizedGroups"])
            let exceptions = groups.contains { !ids(objects[$0]?["exceptions"]).isEmpty }

            var dependencies: [Dependency] = []
            func depend(_ dependency: Dependency) {
                if !dependencies.contains(dependency) { dependencies.append(dependency) }
            }
            for id in ids(target["dependencies"]) {
                let entry = objects[id]
                if let other = object(entry?["target"])?["name"] as? String {
                    depend(Dependency(name: other, isProduct: false))
                } else if let product = object(entry?["productRef"])?["productName"] as? String {
                    depend(Dependency(name: product, isProduct: true))
                } else if let remote = object(entry?["targetProxy"])?["remoteInfo"] as? String {
                    depend(Dependency(name: remote, isProduct: false))
                }
            }
            for id in ids(target["packageProductDependencies"]) {
                depend(Dependency(name: (objects[id]?["productName"] as? String) ?? id, isProduct: true))
            }

            var linked: [String] = []
            var embedded: [String] = []
            for phase in ids(target["buildPhases"]).compactMap({ objects[$0] }) {
                let isa = phase["isa"] as? String
                let embeds = isa == "PBXCopyFilesBuildPhase" && (phase["dstSubfolderSpec"] as? String) == "10"
                guard isa == "PBXFrameworksBuildPhase" || embeds else { continue }
                for file in ids(phase["files"]).compactMap({ objects[$0] }) {
                    if let product = object(file["productRef"])?["productName"] as? String {
                        depend(Dependency(name: product, isProduct: true))
                    } else if let reference = file["fileRef"] as? String, let owner = productOwner[reference] {
                        if embeds { embedded.append(owner) } else { linked.append(owner) }
                    }
                }
            }
            for name in linked { depend(Dependency(name: name, isProduct: false)) }

            targets.append(
                Target(
                    name: name, kind: kind, paths: groups.map(fullPath(of:)), dependencies: dependencies,
                    linked: linked, embedded: embedded, outsideSettings: outside(target["buildConfigurationList"]),
                    hasMembershipExceptions: exceptions))
        }

        self.init(
            targets: targets, packageReferences: ids(root["packageReferences"]).count,
            projectOutsideSettings: outside(root["buildConfigurationList"]),
            schemeTestables: schemeXML.map(Self.testables(inScheme:)))
    }

    /// The `BlueprintName`s of a scheme's `<Testables>`, skipped ones left out.
    static func testables(inScheme xml: String) -> [String] {
        guard let start = xml.range(of: "<Testables>"), let end = xml.range(of: "</Testables>") else { return [] }
        let block = String(xml[start.upperBound..<end.lowerBound])
        return block.components(separatedBy: "<TestableReference").dropFirst().compactMap { reference in
            guard !reference.contains("skipped = \"YES\""),
                let open = reference.range(of: "BlueprintName = \""),
                let close = reference[open.upperBound...].firstIndex(of: "\"")
            else { return nil }
            return String(reference[open.upperBound..<close])
        }
    }
}
