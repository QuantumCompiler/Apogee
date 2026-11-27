import Foundation

/// ADR 0001, ADR 0002 and ADR 0004, mechanically: the module map and the
/// sources held to the four layers, the downward law, Presentation's MVVM
/// shape, and its modules' schemes and previews.
///
/// The map is `Apogee.xcodeproj`. A module is a framework target building one
/// folder, `source/<layer>/<package>/`, and named `<Layer><Package>` -- where
/// it sits is its declaration -- and its tests a unit-test bundle building
/// `tests/<layer>/<package>/`, named `<Layer><Package>Tests`. The application
/// target, `Apogee`, is the composition root at `source/main/`, as the CLI's
/// `source/main.cpp` is: one file, the entry point. The rules, each a violation
/// naming what broke it:
///
/// - the map is not empty; every target builds one folder, with no membership
///   exceptions, and sits and is named as above;
/// - a module depends only on modules of its own layer or a lower one, and
///   nothing in the project reaches outside it -- no Swift package, no search
///   path or linker flag (the app links nothing of the CLI's);
/// - a source file imports another module of the project only when its target
///   declares the dependency;
/// - a UI framework is imported only in Presentation (the entry point counts),
///   and never in a `ViewModels/` file;
/// - every Presentation module but `styles` has `Views/` and `ViewModels/`,
///   with its files inside them; `styles` depends on no sibling;
/// - the application embeds exactly the frameworks it needs at run time;
/// - every module has its mirroring test bundle, and the shared scheme runs
///   every test bundle;
/// - every Presentation module has its own shared scheme, named for it,
///   building it and running its test bundle alone (ADR 0004);
/// - every view a Presentation file declares has a `#Preview` in that file;
///   previews and `Previews/` support sit inside `#if DEBUG`, use no
///   `PreviewProvider`, and never name a real starter (ADR 0004);
/// - every `.confirmationDialog` in a Presentation module's `Views/` hands
///   each `Task` its buttons start the answer the dialog was built with: a
///   `confirm…` call there takes, first, a name the dialog bound outside the
///   `Task` (`if let answer = …`, a `case … (let …)`, or the actions'
///   parameter), and no `Task` there reads the dialog's question back -- the
///   dialog's closing clears it before the `Task` runs (GUI M20);
/// - every argv line under `source/` is the builder's: an array whose first
///   word is a command word holds string literals alone, nothing but such
///   literals is added to it or to a name declared from it, and nothing is
///   added to a line `Argv` built -- so a name, path or id reaches the verb
///   after `--` (GUI M23);
/// - each layer's card, `source/<layer>/CLAUDE.md`, names exactly its modules.
struct LayeringCheck {
    /// Lowest first: a module may depend on its own layer or any before it.
    static let layers = ["infrastructure", "data", "business", "presentation"]

    /// Frameworks that draw. Presentation's alone (ADR 0001), and never a
    /// view-model's (ADR 0002).
    static let uiFrameworks: Set<String> = [
        "SwiftUI", "SwiftUICore", "AppKit", "UIKit", "Cocoa", "Charts", "WidgetKit",
    ]

    /// The application target and the one folder it builds.
    static let applicationName = "Apogee"
    static let applicationPath = "source/main"

    /// Test bundles that mirror no module: the architecture's own checks.
    static let architectureTests = "tests/architecture/"

    /// The Presentation module that is the layer's own floor.
    static let stylesPackage = "styles"

    /// What a preview may never name: the starters and runners that reach the
    /// real binary or the person's machine (ADR 0004).
    static let realStarters = ["ApogeeClient", "ReadRunner", "JobStarter", "ProcessSpawner"]

    let map: ModuleMap
    let tree: SourceTree

    /// Every violation, in a stable order; empty when the project holds the law.
    func violations() -> [String] {
        guard !map.targets.isEmpty else {
            return ["the module map is empty: Apogee.xcodeproj declares no targets, and an empty map proves nothing"]
        }
        var found: [String] = []
        found += folders()
        let modules = sourceModules(&found)
        guard !modules.isEmpty else {
            return found + ["the module map has no framework targets at source/<layer>/<package>/"]
        }
        let byName = Dictionary(uniqueKeysWithValues: modules.map { ($0.target.name, $0) })

        found += outside()
        for module in modules {
            found += edges(of: module, byName: byName)
            found += imports(
                of: module.target, at: module.path, layer: module.layer, byName: byName)
            if module.layer == "presentation" && module.package != Self.stylesPackage {
                found += mvvmShape(of: module)
            }
            if module.layer == "presentation" {
                found += moduleScheme(of: module)
                found += previews(of: module)
                found += dialogs(of: module)
            }
        }
        found += application(byName: byName)
        found += tests(for: modules)
        found += scheme()
        found += cards(for: modules)
        found += argvLines()
        return found
    }

    // MARK: - Folders and placement

    /// Every target builds one folder, whole.
    private func folders() -> [String] {
        var found: [String] = []
        for target in map.targets {
            if target.paths.count != 1 {
                found.append(
                    "\(target.name) builds \(target.paths.count) folders: every target builds exactly one, its directory")
            }
            if target.hasMembershipExceptions {
                found.append("\(target.name) excludes files from its folder: the directory is the module, whole")
            }
        }
        return found
    }

    struct Module {
        let target: ModuleMap.Target
        let layer: String
        let package: String
        var path: String { "source/\(layer)/\(package)" }
        var rank: Int { LayeringCheck.layers.firstIndex(of: layer)! }
    }

    /// The framework targets, each placed and named by the rules; one that is
    /// not is reported and left out.
    private func sourceModules(_ found: inout [String]) -> [Module] {
        var modules: [Module] = []
        for target in map.targets {
            switch target.kind {
            case .test, .application:
                continue
            case .other(let kind):
                found.append("\(target.name) is a \(kind) target: the map holds frameworks, the application and test bundles")
                continue
            case .framework:
                break
            }
            guard let path = target.path else { continue }
            let parts = path.split(separator: "/").map(String.init)
            guard parts.count == 3, parts[0] == "source", Self.layers.contains(parts[1]) else {
                found.append(
                    "\(target.name) is at \(path), not source/<layer>/<package>/ with a layer of \(Self.layers.joined(separator: ", "))")
                continue
            }
            let module = Module(target: target, layer: parts[1], package: parts[2])
            let expected = Self.moduleName(layer: module.layer, package: module.package)
            if target.name != expected {
                found.append("\(target.name) is at \(path), so it must be named \(expected)")
            }
            modules.append(module)
        }
        return modules
    }

