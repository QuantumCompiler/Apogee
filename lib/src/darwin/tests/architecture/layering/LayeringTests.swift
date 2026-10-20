import Foundation
import Testing

/// The app's directory, lib/src/darwin: this file is
/// tests/architecture/layering/LayeringTests.swift.
private let appRoot = URL(fileURLWithPath: #filePath)
    .deletingLastPathComponent().deletingLastPathComponent()
    .deletingLastPathComponent().deletingLastPathComponent()

private let project = appRoot.appendingPathComponent("Apogee.xcodeproj")

@Suite("The layering check, on this project")
struct LayeringOnThisProjectTests {
    @Test("Apogee.xcodeproj and the sources hold ADR 0001 and ADR 0002")
    func thisProjectHoldsTheLaw() throws {
        let map = try ModuleMap(projectAt: project)
        let violations = LayeringCheck(map: map, tree: DiskTree(root: appRoot)).violations()
        #expect(violations.isEmpty, "\(violations.joined(separator: "\n"))")
    }

    @Test("every layer has a module, so the shape is real rather than aspirational")
    func everyLayerIsPresent() throws {
        let map = try ModuleMap(projectAt: project)
        let layers = Set(
            map.targets.filter { $0.kind == .framework }
                .compactMap { $0.path?.split(separator: "/").dropFirst().first.map(String.init) })
        for layer in LayeringCheck.layers {
            #expect(layers.contains(layer), "no module in \(layer)")
        }
    }

    @Test("a dependency planted in the real project file fails as an upward edge")
    func plantedProjectFails() throws {
        let data = try Data(contentsOf: project.appendingPathComponent("project.pbxproj"))
        var plist = try #require(
            try PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any])
        var objects = try #require(plist["objects"] as? [String: [String: Any]])
        func targetID(_ name: String) throws -> String {
            try #require(objects.first { $0.value["isa"] as? String == "PBXNativeTarget" && $0.value["name"] as? String == name }?.key)
        }
        // DataProtocol made to depend on BusinessSession, as Xcode would write it.
        let dataProtocol = try targetID("DataProtocol")
        objects["PLANTED0000000000000DEP1"] = [
            "isa": "PBXTargetDependency", "target": try targetID("BusinessSession"),
        ]
        let dependencies = (objects[dataProtocol]?["dependencies"] as? [String]) ?? []
        objects[dataProtocol]?["dependencies"] = dependencies + ["PLANTED0000000000000DEP1"]
        plist["objects"] = objects
        let scheme = try String(
            contentsOf: project.appendingPathComponent("xcshareddata/xcschemes/Apogee.xcscheme"), encoding: .utf8)
        let map = try ModuleMap(projectPlist: plist, schemeXML: scheme)
        let violations = LayeringCheck(map: map, tree: DiskTree(root: appRoot)).violations()
        #expect(violations == [
            "upward edge: DataProtocol (data) depends on BusinessSession (business) -- dependencies point down"
        ])
    }

    @Test("the scheme's testables are read by name, a skipped one left out")
    func schemeTestables() {
        let xml = """
            <TestAction>
               <Testables>
                  <TestableReference
                     skipped = "NO">
                     <BuildableReference
                        BlueprintName = "DataProtocolTests">
                     </BuildableReference>
                  </TestableReference>
                  <TestableReference
                     skipped = "YES">
                     <BuildableReference
                        BlueprintName = "BusinessSessionTests">
                     </BuildableReference>
                  </TestableReference>
               </Testables>
            </TestAction>
            """
        #expect(ModuleMap.testables(inScheme: xml) == ["DataProtocolTests"])
    }
}

