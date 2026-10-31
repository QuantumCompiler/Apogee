import AppKit
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
                    if model.graph != nil {
                        Menu("More") {
                            ForEach(GraphViewModel.ExportFormat.allCases) { format in
                                Button("Export as \(format.title)…") { export(format) }
                            }
                            Button("Save the report…") { saveReport() }
                            Divider()
                            Button("Dedupe…") { Task { await model.planDedupe() } }
                            Button("Delete the graph…", role: .destructive) { model.askToDelete() }
                        }
                        .fixedSize()
                        .disabled(!(model.stats?.built ?? false) || model.working != nil)
                    }
                }
                if let working = model.working {
                    Text(working).font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
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
        .sheet(isPresented: Binding(get: { model.dedupe != nil }, set: { if !$0 { model.dismissDedupe() } })) {
            DedupeSheet(model: model)
        }
        .confirmationDialog(
            model.pendingDelete.map { "Delete the graph \($0)?" } ?? "",
            isPresented: Binding(get: { model.pendingDelete != nil }, set: { if !$0 { model.pendingDelete = nil } }),
            titleVisibility: .visible
        ) {
            Button("Delete the graph", role: .destructive) { Task { await model.confirmDelete() } }
            Button("Cancel", role: .cancel) { model.pendingDelete = nil }
        } message: {
            Text(deleteMessage)
        }
        .alert("That didn't work", isPresented: Binding(get: { model.problem != nil }, set: { if !$0 { model.dismissProblem() } })) {
            Button("OK") { model.dismissProblem() }
        } message: {
            Text(model.problem ?? "")
        }
    }

    /// What deleting keeps, as the CLI says it does.
    private var deleteMessage: String {
        let name = model.pendingDelete ?? ""
        return model.stats?.isProse == true
            ? "apogee graph delete \(name) -- the collection's graph is cleared; its chunks stay, and the next build starts from scratch."
            : "apogee graph delete \(name) -- its database is removed; its config entry stays (apogee config delete-graph \(name) removes it), and Rebuild parses its sources again."
    }

    /// Where the export goes, asked of the person in a save panel -- the
    /// CLI's own file name offered, never a place chosen for them -- then
    /// the CLI writes it.
    private func export(_ format: GraphViewModel.ExportFormat) {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = model.exportName(format)
        guard panel.runModal() == .OK, let url = panel.url else { return }
        Task { await model.export(format, to: url) }
    }

    /// Where the report goes, asked the same way.
    private func saveReport() {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = model.reportName
        guard panel.runModal() == .OK, let url = panel.url else { return }
        Task { await model.saveReport(to: url) }
    }
}