    /// `presentation`, `styles` -> `PresentationStyles`; `data`, `json-reads` -> `DataJsonReads`.
    static func moduleName(layer: String, package: String) -> String {
        ([layer] + package.split(separator: "-").map(String.init))
            .map { $0.prefix(1).uppercased() + $0.dropFirst() }
            .joined()
    }

    // MARK: - Nothing outside the project

    private func outside() -> [String] {
        var found: [String] = []
        if map.packageReferences > 0 {
            found.append(
                "Apogee.xcodeproj references \(map.packageReferences) Swift package(s): the app links nothing outside this project")
        }
        for setting in map.projectOutsideSettings {
            found.append("the project sets \(setting): the app links nothing it does not build")
        }
        for target in map.targets {
            for setting in target.outsideSettings {
                found.append("\(target.name) sets \(setting): the app links nothing it does not build")
            }
            for dependency in target.dependencies where dependency.isProduct {
                found.append(
                    "\(target.name) depends on the package product \(dependency.name): the app links nothing outside this project")
            }
        }
        return found
    }

    // MARK: - Edges

    private func edges(of module: Module, byName: [String: Module]) -> [String] {
        var found: [String] = []
        let name = module.target.name
        for dependency in module.target.dependencies where !dependency.isProduct {
            guard let other = byName[dependency.name] else {
                found.append("\(name) depends on \(dependency.name), which is not a module of this map")
                continue
            }
            if other.rank > module.rank {
                found.append(
                    "upward edge: \(name) (\(module.layer)) depends on \(other.target.name) (\(other.layer)) -- dependencies point down")
            }
            if module.layer == "presentation" && module.package == Self.stylesPackage && other.layer == "presentation"
                && other.package != Self.stylesPackage
            {
                found.append(
                    "\(name) depends on its sibling \(other.target.name): styles is the Presentation layer's floor and leans on no other module")
            }
        }
        return found
    }

    // MARK: - Imports

    /// `layer` is the importing target's: a module's, or `presentation` for the
    /// entry point.
    private func imports(
        of target: ModuleMap.Target, at path: String, layer: String, byName: [String: Module]
    ) -> [String] {
        var found: [String] = []
        let declared = Set(target.dependencies.map(\.name))
        for file in tree.swiftFiles(under: path) {
            let inViewModels = file.dropFirst(path.count + 1).hasPrefix("ViewModels/")
            for imported in Self.importedModules(in: tree.contents(of: file) ?? "") {
                if byName[imported] != nil && imported != target.name && !declared.contains(imported) {
                    found.append("\(file) imports \(imported), which \(target.name) does not declare as a dependency")
                }
                if Self.uiFrameworks.contains(imported) {
                    if layer != "presentation" {
                        found.append(
                            "\(file) imports \(imported): UI frameworks stay in Presentation, and \(target.name) is \(layer)")
                    } else if inViewModels {
                        found.append(
                            "\(file) imports \(imported): a view-model stays free of UI frameworks, so it tests headlessly")
                    }
                }
            }
        }
        return found
    }

    /// The modules a Swift file imports: `import X`, `@testable import X`,
    /// `public import X`, `import struct X.Y` ...
    static func importedModules(in source: String) -> [String] {
        let pattern =
            #"^[ \t]*(?:@[A-Za-z_]+(?:\([^)]*\))?[ \t]+)*(?:(?:public|package|internal|fileprivate|private)[ \t]+)?import[ \t]+(?:(?:typealias|struct|class|enum|protocol|let|var|func)[ \t]+)?([A-Za-z_][A-Za-z0-9_]*)"#
        let regex = try! NSRegularExpression(pattern: pattern, options: [.anchorsMatchLines])
        let range = NSRange(source.startIndex..., in: source)
        return regex.matches(in: source, range: range).compactMap {
            Range($0.range(at: 1), in: source).map { String(source[$0]) }
        }
    }

    // MARK: - Presentation's shape

    private func mvvmShape(of module: Module) -> [String] {
        var found: [String] = []
        let name = module.target.name
        for required in ["Views", "ViewModels"]
        where tree.swiftFiles(under: "\(module.path)/\(required)").isEmpty {
            found.append("\(name) has no \(required)/ with a Swift file in it: a Presentation module is Views/ + ViewModels/")
        }
        for file in tree.swiftFiles(under: module.path)
        where !file.dropFirst(module.path.count + 1).contains("/") {
            found.append("\(file) sits at the root of \(name): its files live in Views/, ViewModels/ or a directory of their own")
        }
        return found
    }

    // MARK: - The application

    /// One application target, `Apogee` at `source/main/`: the entry point
    /// alone, embedding exactly the frameworks its links reach.
    private func application(byName: [String: Module]) -> [String] {
        let applications = map.targets.filter { $0.kind == .application }
        guard applications.count == 1, let app = applications.first else {
            return ["the project has \(applications.count) application targets: it has one, \(Self.applicationName)"]
        }
        var found: [String] = []
        if app.name != Self.applicationName {
            found.append("the application target is \(app.name): it is named \(Self.applicationName)")
        }
        if let path = app.path, path != Self.applicationPath {
            found.append("\(app.name) builds \(path): the entry point lives at \(Self.applicationPath)/")
        }
        let files = tree.swiftFiles(under: Self.applicationPath)
        if files.count != 1 {
            found.append(
                "\(Self.applicationPath)/ holds \(files.count) Swift files: the entry point is one file, and the app itself is PresentationApp")
        }
        found += imports(of: app, at: Self.applicationPath, layer: "presentation", byName: byName)

        // What dyld will look for in Contents/Frameworks: every framework the
        // application links, and every one those link in turn.
        var needed: [String] = []
        func reach(_ name: String) {
            guard !needed.contains(name), let module = byName[name] else { return }
            needed.append(name)
            module.target.linked.forEach(reach)
        }
        app.linked.forEach(reach)
        for missing in needed where !app.embedded.contains(missing) {
            found.append("\(app.name) does not embed \(missing), which it needs at run time: the app would not launch")
        }
        for extra in app.embedded where !needed.contains(extra) {
            found.append("\(app.name) embeds \(extra), which nothing it links needs")
        }
        return found
    }