/// A clean project in miniature -- the four layers, the app, styles, the entry
/// point and their tests -- that each test plants one violation into.
private struct Planted {
    var targets: [ModuleMap.Target] = [
        framework("InfrastructureProcess", "source/infrastructure/process"),
        framework("DataProtocol", "source/data/protocol"),
        framework("BusinessSession", "source/business/session", ["InfrastructureProcess", "DataProtocol"]),
        framework("PresentationStyles", "source/presentation/styles"),
        framework("PresentationApp", "source/presentation/app", ["PresentationStyles", "BusinessSession"]),
        ModuleMap.Target(
            name: "Apogee", kind: .application, paths: ["source/main"],
            dependencies: [ModuleMap.Dependency(name: "PresentationApp", isProduct: false)],
            linked: ["PresentationApp"],
            embedded: ["PresentationApp", "PresentationStyles", "BusinessSession", "InfrastructureProcess", "DataProtocol"]),
        test("InfrastructureProcessTests", "tests/infrastructure/process"),
        test("DataProtocolTests", "tests/data/protocol"),
        test("BusinessSessionTests", "tests/business/session"),
        test("PresentationStylesTests", "tests/presentation/styles"),
        test("PresentationAppTests", "tests/presentation/app"),
        test("ArchitectureLayeringTests", "tests/architecture/layering"),
    ]
    var packageReferences = 0
    var schemeTestables: [String]? = [
        "InfrastructureProcessTests", "DataProtocolTests", "BusinessSessionTests", "PresentationStylesTests",
        "PresentationAppTests", "ArchitectureLayeringTests",
    ]
    var files: [String: String] = [
        "source/infrastructure/process/ChildCommand.swift": "import Foundation\n",
        "source/data/protocol/MachineProtocol.swift": "public enum MachineProtocol {}\n",
        "source/business/session/ChatLaunch.swift": "import DataProtocol\nimport InfrastructureProcess\n",
        "source/presentation/styles/Palette.swift": "import AppKit\nimport SwiftUI\n",
        "source/presentation/app/Views/ApogeeApp.swift": "import PresentationStyles\nimport SwiftUI\n",
        "source/presentation/app/ViewModels/AppViewModel.swift": "import BusinessSession\nimport Observation\n",
        "source/main/ApogeeMain.swift": "import PresentationApp\n",
        "source/infrastructure/CLAUDE.md": "# Infrastructure\n\n**Modules:** `process` -- the child.\n",
        "source/data/CLAUDE.md": "# Data\n\n**Modules:** `protocol`.\n",
        "source/business/CLAUDE.md": "# Business\n\n**Modules:** `session`.\n",
        "source/presentation/CLAUDE.md": "# Presentation\n\n**Modules:** `styles`, `app`.\n",
    ]

    /// A framework that links -- and so depends on -- `links`.
    static func framework(_ name: String, _ path: String, _ links: [String] = []) -> ModuleMap.Target {
        ModuleMap.Target(
            name: name, kind: .framework, paths: [path],
            dependencies: links.map { ModuleMap.Dependency(name: $0, isProduct: false) }, linked: links)
    }

    static func test(_ name: String, _ path: String) -> ModuleMap.Target {
        ModuleMap.Target(name: name, kind: .test, paths: [path])
    }

    func index(_ name: String) -> Int { targets.firstIndex { $0.name == name }! }

    /// `name` with its fields changed by `change`.
    mutating func change(_ name: String, _ change: (inout Fields) -> Void) {
        let target = targets[index(name)]
        var fields = Fields(target)
        change(&fields)
        targets[index(name)] = fields.target(name: name, kind: target.kind)
    }

    struct Fields {
        var paths: [String]
        var dependencies: [ModuleMap.Dependency]
        var linked: [String]
        var embedded: [String]
        var outsideSettings: [String]
        var hasMembershipExceptions: Bool

        init(_ target: ModuleMap.Target) {
            paths = target.paths
            dependencies = target.dependencies
            linked = target.linked
            embedded = target.embedded
            outsideSettings = target.outsideSettings
            hasMembershipExceptions = target.hasMembershipExceptions
        }

        func target(name: String, kind: ModuleMap.Kind) -> ModuleMap.Target {
            ModuleMap.Target(
                name: name, kind: kind, paths: paths, dependencies: dependencies, linked: linked,
                embedded: embedded, outsideSettings: outsideSettings, hasMembershipExceptions: hasMembershipExceptions)
        }
    }

    func violations() -> [String] {
        LayeringCheck(
            map: ModuleMap(targets: targets, packageReferences: packageReferences, schemeTestables: schemeTestables),
            tree: PlantedTree(files: files)
        ).violations()
    }
}

@Suite("The layering check, on planted violations")
struct LayeringPlantedTests {
    @Test("the miniature is clean before anything is planted")
    func baselineIsClean() {
        #expect(Planted().violations() == [])
    }

