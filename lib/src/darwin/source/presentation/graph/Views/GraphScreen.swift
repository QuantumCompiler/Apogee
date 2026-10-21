import BusinessReads
import PresentationShell
import PresentationStyles
import SwiftUI

/// The Code graph place: search, the neighbourhood drawn, a node explained.
public struct GraphScreen: View {
    @Bindable private var model: GraphViewModel
    private let shell: ShellViewModel
    @State private var building = false

    public init(model: GraphViewModel, shell: ShellViewModel) {
        self.model = model
        self.shell = shell
    }

    public var body: some View {
        VStack(spacing: 0) {
            ScreenHeader(title: "Code graph") {
                if !model.graphs.isEmpty {
                    Picker("Graph", selection: Binding(get: { model.graph ?? "" }, set: { name in Task { await model.choose(name) } })) {
                        ForEach(model.graphs, id: \.self) { Text($0).tag($0) }
                    }
                    .labelsHidden()
                    .fixedSize()
                }
                if let stats = model.stats {
                    Text(stats.isProse
                        ? "\(stats.nodes) entities · \(stats.edges) relations · \(stats.communities) communities"
                        : "\(stats.nodes) nodes · \(stats.edges) edges · \(stats.codeFiles) files" + (stats.staleFiles > 0 ? " · \(stats.staleFiles) changed" : ""))
                        .font(.apogeeMeta.monospacedDigit())
                        .foregroundStyle(Palette.textTertiary)
                }
                if model.isBuilding {
                    ProgressView().controlSize(.small)
                    Text(model.building?.busy ?? "building").font(.apogeeMeta).foregroundStyle(Palette.textSecondary).lineLimit(1)
                } else {
                    if let stats = model.stats, stats.isProse {
                        Button(stats.built ? "Rebuild" : "Build its graph") { model.rebuild() }
                            .help("A model reads the collection's chunks for the things they name and how they relate")
                        if stats.built {
                            Button("Find communities") { model.findCommunities() }
                                .help("Group related entities and have the utility model summarise each group")
                        }
                    } else if model.graph != nil {
                        Button("Rebuild") { model.rebuild() }.help("Parse the sources again where files changed -- no model")
                    }
                    Button("New graph…") { building = true }
                }
            }
            Divider()
            HStack(spacing: 0) {
                VStack(spacing: 0) {
                    HStack {
                        TextField("A function, a class, a file, or words naming some", text: $model.question)
                            .textFieldStyle(.roundedBorder)
                            .onSubmit { Task { await model.search() } }
                        Button("Search") { Task { await model.search() } }
                            .disabled(model.graph == nil || model.question.trimmingCharacters(in: .whitespaces).isEmpty)
                    }
                    .padding(Spacing.m)
                    if let empty = model.emptyText {
                        EmptyState(symbol: "point.3.connected.trianglepath.dotted", title: "Code graph", message: empty)
                            .frame(maxWidth: .infinity, maxHeight: .infinity)
                    } else if let result = model.result {
                        GraphCanvas(model: model, result: result)
                    } else if let report = model.report {
                        ReportView(model: model, report: report)
                    }
                }
                if model.explain != nil && shell.layout.sidePanes {
                    Divider()
                    ExplainCard(model: model).frame(width: 300)
                }
            }
        }
        .sheet(isPresented: $building) {
            NewGraphSheet { name, folder in model.build(name: name, folder: folder) }
        }
    }
}

/// The neighbourhood: edges drawn on a canvas, each node a button at its
/// place in the layout.
struct GraphCanvas: View {
    let model: GraphViewModel
    let result: GraphQuery

    var body: some View {
        GeometryReader { geometry in
            let size = geometry.size
            let place = { (name: String) -> CGPoint? in
                model.layout.positions[name].map { CGPoint(x: $0.x * size.width, y: $0.y * size.height) }
            }
            ZStack {
                Canvas { context, _ in
                    for edge in model.edges {
                        guard let a = place(edge.from), let b = place(edge.to) else { continue }
                        var path = Path()
                        path.move(to: a)
                        path.addLine(to: b)
                        let selected = model.selected == edge.from || model.selected == edge.to
                        context.stroke(path, with: .color(selected ? Palette.accent : Palette.strong), lineWidth: selected ? 1.5 : 1)
                    }
                }
                ForEach(result.nodes) { node in
                    if let point = place(node.name) {
                        Button {
                            Task { await model.select(node.name) }
                        } label: {
                            VStack(spacing: 1) {
                                Text(node.short).font(.apogeeSmall.weight(node.hop == 0 ? .semibold : .regular))
                                Text(node.type).font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                            }
                            .padding(.horizontal, Spacing.s)
                            .padding(.vertical, Spacing.xs)
                            .background(model.selected == node.name ? Palette.selection : Palette.card, in: RoundedRectangle(cornerRadius: Radius.control))
                            .overlay(RoundedRectangle(cornerRadius: Radius.control).strokeBorder(node.hop == 0 ? Palette.accent : Palette.border))
                        }
                        .buttonStyle(.plain)
                        .help(node.name + (node.place.map { " · \($0)" } ?? ""))
                        .position(point)
                    }
                }
            }
        }
        .padding(Spacing.l)
        .background(Palette.window)
    }
}

/// One node explained: its degree, where it is written, its neighbours by
/// relation -- each a step to walk.
struct ExplainCard: View {
    let model: GraphViewModel