    // MARK: - Tests mirror the source

    private func tests(for modules: [Module]) -> [String] {
        var found: [String] = []
        var mirrored: Set<String> = []
        let byPath = Dictionary(uniqueKeysWithValues: modules.map { ($0.path, $0) })
        for target in map.targets where target.kind == .test {
            guard let path = target.path else { continue }
            if path.hasPrefix(Self.architectureTests) {
                let check = String(path.dropFirst(Self.architectureTests.count))
                if check.isEmpty || check.contains("/") {
                    found.append(
                        "\(target.name) is at \(path): an architecture check sits directly under \(Self.architectureTests)")
                    continue
                }
                let expected = Self.moduleName(layer: "architecture", package: check) + "Tests"
                if target.name != expected {
                    found.append("\(target.name) is at \(path), so it must be named \(expected)")
                }
                continue
            }
            let parts = path.split(separator: "/").map(String.init)
            guard parts.count == 3, parts[0] == "tests",
                let source = byPath["source/\(parts[1])/\(parts[2])"]
            else {
                found.append("\(target.name) is at \(path), which mirrors no source/<layer>/<package>/ module")
                continue
            }
            let expected = source.target.name + "Tests"
            if target.name != expected {
                found.append("\(target.name) is at \(path), so it must be named \(expected)")
            }
            mirrored.insert(source.target.name)
        }
        for module in modules where !mirrored.contains(module.target.name) {
            found.append("\(module.target.name) has no test bundle at tests/\(module.layer)/\(module.package)/")
        }
        return found
    }

    /// The shared scheme runs every test bundle, so none is built and never run.
    private func scheme() -> [String] {
        guard let testables = map.schemeTestables else {
            return ["the shared scheme Apogee (xcshareddata/xcschemes/Apogee.xcscheme) is missing: it is what runs the tests"]
        }
        return map.targets.filter { $0.kind == .test && !testables.contains($0.name) }.map {
            "\($0.name) is not a testable of the shared scheme Apogee: its tests would never run"
        }
    }

    // MARK: - Schemes and previews (ADR 0004)

    /// A Presentation module's own shared scheme: named for it, building it,
    /// running its test bundle alone.
    private func moduleScheme(of module: Module) -> [String] {
        let name = module.target.name
        guard let scheme = map.schemes[name] else {
            return ["\(name) has no shared scheme (xcshareddata/xcschemes/\(name).xcscheme): a Presentation module is built, previewed and tested on its own"]
        }
        var found: [String] = []
        if !scheme.built.contains(name) {
            found.append("the scheme \(name) builds \(scheme.built.joined(separator: ", ")), not \(name)")
        }
        if scheme.tested != [name + "Tests"] {
            found.append("the scheme \(name) runs \(scheme.tested.isEmpty ? "no tests" : scheme.tested.joined(separator: ", ")): it runs \(name)Tests alone")
        }
        return found
    }

    /// Every view a file declares has a `#Preview` in it; previews and their
    /// support stay debug-only, macro-only, and hermetic.
    private func previews(of module: Module) -> [String] {
        var found: [String] = []
        for file in tree.swiftFiles(under: module.path) {
            let source = tree.contents(of: file) ?? ""
            let lines = source.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
            if source.contains("PreviewProvider") {
                found.append("\(file) uses PreviewProvider: a view is previewed with #Preview")
            }
            if file.dropFirst(module.path.count + 1).hasPrefix("Previews/") {
                let content = lines.map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
                if content.first != "#if DEBUG" || content.last != "#endif" {
                    found.append("\(file) is preview support outside #if DEBUG: previews never ship")
                }
                found += Self.realStarters.filter { Self.names($0, in: source) }.map {
                    "\(file) names \($0): a preview never starts the binary"
                }
                continue
            }
            let views = Self.declaredViews(in: source)
            guard let first = source.range(of: "#Preview") else {
                found += views.map { "\(file) declares \($0) with no #Preview: every view a module draws is previewable" }
                continue
            }
            let previews = String(source[first.lowerBound...])
            for view in views where !Self.constructs(view, in: previews) {
                found.append("\(file): \(view) has no #Preview")
            }
            var debug = false
            for line in lines {
                let trimmed = line.trimmingCharacters(in: .whitespaces)
                if trimmed == "#if DEBUG" { debug = true } else if trimmed == "#endif" { debug = false }
                if trimmed.hasPrefix("#Preview") && !debug {
                    found.append("\(file) has a #Preview outside #if DEBUG: previews never ship")
                    break
                }
            }
            found += Self.realStarters.filter { Self.names($0, in: previews) }.map {
                "\(file) names \($0) in its previews: a preview never starts the binary"
            }
        }
        return found
    }

    /// The views a file declares at its top level, private helpers left out:
    /// a `struct` conforming to `View` or a representable.
    static func declaredViews(in source: String) -> [String] {
        let pattern = #"^(?:public |internal |package )?struct ([A-Za-z_][A-Za-z0-9_]*)(?:<[^\n{]*?>)?\s*:\s*([^{\n]*)\{"#
        let regex = try! NSRegularExpression(pattern: pattern, options: [.anchorsMatchLines])
        let range = NSRange(source.startIndex..., in: source)
        return regex.matches(in: source, range: range).compactMap { match in
            guard let name = Range(match.range(at: 1), in: source), let conformances = Range(match.range(at: 2), in: source)
            else { return nil }
            let list = source[conformances]
            let isView = list.split(whereSeparator: { ",: ".contains($0) }).contains {
                $0 == "View" || $0 == "NSViewRepresentable" || $0 == "NSViewControllerRepresentable"
            }
            return isView ? String(source[name]) : nil
        }
    }