    @Test("an empty map is refused, never passed")
    func emptyMapRefused() {
        var planted = Planted()
        planted.targets = []
        #expect(planted.violations() == [
            "the module map is empty: Apogee.xcodeproj declares no targets, and an empty map proves nothing"
        ])
    }

    @Test("an upward edge fails, naming both targets and their layers")
    func upwardEdge() {
        var planted = Planted()
        planted.change("DataProtocol") { $0.dependencies.append(.init(name: "BusinessSession", isProduct: false)) }
        #expect(planted.violations() == [
            "upward edge: DataProtocol (data) depends on BusinessSession (business) -- dependencies point down"
        ])
    }

    @Test("a SwiftUI import in a ViewModels/ file fails")
    func swiftUIInViewModel() {
        var planted = Planted()
        planted.files["source/presentation/app/ViewModels/AppViewModel.swift"] = "import Foundation\nimport SwiftUI\n"
        #expect(planted.violations() == [
            "source/presentation/app/ViewModels/AppViewModel.swift imports SwiftUI: a view-model stays free of UI frameworks, so it tests headlessly"
        ])
    }

    @Test("styles leaning on a sibling Presentation module fails")
    func stylesOnSibling() {
        var planted = Planted()
        planted.change("PresentationStyles") { $0.dependencies.append(.init(name: "PresentationApp", isProduct: false)) }
        #expect(planted.violations() == [
            "PresentationStyles depends on its sibling PresentationApp: styles is the Presentation layer's floor and leans on no other module"
        ])
    }

    @Test("a UI framework below Presentation fails")
    func uiFrameworkInBusiness() {
        var planted = Planted()
        planted.files["source/business/session/ChatLaunch.swift"] = "@preconcurrency import AppKit\n"
        #expect(planted.violations() == [
            "source/business/session/ChatLaunch.swift imports AppKit: UI frameworks stay in Presentation, and BusinessSession is business"
        ])
    }

    @Test("an import of an undeclared module of the project fails")
    func undeclaredImport() {
        var planted = Planted()
        planted.files["source/data/protocol/MachineProtocol.swift"] = "import InfrastructureProcess\n"
        #expect(planted.violations() == [
            "source/data/protocol/MachineProtocol.swift imports InfrastructureProcess, which DataProtocol does not declare as a dependency"
        ])
    }

    @Test("anything from outside the project fails: a package, a product, a search path, a linker flag")
    func outsideTheProject() {
        var planted = Planted()
        planted.packageReferences = 1
        planted.change("BusinessSession") { $0.dependencies.append(.init(name: "ApogeeCLI", isProduct: true)) }
        planted.change("InfrastructureProcess") { $0.outsideSettings = ["LIBRARY_SEARCH_PATHS", "OTHER_LDFLAGS"] }
        #expect(planted.violations() == [
            "Apogee.xcodeproj references 1 Swift package(s): the app links nothing outside this project",
            "InfrastructureProcess sets LIBRARY_SEARCH_PATHS: the app links nothing it does not build",
            "InfrastructureProcess sets OTHER_LDFLAGS: the app links nothing it does not build",
            "BusinessSession depends on the package product ApogeeCLI: the app links nothing outside this project",
        ])
    }

    @Test("a module outside source/<layer>/<package>/, misnamed, or not one folder whole, fails")
    func placementAndNaming() {
        var planted = Planted()
        planted.targets[planted.index("DataProtocol")] = Planted.framework("Protocol", "source/data/protocol")
        planted.targets.append(Planted.framework("Helpers", "source/util/helpers"))
        planted.change("InfrastructureProcess") { $0.hasMembershipExceptions = true }
        #expect(planted.violations() == [
            "InfrastructureProcess excludes files from its folder: the directory is the module, whole",
            "Protocol is at source/data/protocol, so it must be named DataProtocol",
            "Helpers is at source/util/helpers, not source/<layer>/<package>/ with a layer of infrastructure, data, business, presentation",
            "BusinessSession depends on DataProtocol, which is not a module of this map",
            "Apogee embeds DataProtocol, which nothing it links needs",
            "DataProtocolTests is at tests/data/protocol, so it must be named ProtocolTests",
        ])
    }

    @Test("a Presentation module without the MVVM shape fails")
    func mvvmShape() {
        var planted = Planted()
        planted.files.removeValue(forKey: "source/presentation/app/ViewModels/AppViewModel.swift")
        planted.files["source/presentation/app/Loose.swift"] = "import SwiftUI\n"
        #expect(planted.violations() == [
            "PresentationApp has no ViewModels/ with a Swift file in it: a Presentation module is Views/ + ViewModels/",
            "source/presentation/app/Loose.swift sits at the root of PresentationApp: its files live in Views/, ViewModels/ or a directory of their own",
        ])
    }

    @Test("an application missing a framework it needs at run time fails, and so does one embedding a stray")
    func applicationEmbeds() {
        var planted = Planted()
        planted.change("Apogee") {
            $0.embedded.removeAll { $0 == "InfrastructureProcess" }
        }
        planted.targets.append(Planted.framework("PresentationExtra", "source/presentation/extra"))
        planted.files["source/presentation/extra/Views/Extra.swift"] = "import SwiftUI\n"
        planted.files["source/presentation/extra/ViewModels/ExtraModel.swift"] = "import Foundation\n"
        planted.files["source/presentation/CLAUDE.md"] = "**Modules:** `styles`, `app`, `extra`.\n"
        planted.targets.append(Planted.test("PresentationExtraTests", "tests/presentation/extra"))
        planted.schemeTestables?.append("PresentationExtraTests")
        planted.change("Apogee") { $0.embedded.append("PresentationExtra") }
        #expect(planted.violations() == [
            "Apogee does not embed InfrastructureProcess, which it needs at run time: the app would not launch",
            "Apogee embeds PresentationExtra, which nothing it links needs",
        ])
    }

    @Test("the entry point is one file at source/main/, importing only what the application declares")
    func entryPoint() {
        var planted = Planted()
        planted.files["source/main/Helpers.swift"] = "import BusinessSession\n"
        #expect(planted.violations() == [
            "source/main/ holds 2 Swift files: the entry point is one file, and the app itself is PresentationApp",
            "source/main/Helpers.swift imports BusinessSession, which Apogee does not declare as a dependency",
        ])
    }

    @Test("a module without its mirroring test bundle fails, and so does a misnamed one")
    func testsMirror() {
        var planted = Planted()
        planted.targets.removeAll { $0.name == "DataProtocolTests" }
        planted.schemeTestables?.removeAll { $0 == "DataProtocolTests" }
        planted.targets.append(Planted.test("SessionTests", "tests/business/session"))
        planted.targets.append(Planted.test("ArchitectureStrayTests", "tests/architecture/other/deeper"))
        planted.schemeTestables? += ["SessionTests", "ArchitectureStrayTests"]
        #expect(planted.violations() == [
            "SessionTests is at tests/business/session, so it must be named BusinessSessionTests",
            "ArchitectureStrayTests is at tests/architecture/other/deeper: an architecture check sits directly under tests/architecture/",
            "DataProtocol has no test bundle at tests/data/protocol/",
        ])
    }

    @Test("a test bundle the shared scheme does not run fails, and so does a missing scheme")
    func schemeRunsEveryBundle() {
        var planted = Planted()
        planted.schemeTestables?.removeAll { $0 == "BusinessSessionTests" }
        #expect(planted.violations() == [
            "BusinessSessionTests is not a testable of the shared scheme Apogee: its tests would never run"
        ])
        planted.schemeTestables = nil
        #expect(planted.violations() == [
            "the shared scheme Apogee (xcshareddata/xcschemes/Apogee.xcscheme) is missing: it is what runs the tests"
        ])
    }

    @Test("a layer card that misses a module, or is missing, fails")
    func layerCards() {
        var planted = Planted()
        planted.files["source/presentation/CLAUDE.md"] = "**Modules:** `app`.\n"
        planted.files.removeValue(forKey: "source/data/CLAUDE.md")
        #expect(planted.violations() == [
            "source/data/CLAUDE.md is missing: every layer carries its card",
            "source/presentation/CLAUDE.md names app but the layer's modules are app, styles",
        ])
    }

    @Test("imports are read in every form Swift writes them")
    func importForms() {
        let source = """
            import Foundation
            @testable import PresentationApp
            @preconcurrency import AppKit
            public import DataProtocol
            import struct SwiftUI.Color
              import Observation // indented
            // import NotThis
            let text = "import NorThis"
            """
        #expect(LayeringCheck.importedModules(in: source) == [
            "Foundation", "PresentationApp", "AppKit", "DataProtocol", "SwiftUI", "Observation",
        ])
    }
}