/// A dedupe: its dry run as the CLI prints it, and the merge offered only
/// where it names entities to merge.
struct DedupeSheet: View {
    let model: GraphViewModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("Dedupe \(model.graph ?? "the graph")").font(.apogeeHeading)
            switch model.dedupe {
            case .planning?:
                HStack {
                    ProgressView().controlSize(.small)
                    Text("apogee graph dedupe \(model.graph ?? "") --dry-run").font(.apogeeCode).foregroundStyle(Palette.textSecondary)
                }
            case .planned(let text)?:
                Text("The dry run -- nothing changed yet:").font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                printed(text)
                if model.dedupeMerges {
                    Text("apogee graph dedupe \(model.graph ?? "") -- the entities named merge into the one each is kept as. Re-run Find communities after, as the CLI says.")
                        .font(.apogeeMeta)
                        .foregroundStyle(Palette.textTertiary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            case .found(let plan)?:
                Text("The dry run -- nothing changed yet:").font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                DedupeGroups(plan: plan)
                if model.dedupeMerges {
                    Text("apogee graph dedupe \(model.graph ?? "") -- the entities named merge into the one each is kept as. Re-run Find communities after, as the CLI says.")
                        .font(.apogeeMeta)
                        .foregroundStyle(Palette.textTertiary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            case .merging?:
                HStack {
                    ProgressView().controlSize(.small)
                    Text(model.building?.busy ?? "merging").font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                }
            case .merged(let text)?:
                printed(text)
            case .failed(let message)?:
                Text(message).font(.apogeeSmall).foregroundStyle(Palette.error).textSelection(.enabled)
            case nil:
                EmptyView()
            }
            HStack {
                Spacer()
                if model.dedupeMerges {
                    Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                    Button("Merge them", role: .destructive) { model.confirmDedupe() }
                } else {
                    Button("Done") { dismiss() }
                        .keyboardShortcut(.defaultAction)
                        .disabled(model.dedupe == .merging)
                }
            }
        }
        .padding(Spacing.xl)
        .frame(width: 560)
    }

    private func printed(_ text: String) -> some View {
        Text(text)
            .font(.apogeeCode)
            .textSelection(.enabled)
            .fixedSize(horizontal: false, vertical: true)
            .padding(Spacing.m)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Palette.window, in: RoundedRectangle(cornerRadius: Radius.card))
    }
}

/// A dedupe's plan, from its document (the CLI's M31): each group -- the
/// entity kept and the names that merge into it -- then what the plan says
/// in a sentence, a code graph's entities merged by name as they were built,
/// and why the vector pass compared nothing where it didn't run.
struct DedupeGroups: View {
    let plan: DedupePlan

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.s) {
            if !plan.groups.isEmpty {
                Grid(alignment: .leadingFirstTextBaseline, horizontalSpacing: Spacing.m, verticalSpacing: Spacing.s) {
                    GridRow {
                        ForEach(["KEPT", "MERGED INTO IT"], id: \.self) {
                            Text($0).font(.system(size: TypeScale.label, weight: .medium, design: .monospaced)).foregroundStyle(Palette.textTertiary)
                        }
                    }
                    ForEach(plan.groups) { group in
                        GridRow {
                            VStack(alignment: .leading, spacing: 0) {
                                Text(group.kept).font(.apogeeCode)
                                Text(group.keptType).font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                            }
                            Text(group.merged.joined(separator: ", "))
                                .font(.apogeeSmall)
                                .textSelection(.enabled)
                                .fixedSize(horizontal: false, vertical: true)
                        }
                    }
                }
            }
            Text(plan.summary).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
            if plan.codeEntities > 0 {
                Text("Code entities: \(plan.codeEntities), merged by exact qualified name as they were built (\(plan.codeIdentityMerges) stated at more than one place), never by resemblance.")
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            if let skipped = plan.proseSkipped {
                Text("Prose entities: vector dedupe skipped -- \(skipped).")
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
                    .fixedSize(horizontal: false, vertical: true)
            }
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

#if DEBUG
#Preview("A code graph searched, a node explained") {
    let graph = GraphPreview.graph()
    GraphScreen(model: graph, shell: GraphPreview.shell())
        .frame(width: 1200, height: 720)
        .task { await GraphPreview.searched(graph) }
}

#Preview("A collection graph's report") {
    let graph = GraphPreview.graph(GraphPreview.prose)
    GraphScreen(model: graph, shell: GraphPreview.shell())
        .frame(width: 1100, height: 720)
        .task { await graph.connect(binary: previewBinary) }
}

#Preview("A collection graph not built yet") {
    let graph = GraphPreview.graph(GraphPreview.unbuilt)
    GraphScreen(model: graph, shell: GraphPreview.shell())
        .frame(width: 1100, height: 520)
        .task { await graph.connect(binary: previewBinary) }
}

#Preview("A rebuild running") {
    let graph = GraphPreview.graph()
    GraphScreen(model: graph, shell: GraphPreview.shell())
        .frame(width: 1100, height: 520)
        .task {
            await graph.connect(binary: previewBinary)
            graph.rebuild()
        }
}

#Preview("No code graph yet") {
    let graph = GraphPreview.graph(GraphPreview.none)
    GraphScreen(model: graph, shell: GraphPreview.shell())
        .frame(width: 1100, height: 520)
        .task { await graph.connect(binary: previewBinary) }
}