    /// Whether `text` builds `view`: `View(`, `View {` or `View<`.
    static func constructs(_ view: String, in text: String) -> Bool {
        let regex = try! NSRegularExpression(pattern: "\\b" + view + #"\s*[({<]"#)
        return regex.firstMatch(in: text, range: NSRange(text.startIndex..., in: text)) != nil
    }

    /// Whether `word` appears in `text` as a word.
    static func names(_ word: String, in text: String) -> Bool {
        let regex = try! NSRegularExpression(pattern: "\\b" + word + "\\b")
        return regex.firstMatch(in: text, range: NSRange(text.startIndex..., in: text)) != nil
    }

    // MARK: - Confirmation dialogs

    /// Every `.confirmationDialog` in the module's `Views/`, its buttons held
    /// to the answer it was built with (GUI M20). On a press SwiftUI runs the
    /// button's action, then closes the dialog -- its `isPresented` binding
    /// set to false, which clears the question in the view-model -- and only
    /// then runs a `Task` the action started: a `Task` that reads the
    /// question back finds nothing, and the confirmed action never runs.
    private func dialogs(of module: Module) -> [String] {
        tree.swiftFiles(under: "\(module.path)/Views").flatMap {
            Self.dialogViolations(in: tree.contents(of: $0) ?? "", file: $0)
        }
    }

    /// What breaks the dialog rule in `source`, each named by `file` and
    /// line: a `confirm…` call in a dialog's `Task` whose first argument is
    /// not a name the dialog bound outside that `Task`, and a dialog's `Task`
    /// that reads the dialog's question -- the property its `isPresented`
    /// reads -- back.
    static func dialogViolations(in source: String, file: String) -> [String] {
        let code = Self.code(source)
        var found: [String] = []
        for opening in Self.matches(#"\.confirmationDialog\s*\("#, in: code, within: code.startIndex..<code.endIndex) {
            let open = code.index(before: opening.range.upperBound)
            guard let close = Self.closing(of: open, in: code) else { continue }
            let arguments = code.index(after: open)..<close
            guard let actions = Self.actions(after: close, arguments: arguments, in: code) else { continue }
            let question = Self.question(in: String(code[arguments]))
            let parameter = Self.matches(#"^\{\s*\(?\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)?\s+in\b"#, in: code, within: actions)
                .first?.groups[0].map { String(code[$0]) }
            for task in Self.matches(#"\bTask\s*(?:\([^()]*\))?\s*\{"#, in: code, within: actions) {
                let brace = code.index(before: task.range.upperBound)
                guard let end = Self.closing(of: brace, in: code) else { continue }
                let body = brace..<code.index(after: end)
                if let question,
                    let read = Self.matches(#"\."# + NSRegularExpression.escapedPattern(for: question) + #"\b"#, in: code, within: body).first
                {
                    found.append(
                        "\(file):\(Self.line(of: read.range.lowerBound, in: code)): a confirmation dialog's Task reads \(question) back -- the dialog's closing clears it before the Task runs; hand the Task the answer captured where the button is built")
                    continue
                }
                let bound = Set(
                    Self.matches(#"\blet\s+([A-Za-z_][A-Za-z0-9_]*)"#, in: code, within: actions.lowerBound..<task.range.lowerBound)
                        .compactMap { $0.groups[0].map { String(code[$0]) } } + (parameter.map { [$0] } ?? []))
                for call in Self.matches(#"\b(confirm[A-Za-z0-9_]*)\s*\("#, in: code, within: body) {
                    guard let name = call.groups[0].map({ String(code[$0]) }), name != "confirmationDialog" else { continue }
                    let paren = code.index(before: call.range.upperBound)
                    let first = Self.firstArgument(after: paren, in: code)
                    if !bound.contains(first) {
                        found.append(
                            "\(file):\(Self.line(of: call.range.lowerBound, in: code)): \(name)(…) in a confirmation dialog's Task is not handed the answer the dialog showed -- its closing clears the question before the Task runs; capture the answer where the button is built (if let answer = …) and hand it in")
                    }
                }
            }
        }
        return found
    }

    /// The dialog's actions: the trailing closure after its arguments, or an
    /// `actions:` closure among them.
    private static func actions(after close: String.Index, arguments: Range<String.Index>, in code: String) -> Range<String.Index>? {
        var next = code.index(after: close)
        while next < code.endIndex, code[next].isWhitespace { next = code.index(after: next) }
        let brace: String.Index
        if next < code.endIndex, code[next] == "{" {
            brace = next
        } else if let labelled = Self.matches(#"\bactions\s*:\s*\{"#, in: code, within: arguments).first {
            brace = code.index(before: labelled.range.upperBound)
        } else {
            return nil
        }
        return Self.closing(of: brace, in: code).map { brace..<code.index(after: $0) }
    }

    /// The question a dialog's `isPresented` reads -- `$model.removing`, or
    /// a binding whose `get` reads `model.pending` -- by its last name.
    private static func question(in arguments: String) -> String? {
        let name = "[A-Za-z_][A-Za-z0-9_]*"
        for pattern in [
            #"isPresented\s*:\s*\$(?:"# + name + #"\.)*("# + name + ")",
            #"isPresented\s*:\s*Binding\s*\(\s*get\s*:\s*\{\s*(?:"# + name + #"\.)*("# + name + ")",
        ] {
            if let match = Self.matches(pattern, in: arguments, within: arguments.startIndex..<arguments.endIndex).first,
                let range = match.groups[0]
            {
                return String(arguments[range])
            }
        }
        return nil
    }

    /// The first argument of the call whose `(` is at `paren`, trimmed: up to
    /// the first comma outside any bracket, or the call's close.
    private static func firstArgument(after paren: String.Index, in code: String) -> String {
        var depth = 0
        var index = code.index(after: paren)
        let start = index
        while index < code.endIndex {
            let character = code[index]
            if "([{".contains(character) {
                depth += 1
            } else if ")]}".contains(character) {
                if depth == 0 { break }
                depth -= 1
            } else if character == ",", depth == 0 {
                break
            }
            index = code.index(after: index)
        }
        return code[start..<index].trimmingCharacters(in: .whitespacesAndNewlines)
    }

    /// The bracket closing the one at `open`, counting that pair alone: once
    /// strings and comments are blank, the code's brackets balance.
    private static func closing(of open: String.Index, in code: String) -> String.Index? {
        let opener = code[open]
        let closer: Character = opener == "(" ? ")" : opener == "[" ? "]" : "}"
        var depth = 0
        var index = open
        while index < code.endIndex {
            if code[index] == opener {
                depth += 1
            } else if code[index] == closer {
                depth -= 1
                if depth == 0 { return index }
            }
            index = code.index(after: index)
        }
        return nil
    }

    /// The 1-based line `index` is on.
    private static func line(of index: String.Index, in code: String) -> Int {
        code[..<index].reduce(1) { $1.isNewline ? $0 + 1 : $0 }
    }

    /// A match in the code: its range, and each group's -- `nil` where a
    /// group took no part.
    private struct Match {
        let range: Range<String.Index>
        let groups: [Range<String.Index>?]
    }

    /// Each match of `pattern` in `text` within `range`.
    private static func matches(_ pattern: String, in text: String, within range: Range<String.Index>) -> [Match] {
        let regex = try! NSRegularExpression(pattern: pattern, options: [.anchorsMatchLines])
        return regex.matches(in: text, range: NSRange(range, in: text)).compactMap { match in
            Range(match.range, in: text).map { whole in
                Match(range: whole, groups: (1..<match.numberOfRanges).map { Range(match.range(at: $0), in: text) })
            }
        }
    }

    /// `source` with its comments and string literals blank -- each of their
    /// characters a space, its newlines kept -- so what is left reads as code:
    /// its brackets balance, a name is a name, and a line is the file's.
    /// Interpolations are blank with the strings they sit in.
    static func code(_ source: String) -> String {
        var masker = Masker(source)
        masker.code(inInterpolation: false)
        return String(masker.out)
    }

    /// The scanner behind `code(_:)`, over the source's characters.
    private struct Masker {
        let characters: [Character]
        var out: [Character]
        var at = 0
        /// Every string literal it blanked, by the characters it spans --
        /// quotes and pounds included -- and whether an interpolation opens
        /// it, so the literal's first characters are not the author's.
        var literals: [(range: Range<Int>, opensWithInterpolation: Bool)] = []

        init(_ source: String) {
            characters = Array(source)
            out = characters
        }

        func starts(_ text: String, at index: Int) -> Bool {
            var index = index
            for character in text {
                guard index < characters.count, characters[index] == character else { return false }
                index += 1
            }
            return true
        }

        mutating func blank(_ range: Range<Int>) {
            for index in range where !characters[index].isNewline { out[index] = " " }
        }

        /// Code from `at` -- in an interpolation, up to the `)` closing it,
        /// left at `at` -- each comment and string in it blanked.
        mutating func code(inInterpolation: Bool) {
            var depth = 0
            while at < characters.count {
                let start = at
                if starts("//", at: at) {
                    while at < characters.count, !characters[at].isNewline { at += 1 }
                    blank(start..<at)
                } else if starts("/*", at: at) {
                    var nesting = 0
                    while at < characters.count {
                        if starts("/*", at: at) {
                            nesting += 1
                            at += 2
                        } else if starts("*/", at: at) {
                            nesting -= 1
                            at += 2
                            if nesting == 0 { break }
                        } else {
                            at += 1
                        }
                    }
                    blank(start..<at)
                } else if let hashes = stringOpening() {
                    let opens = string(hashes: hashes)
                    blank(start..<at)
                    literals.append((start..<at, opens))
                } else {
                    if inInterpolation {
                        if characters[at] == "(" {
                            depth += 1
                        } else if characters[at] == ")" {
                            if depth == 0 { return }
                            depth -= 1
                        }
                    }
                    at += 1
                }
            }
        }

        /// How many `#`s open the string literal at `at`; `nil` when none
        /// opens there (`#if`, `#Preview`).
        func stringOpening() -> Int? {
            var index = at
            while index < characters.count, characters[index] == "#" { index += 1 }
            return index < characters.count && characters[index] == "\"" ? index - at : nil
        }

        /// A string literal, from its opening at `at` to just past its close:
        /// single-line or multi-line, raw by `hashes`, its interpolations
        /// scanned as code so a string inside one does not end it. Whether
        /// an interpolation opens its text.
        @discardableResult
        mutating func string(hashes: Int) -> Bool {
            let pounds = String(repeating: "#", count: hashes)
            at += hashes
            let multiline = starts("\"\"\"", at: at)
            let quote = multiline ? "\"\"\"" : "\""
            at += quote.count
            let text = at
            var opens = false
            let close = quote + pounds
            let escape = "\\" + pounds
            while at < characters.count {
                if starts(close, at: at) {
                    at += close.count
                    return opens
                }
                if starts(escape, at: at) {
                    let escaped = at
                    at += escape.count
                    if at < characters.count, characters[at] == "(" {
                        opens = opens || escaped == text
                        at += 1
                        code(inInterpolation: true)
                    }
                    if at < characters.count { at += 1 }
                    continue
                }
                if !multiline, characters[at].isNewline { return opens }
                at += 1
            }
            return opens
        }
    }

    // MARK: - Argv lines

    /// The binary's command words, one a line, as `scripts/refresh-parity-fixture.sh`
    /// records them: an array whose first word is one is a line the app runs.
    static let commandWordsFile = "tests/presentation/shell/Fixtures/command-words.txt"

    /// Every argv line under `source/` held to the builder (GUI M23).
    private func argvLines() -> [String] {
        guard let text = tree.contents(of: Self.commandWordsFile) else {
            return ["\(Self.commandWordsFile) is missing: the argv rule reads the binary's command words from it"]
        }
        let words = Set(text.split(whereSeparator: \.isNewline).map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty })
        guard !words.isEmpty else {
            return ["\(Self.commandWordsFile) names no command word: an empty list proves nothing"]
        }
        let files = tree.swiftFiles(under: "source").map { ($0, tree.contents(of: $0) ?? "") }
        // A line's name may be declared in one file and added to in another
        // (`RoleAssignment.verb`, read from Presentation): a public one is
        // the whole tree's. A local is its file's alone.
        var names: (literal: Set<String>, built: Set<String>) = ([], [])
        for (_, source) in files {
            let found = Self.argvLineNames(in: source, commandWords: words, publicOnly: true)
            names.literal.formUnion(found.literal)
            names.built.formUnion(found.built)
        }
        let lineNames = (literal: names.literal.subtracting(names.built), built: names.built)
        return files.flatMap { Self.argvViolations(in: $0.1, file: $0.0, commandWords: words, lineNames: lineNames) }
    }

    /// The names `source` declares from a line (`ArgvScan.lineNames`); with
    /// `publicOnly`, those declared `public` alone.
    static func argvLineNames(in source: String, commandWords: Set<String>, publicOnly: Bool = false) -> (literal: Set<String>, built: Set<String>) {
        var masker = Masker(source)
        masker.code(inInterpolation: false)
        return ArgvScan(text: masker.characters, code: masker.out, literals: masker.literals, words: commandWords).lineNames(publicOnly: publicOnly)
    }

    /// What breaks the argv rule in `source`, each named by `file` and line
    /// (GUI M23). The CLI's parser reads a dash-led word before `--` as an
    /// option -- `embed delete --yes -x` exits 106 -- so a name, path or id
    /// reaches the verb only through `Argv.line`, after its `--`:
    /// - a line literal -- an array whose first element is a string literal
    ///   naming a command word -- holds only string literals, each opening
    ///   with the author's own text: read from the source at the positions
    ///   `code(_:)` blanks, so "the word after a dash literal" is never taken
    ///   for an option's value;
    /// - nothing but a list of such literals is added to a line literal, or
    ///   to a name declared from one, in any file of the tree (`+`, `+=`,
    ///   `append`, `insert`): today's `role.verb + [backend]`, and as much
    ///   `line + names` or `line + (name.map { [$0] } ?? [])`;
    /// - nothing is added to a line `Argv` built, or to a name declared from
    ///   one: such a line holds its end of options, and a later option goes
    ///   through `Argv.adding`.
    ///
    /// A `__complete` ask opens with no command word, so it is exempt, as its
    /// protocol takes its words verbatim. `lineNames` are the tree's
    /// (`argvLineNames`), this file's added.
    static func argvViolations(
        in source: String, file: String, commandWords: Set<String>, lineNames: (literal: Set<String>, built: Set<String>) = ([], [])
    ) -> [String] {
        var masker = Masker(source)
        masker.code(inInterpolation: false)
        let scan = ArgvScan(text: masker.characters, code: masker.out, literals: masker.literals, words: commandWords)
        var found: [(Int, String)] = []
        let builder = "the CLI's parser reads a dash-led word before -- as an option, so a name, path or id goes through Argv.line (its options, then -- and the names)"
        for open in scan.code.indices where scan.code[open] == "[" && scan.isLineLiteral(open) {
            let held = scan.elements(of: open).filter { !scan.isPlainLiteral($0) }
            if let first = held.first {
                found.append((open,
                    "\(file):\(scan.line(of: first.lowerBound)): a command line holds \(held.map(scan.words).joined(separator: ", ")), not string literals alone -- \(builder)"))
            }
        }
        let own = scan.lineNames()
        let local = (literal: own.literal.subtracting(own.built), built: own.built)
        let builtMembers = lineNames.built.union(own.built)
        let members = (literal: lineNames.literal.union(own.literal).subtracting(builtMembers), built: builtMembers)
        for site in scan.additions() {
            switch scan.line(before: site.operand, local: local, members: members) {
            case .built:
                found.append((site.start,
                    "\(file):\(scan.line(of: site.start)): \(scan.words(site.added)) is added to a line Argv built -- it holds its end of options already, so a word added at its end lands after -- and is read as a name; add an option with Argv.adding"))
            case .literal:
                let held = site.elements.filter { !scan.isPlainLiteral($0) }
                if let first = held.first {
                    found.append((site.start,
                        "\(file):\(scan.line(of: first.lowerBound)): \(held.map(scan.words).joined(separator: ", ")) is added to a command line -- \(builder)"))
                }
            case nil:
                continue
            }
        }
        return found.sorted { $0.0 < $1.0 }.map(\.1)
    }

    /// The argv rule's reading of one file: its characters, the same with
    /// comments and strings blank, and where each string literal lies.
    private struct ArgvScan {
        let text: [Character]
        let code: [Character]
        let literals: [Int: (range: Range<Int>, opensWithInterpolation: Bool)]
        let words: Set<String>

        init(text: [Character], code: [Character], literals: [(range: Range<Int>, opensWithInterpolation: Bool)], words: Set<String>) {
            self.text = text
            self.code = code
            self.literals = Dictionary(literals.map { ($0.range.lowerBound, $0) }, uniquingKeysWith: { first, _ in first })
            self.words = words
        }

        /// What a line is added to: one an `Argv` call built, or a line literal.
        enum Kind { case built, literal }

        /// One addition to something: `x + [...]`, `x += [...]`,
        /// `x.append(...)`, `x.insert(...)`. `operand` is just past what it
        /// adds to, `added` what it adds and `elements` its words.
        struct Addition {
            let start: Int
            let operand: Int
            let added: Range<Int>
            let elements: [Range<Int>]
        }

        static func isSpace(_ character: Character) -> Bool { character == " " || character == "\t" || character.isNewline }
        static func isName(_ character: Character) -> Bool { character == "_" || character.isLetter || character.isNumber }

        func skipSpace(from index: Int) -> Int {
            var index = index
            while index < text.count, Self.isSpace(text[index]) { index += 1 }
            return index
        }

        func skipSpace(backFrom index: Int) -> Int {
            var index = index
            while index >= 0, Self.isSpace(text[index]) { index -= 1 }
            return index
        }

        /// The 1-based line `index` is on.
        func line(of index: Int) -> Int {
            text[..<index].reduce(1) { $1.isNewline ? $0 + 1 : $0 }
        }

        /// The source of `range`, on one line.
        func words(_ range: Range<Int>) -> String {
            String(text[range]).split(whereSeparator: \.isNewline).map { $0.trimmingCharacters(in: .whitespaces) }.joined(separator: " ")
        }

        /// The bracket closing the one at `open`, in the code.
        func closing(_ open: Int) -> Int? {
            let opener = code[open]
            let closer: Character = opener == "(" ? ")" : opener == "[" ? "]" : "}"
            var depth = 0
            for index in open..<code.count {
                if code[index] == opener {
                    depth += 1
                } else if code[index] == closer {
                    depth -= 1
                    if depth == 0 { return index }
                }
            }
            return nil
        }

        /// The bracket opening the one at `close`, in the code.
        func opening(_ close: Int) -> Int? {
            let closer = code[close]
            let opener: Character = closer == ")" ? "(" : closer == "]" ? "[" : "{"
            var depth = 0
            for index in stride(from: close, through: 0, by: -1) {
                if code[index] == closer {
                    depth += 1
                } else if code[index] == opener {
                    depth -= 1
                    if depth == 0 { return index }
                }
            }
            return nil
        }

        /// Whether the `[` at `open` subscripts what is before it.
        func subscripts(_ open: Int) -> Bool {
            open > 0 && (Self.isName(code[open - 1]) || ")]?!".contains(code[open - 1]))
        }

        /// A string literal's own text, its quotes and pounds dropped; `nil`
        /// for a multi-line one or none at `index`.
        func literalText(at index: Int) -> String? {
            guard let literal = literals[index] else { return nil }
            var body = text[literal.range]
            while body.first == "#" { body = body.dropFirst() }
            while body.last == "#" { body = body.dropLast() }
            guard body.count >= 2, body.first == "\"", body.last == "\"", !body.starts(with: "\"\"\"") else { return nil }
            return String(body.dropFirst().dropLast())
        }

        /// Whether the `[` at `open` opens a line literal: an array whose
        /// first element is a string literal naming a command word.
        func isLineLiteral(_ open: Int) -> Bool {
            guard code[open] == "[", !subscripts(open) else { return false }
            let first = skipSpace(from: open + 1)
            guard let word = literalText(at: first), words.contains(word), let literal = literals[first] else { return false }
            let next = skipSpace(from: literal.range.upperBound)
            return next < text.count && text[next] != ":"
        }

        /// The elements of the bracketed list at `open`, each trimmed.
        func elements(of open: Int) -> [Range<Int>] {
            guard let close = closing(open) else { return [] }
            var found: [Range<Int>] = []
            var depth = 0
            var start = open + 1
            for index in (open + 1)..<close {
                if "([{".contains(code[index]) {
                    depth += 1
                } else if ")]}".contains(code[index]) {
                    depth -= 1
                } else if code[index] == ",", depth == 0 {
                    found.append(start..<index)
                    start = index + 1
                }
            }
            found.append(start..<close)
            return found.compactMap { range in
                let lower = skipSpace(from: range.lowerBound)
                var upper = range.upperBound
                while upper > lower, Self.isSpace(text[upper - 1]) { upper -= 1 }
                return lower < upper ? lower..<upper : nil
            }
        }

        /// Whether `range` is one string literal opening with its author's text.
        func isPlainLiteral(_ range: Range<Int>) -> Bool {
            guard let literal = literals[range.lowerBound] else { return false }
            return literal.range == range && !literal.opensWithInterpolation
        }

        /// The names the file declares (`let`, `var`, `func`) from a line --
        /// their initializer or body holding, outside any bracket, brace or
        /// closure of its own, a line literal (`literal`) or an `Argv.line`,
        /// `Argv.adding` or `Argv.text` call (`built`).
        func lineNames(publicOnly: Bool = false) -> (literal: Set<String>, built: Set<String>) {
            var literal: Set<String> = []
            var built: Set<String> = []
            let source = String(code)
            let regex = try! NSRegularExpression(pattern: #"\b(?:let|var|func)\s+([A-Za-z_][A-Za-z0-9_]*)"#)
            // The matches come in order: each one's end found from the last's.
            var cursor = (index: source.startIndex, offset: 0)
            for match in regex.matches(in: source, range: NSRange(source.startIndex..., in: source)) {
                guard let range = Range(match.range(at: 1), in: source), let whole = Range(match.range, in: source) else { continue }
                let name = String(source[range])
                if publicOnly {
                    let lineStart = source[..<whole.lowerBound].lastIndex(where: \.isNewline).map { source.index(after: $0) } ?? source.startIndex
                    let modifiers = source[lineStart..<whole.lowerBound].split(separator: " ")
                    guard modifiers.contains("public") || modifiers.contains("open") else { continue }
                }
                cursor = (whole.upperBound, cursor.offset + source.distance(from: cursor.index, to: whole.upperBound))
                guard let extent = extent(after: cursor.offset) else { continue }
                var depth = 0
                for index in extent {
                    let character = code[index]
                    if "([{".contains(character) {
                        if depth == 0, character == "[", isLineLiteral(index) { literal.insert(name) }
                        depth += 1
                    } else if ")]}".contains(character) {
                        depth -= 1
                    } else if depth == 0, character == "A", index == 0 || !Self.isName(code[index - 1]),
                        ["line", "adding", "text"].contains(where: { starts("Argv.\($0)(", at: index) })
                    {
                        built.insert(name)
                    }
                }
            }
            return (literal.subtracting(built), built)
        }

        /// Whether the code at `index` reads `word`.
        func starts(_ word: String, at index: Int) -> Bool {
            var index = index
            for character in word {
                guard index < code.count, code[index] == character else { return false }
                index += 1
            }
            return true
        }

        /// A declaration's initializer (after its `=`, to the end of its line
        /// or a `{`) or body (its braces), from just past its name.
        func extent(after name: Int) -> Range<Int>? {
            var depth = 0
            var index = name
            while index < code.count {
                let character = code[index]
                if "([".contains(character) {
                    depth += 1
                } else if ")]".contains(character) {
                    depth -= 1
                    // A bracket the name sits inside closes (`case .x(let y)`): no declaration of a line.
                    if depth < 0 { return nil }
                } else if depth == 0, character == "{" {
                    return closing(index).map { (index + 1)..<$0 }
                } else if depth == 0, character.isNewline {
                    return nil
                } else if depth == 0, character == "=", index + 1 < code.count, code[index + 1] != "=",
                    index > 0, !"=!<>".contains(code[index - 1])
                {
                    var end = index + 1
                    var inner = 0
                    while end < code.count {
                        let next = code[end]
                        if "([".contains(next) {
                            inner += 1
                        } else if ")]".contains(next) {
                            inner -= 1
                        } else if inner == 0, next.isNewline || next == "{" {
                            break
                        }
                        end += 1
                    }
                    return (index + 1)..<end
                }
                index += 1
            }
            return nil
        }

        /// Every addition in the code: `+ [...]` and `+= [...]`, and a
        /// name's `.append(...)` or `.insert(...)`.
        func additions() -> [Addition] {
            var found: [Addition] = []
            for index in code.indices where code[index] == "+" {
                let isCompound = index + 1 < code.count && code[index + 1] == "="
                let open = skipSpace(from: index + (isCompound ? 2 : 1))
                guard open < code.count else { continue }
                if code[open] == "[", let close = closing(open) {
                    found.append(Addition(start: open, operand: index, added: open..<(close + 1), elements: elements(of: open)))
                } else if let operand = operand(from: open) {
                    // Anything but a list -- a name, a call, `(...)` -- is no
                    // list of literals.
                    found.append(Addition(start: open, operand: index, added: operand, elements: [operand]))
                }
            }
            for index in code.indices where code[index] == "." {
                for verb in ["append", "insert"] where starts(verb, at: index + 1) {
                    let paren = skipSpace(from: index + 1 + verb.count)
                    guard paren < code.count, code[paren] == "(", let close = closing(paren) else { continue }
                    let arguments = elements(of: paren)
                    guard let first = arguments.first else { continue }
                    var added = [first]
                    if String(text[first]).hasPrefix("contentsOf:") {
                        let value = skipSpace(from: first.lowerBound + "contentsOf:".count)
                        added = code[value] == "[" ? elements(of: value) : [value..<first.upperBound]
                    }
                    found.append(Addition(start: first.lowerBound, operand: index, added: paren..<(close + 1), elements: added))
                }
            }
            return found
        }

        /// The operand that opens at `start`: up to the end of its line, or the
        /// first `,`, `+` or closing bracket outside any bracket of its own.
        func operand(from start: Int) -> Range<Int>? {
            var depth = 0
            var end = start
            while end < code.count {
                let character = code[end]
                if "([{".contains(character) {
                    depth += 1
                } else if ")]}".contains(character) {
                    if depth == 0 { break }
                    depth -= 1
                } else if depth == 0, character.isNewline || character == "," || character == "+" {
                    break
                }
                end += 1
            }
            while end > start, Self.isSpace(text[end - 1]) { end -= 1 }
            return start < end ? start..<end : nil
        }

        /// What the operand just before `position` (an addition's `+` or
        /// `.`) is: a line `Argv` built, a line literal, or neither.
        /// A bare name is read by the file's own declarations (`local`); a
        /// member -- `role.verb` -- by the tree's public ones too (`members`).
        func line(
            before position: Int, local: (literal: Set<String>, built: Set<String>), members: (literal: Set<String>, built: Set<String>)
        ) -> Kind? {
            let last = skipSpace(backFrom: position - 1)
            guard last >= 0 else { return nil }
            if code[last] == "]" {
                guard let open = opening(last) else { return nil }
                return isLineLiteral(open) ? .literal : nil
            }
            var end = last
            if code[last] == ")" {
                guard let open = opening(last) else { return nil }
                end = skipSpace(backFrom: open - 1)
            }
            var start = end
            while start >= 0, Self.isName(code[start]) { start -= 1 }
            guard start < end else { return nil }
            let name = String(code[(start + 1)...end])
            if ["line", "adding", "text"].contains(name), start >= 4, String(code[(start - 4)...start]) == "Argv." { return .built }
            let names = start >= 0 && code[start] == "." ? members : local
            if names.built.contains(name) { return .built }
            return names.literal.contains(name) ? .literal : nil
        }
    }

    // MARK: - Layer cards

    /// Each layer with modules has `source/<layer>/CLAUDE.md`, whose
    /// `**Modules:**` line names them -- exactly them -- in backticks.
    private func cards(for modules: [Module]) -> [String] {
        var found: [String] = []
        for layer in Self.layers {
            let packages = Set(modules.filter { $0.layer == layer }.map(\.package))
            guard !packages.isEmpty else { continue }
            let card = "source/\(layer)/CLAUDE.md"
            guard let text = tree.contents(of: card) else {
                found.append("\(card) is missing: every layer carries its card")
                continue
            }
            guard let line = text.split(separator: "\n").first(where: { $0.hasPrefix("**Modules:**") }) else {
                found.append("\(card) has no **Modules:** line")
                continue
            }
            let named = Set(
                line.split(separator: "`", omittingEmptySubsequences: false).enumerated()
                    .filter { $0.offset % 2 == 1 }.map { String($0.element) })
            if named != packages {
                found.append(
                    "\(card) names \(named.sorted().joined(separator: ", ")) but the layer's modules are \(packages.sorted().joined(separator: ", "))")
            }
        }
        return found
    }
}