    var body: some View {
        ScrollView {
            if let explain = model.explain {
                VStack(alignment: .leading, spacing: Spacing.m) {
                    HStack(alignment: .firstTextBaseline) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(explain.node.short).font(.apogeeHeading)
                            Text(explain.node.type).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                        }
                        Spacer()
                        Button {
                            model.clearSelection()
                        } label: {
                            Image(systemName: "xmark")
                        }
                        .buttonStyle(.plain)
                        .accessibilityLabel("Close")
                    }
                    if !explain.description.isEmpty {
                        Text(explain.description).font(.apogeeCode).textSelection(.enabled)
                    }
                    Text("\(explain.degreeIn) in · \(explain.degreeOut) out").font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
                    ForEach(explain.statedAt, id: \.self) { place in
                        Text(place).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
                    }
                    if !explain.communities.isEmpty {
                        section("COMMUNITIES")
                        ForEach(explain.communities) { community in
                            Text("#\(community.id) · \(community.size) entities" + (community.summary.map { ": " + $0 } ?? " -- not summarised yet"))
                                .font(.apogeeSmall)
                                .textSelection(.enabled)
                        }
                    }
                    if !explain.decisions.isEmpty {
                        section("DECISIONS")
                        ForEach(explain.decisions) { decision in
                            DecisionLine(decision: decision)
                        }
                    }
                    ForEach(explain.relations) { relation in
                        VStack(alignment: .leading, spacing: Spacing.xs) {
                            Text(relation.heading.uppercased() + (relation.total > relation.neighbors.count ? " · \(relation.neighbors.count) of \(relation.total)" : ""))
                                .font(.system(size: TypeScale.label, weight: .medium, design: .monospaced))
                                .foregroundStyle(Palette.textTertiary)
                            ForEach(relation.neighbors) { neighbor in
                                Button(neighbor.short) { Task { await model.walk(to: neighbor.name) } }
                                    .buttonStyle(.link)
                                    .font(.apogeeSmall)
                                    .help(neighbor.name)
                            }
                        }
                    }
                }
                .padding(Spacing.l)
            }
        }
        .background(Palette.inspector)
    }

    private func section(_ title: String) -> some View {
        Text(title).font(.system(size: TypeScale.label, weight: .medium, design: .monospaced)).foregroundStyle(Palette.textTertiary)
    }
}

/// A decision record attached to the graph: its decision, its branch.
struct DecisionLine: View {
    let decision: GraphDecision

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(decision.decision.isEmpty ? decision.name : decision.decision).font(.apogeeSmall).textSelection(.enabled)
            Text([decision.name, decision.status, decision.discipline].filter { !$0.isEmpty }.joined(separator: " · "))
                .font(.apogeeMeta.monospaced())
                .foregroundStyle(Palette.textTertiary)
        }
    }
}

/// A collection graph's report, before any search: its summary, its
/// communities, the decisions attached, its busiest entities -- each entity a
/// way in.
struct ReportView: View {
    let model: GraphViewModel
    let report: GraphReport

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.l) {
                Text(report.summary).font(.apogeeBody).textSelection(.enabled)
                if !report.communities.isEmpty {
                    heading("COMMUNITIES · \(report.communitiesTotal)")
                    ForEach(report.communities) { community in
                        VStack(alignment: .leading, spacing: Spacing.xs) {
                            Text(community.summary ?? "Not summarised yet -- Find communities has the utility model write one.")
                                .font(.apogeeBody)
                                .foregroundStyle(community.summary == nil ? Palette.textTertiary : Palette.textPrimary)
                            HStack(spacing: Spacing.s) {
                                ForEach(community.members) { member in
                                    Button(member.name) { Task { await model.select(member.name) } }.buttonStyle(.link).font(.apogeeSmall)
                                }
                            }
                        }
                        .padding(Spacing.m)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(Palette.inspector, in: RoundedRectangle(cornerRadius: Radius.card))
                    }
                }
                if !report.decisions.isEmpty {
                    heading("DECISIONS · \(report.decisionsTotal)")
                    ForEach(report.decisions) { DecisionLine(decision: $0) }
                }
                if !report.hubs.isEmpty {
                    heading("BUSIEST")
                    ForEach(report.hubs) { hub in
                        HStack {
                            Button(hub.node.name) { Task { await model.select(hub.node.name) } }.buttonStyle(.link)
                            Text("\(hub.node.type) · \(hub.degree) relation\(hub.degree == 1 ? "" : "s")")
                                .font(.apogeeMeta)
                                .foregroundStyle(Palette.textTertiary)
                        }
                    }
                }
            }
            .padding(Spacing.xl)
            .frame(maxWidth: 720, alignment: .leading)
            .frame(maxWidth: .infinity)
        }
    }

    private func heading(_ title: String) -> some View {
        Text(title).font(.system(size: TypeScale.label, weight: .medium, design: .monospaced)).foregroundStyle(Palette.textTertiary)
    }
}

/// A new code graph: a name and a folder of code.
struct NewGraphSheet: View {
    let build: (String, URL) -> Void
    @State private var name = ""
    @State private var folder: URL?
    @State private var choosing = false
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("New code graph").font(.apogeeHeading)
            Text("Parses a folder's code into a graph of its files, classes, functions and calls -- no model, no key, no network.")
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textSecondary)
            TextField("Name", text: $name).textFieldStyle(.roundedBorder)
            HStack {
                Text(folder?.path ?? "No folder chosen").lineLimit(1).truncationMode(.middle).foregroundStyle(Palette.textSecondary)
                Spacer()
                Button("Choose…") { choosing = true }
            }
            if let folder, !name.isEmpty {
                Text("apogee graph build \(name) --source \(folder.path)").font(.apogeeCode).foregroundStyle(Palette.textTertiary)
            }
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Build") {
                    if let folder { build(name, folder) }
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(folder == nil || name.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 520)
        .fileImporter(isPresented: $choosing, allowedContentTypes: [.folder]) { result in
            if case .success(let url) = result {
                folder = url
                if name.isEmpty { name = url.lastPathComponent }
            }
        }
    }
}