#Preview("Apogee not installed") {
    let graph = GraphPreview.graph()
    GraphScreen(model: graph, shell: GraphPreview.shell())
        .frame(width: 1100, height: 520)
        .task { await graph.connect(binary: nil) }
}

#Preview("The neighbourhood of runner fetch") {
    let graph = GraphPreview.graph()
    GraphCanvas(model: graph, result: GraphPreview.query)
        .frame(width: 800, height: 600)
        .task { await GraphPreview.searched(graph) }
}

#Preview("A class explained: its callers, where it is defined") {
    let graph = GraphPreview.graph()
    ExplainCard(model: graph)
        .frame(width: 300, height: 640)
        .task { await GraphPreview.searched(graph) }
}

#Preview("An entity explained: its community and decision") {
    let graph = GraphPreview.graph(GraphPreview.prose)
    ExplainCard(model: graph)
        .frame(width: 300, height: 640)
        .task {
            await graph.connect(binary: previewBinary)
            await graph.select("Heat pump")
        }
}

#Preview("A decision record attached") {
    DecisionLine(decision: GraphPreview.report.decisions[0])
        .padding(Spacing.l)
        .frame(width: 420)
}

#Preview("A report: summary, communities, decisions, busiest") {
    ReportView(model: GraphPreview.graph(GraphPreview.prose), report: GraphPreview.report)
        .frame(width: 800, height: 720)
}

#Preview("Deleting a graph, asked first") {
    let graph = GraphPreview.graph()
    GraphScreen(model: graph, shell: GraphPreview.shell())
        .frame(width: 1100, height: 520)
        .task {
            await graph.connect(binary: previewBinary)
            graph.askToDelete()
        }
}

#Preview("A named graph deleted: Rebuild makes it again") {
    let graph = GraphPreview.graph(GraphPreview.deleted)
    GraphScreen(model: graph, shell: GraphPreview.shell())
        .frame(width: 1100, height: 520)
        .task { await graph.connect(binary: previewBinary) }
}

#Preview("An export refused, in the CLI's words") {
    let graph = GraphPreview.graph(GraphPreview.exportRefused)
    GraphScreen(model: graph, shell: GraphPreview.shell())
        .frame(width: 1100, height: 520)
        .task {
            await graph.connect(binary: previewBinary)
            await graph.export(.html, to: URL(fileURLWithPath: "/Users/someone/Documents/demo.html"))
        }
}

#Preview("Dedupe: the dry run, nothing to merge") {
    let graph = GraphPreview.graph()
    DedupeSheet(model: graph)
        .task {
            await graph.connect(binary: previewBinary)
            await graph.planDedupe()
        }
}

#Preview("Dedupe: the dry run names merges, offered on the person's word") {
    let graph = GraphPreview.graph(GraphPreview.merges)
    DedupeSheet(model: graph)
        .task {
            await graph.connect(binary: previewBinary)
            await graph.planDedupe()
        }
}

#Preview("Dedupe: the dry run's groups, from its document, Merge offered") {
    let graph = GraphPreview.documentGraph(GraphPreview.documents())
    DedupeSheet(model: graph)
        .task {
            await graph.connect(binary: previewBinary)
            await graph.planDedupe()
        }
}

#Preview("Dedupe: the document names no group") {
    let graph = GraphPreview.documentGraph(GraphPreview.documents(nothing: true))
    DedupeSheet(model: graph)
        .task {
            await graph.connect(binary: previewBinary)
            await graph.planDedupe()
        }
}

#Preview("Dedupe groups, kept and merged") {
    DedupeGroups(plan: GraphPreview.groupsPlan)
        .padding(Spacing.xl)
        .frame(width: 560)
}

#Preview("Dedupe: merging") {
    let graph = GraphPreview.graph(GraphPreview.merges)
    DedupeSheet(model: graph)
        .task {
            await graph.connect(binary: previewBinary)
            await graph.planDedupe()
            graph.confirmDedupe()
        }
}

#Preview("New code graph: nothing chosen yet") {
    NewGraphSheet { _, _ in }
}
#endif
