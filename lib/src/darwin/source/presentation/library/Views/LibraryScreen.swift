import BusinessJobs
import BusinessReads
import PresentationShell
import PresentationStyles
import SwiftUI

/// The Library place (⌘0): agents, symphonies and MCP servers -- the
/// definitions sessions and runs draw on.
public struct LibraryScreen: View {
    public enum Tab: String, CaseIterable, Identifiable {
        case agents
        case symphonies
        case mcp
        public var id: String { rawValue }
        var title: String {
            switch self {
            case .agents: "Agents"
            case .symphonies: "Symphonies"
            case .mcp: "MCP servers"
            }
        }
    }

    @Bindable private var agents: AgentsViewModel
    @Bindable private var symphonies: SymphoniesViewModel
    @Bindable private var mcp: MCPViewModel
    @State private var tab: Tab = .agents

    public init(agents: AgentsViewModel, symphonies: SymphoniesViewModel, mcp: MCPViewModel) {
        self.agents = agents
        self.symphonies = symphonies
        self.mcp = mcp
    }

    public var body: some View {
        VStack(spacing: 0) {
            ScreenHeader(title: "Library") {
                Picker("Show", selection: $tab) {
                    ForEach(Tab.allCases) { Text($0.title).tag($0) }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .fixedSize()
                if tab == .agents { Button("New agent…") { agents.createForm = .init(); agents.creating = true } }
                if tab == .mcp && mcp.reading { ProgressView().controlSize(.small); Text("connecting").font(.apogeeMeta).foregroundStyle(Palette.textSecondary) }
            }
            Divider()
            switch tab {
            case .agents: AgentsView(model: agents)
            case .symphonies: SymphoniesView(model: symphonies)
            case .mcp: MCPView(model: mcp)
            }
        }
        .sheet(isPresented: $agents.creating) { AgentSheet(model: agents) }
        .sheet(item: Binding(get: { agents.editor.map(EditorID.init) }, set: { if $0 == nil { agents.closeEditor() } })) { _ in
            EditorSheet(editor: Binding(get: { agents.editor! }, set: { agents.editor = $0 }), save: { await agents.save() }, restore: nil, close: agents.closeEditor)
        }
        .sheet(item: Binding(get: { symphonies.editor.map(EditorID.init) }, set: { if $0 == nil { symphonies.closeEditor() } })) { _ in
            EditorSheet(editor: Binding(get: { symphonies.editor! }, set: { symphonies.editor = $0 }), save: { await symphonies.save() }, restore: { await symphonies.restore() }, close: symphonies.closeEditor)
        }
        .alert("That didn't work", isPresented: Binding(get: { (agents.failure ?? symphonies.failure ?? mcp.failure) != nil }, set: { if !$0 { agents.dismissFailure(); symphonies.dismissFailure(); mcp.dismissFailure() } })) {
            Button("OK") { agents.dismissFailure(); symphonies.dismissFailure(); mcp.dismissFailure() }
        } message: {
            Text(agents.failure ?? symphonies.failure ?? mcp.failure ?? "")
        }
    }

    struct EditorID: Identifiable {
        let editor: DefinitionEditor
        var id: String { editor.path }
    }
}

/// A list beside a page, the Library's shape; under its rows, what the list
/// has to say -- no apogee, or a read that failed.
struct ListPage<Row: Identifiable, Label: View, Page: View>: View {
    let rows: [Row]
    let selected: Row.ID?
    let pick: (Row) -> Void
    var note: String? = nil
    @ViewBuilder let label: (Row) -> Label
    @ViewBuilder let page: () -> Page

    var body: some View {
        HStack(spacing: 0) {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 1) {
                    ForEach(rows) { row in
                        Button { pick(row) } label: {
                            label(row)
                                .padding(Spacing.m)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .background(selected == row.id ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                                .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                    }
                    if let note {
                        Text(note).font(.apogeeSmall).foregroundStyle(Palette.textTertiary).padding(Spacing.m).textSelection(.enabled)
                    }
                }
                .padding(Spacing.s)
            }
            .frame(width: 300)
            .background(Palette.sidebar)
            Divider()
            page().frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        }
    }
}

struct AgentsView: View {
    @Bindable var model: AgentsViewModel

    var body: some View {
        ListPage(rows: model.agents, selected: model.selected, pick: { model.selected = $0.name }, note: model.emptyText) { agent in
            VStack(alignment: .leading, spacing: 2) {
                Text(agent.name).font(.apogeeBody)
                Text(agent.bundled ? "shipped · \(agent.tools)" : "yours · \(agent.tools)").font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
            }
        } page: {
            if let agent = model.current {
                ScrollView {
                    VStack(alignment: .leading, spacing: Spacing.m) {
                        HStack {
                            Text(agent.name).font(.apogeeHeading)
                            Spacer()
                            if !agent.bundled { Button("Delete…", role: .destructive) { model.pendingDelete = agent } }
                        }
                        Text(agent.description).font(.apogeeBody).textSelection(.enabled)
                        Text("Model: \(agent.model.isEmpty ? "the default" : agent.model) · tools: \(agent.tools)" + (agent.collection.isEmpty ? "" : " · retrieves from \(agent.collection)"))
                            .font(.apogeeSmall)
                            .foregroundStyle(Palette.textSecondary)
                        Text("FILES").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                        ForEach(agent.prompts + agent.schemas, id: \.self) { file in
                            HStack {
                                Text(file).font(.apogeeCode)
                                Spacer()
                                if !agent.bundled { Button("Edit") { model.edit(agent, file: file) }.buttonStyle(.borderless) }
                            }
                        }
                        if agent.bundled {
                            Text("A shipped agent: its files are Apogee's.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
                        }
                        Text("apogee analyze --agent \(agent.name) runs it.").font(.apogeeCode).foregroundStyle(Palette.textTertiary)
                    }
                    .padding(Spacing.xl)
                }
            } else {
                Text("Choose an agent.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary).padding(Spacing.xl)
            }
        }
        .confirmationDialog(
            "Delete the agent \(model.pendingDelete?.name ?? "")?",
            isPresented: Binding(get: { model.pendingDelete != nil }, set: { if !$0 { model.pendingDelete = nil } }),
            titleVisibility: .visible
        ) {
            Button("Delete it and its files", role: .destructive) { Task { await model.confirmDelete(purge: true) } }
            Button("Delete it, keep its files") { Task { await model.confirmDelete(purge: false) } }
            Button("Cancel", role: .cancel) { model.pendingDelete = nil }
        }
    }
}

struct SymphoniesView: View {
    @Bindable var model: SymphoniesViewModel

    var body: some View {
        ListPage(rows: model.symphonies, selected: model.selected, pick: { model.select($0) }, note: model.emptyText) { symphony in
            VStack(alignment: .leading, spacing: 2) {
                Text(symphony.name).font(.apogeeBody)
                Text("\(symphony.stages.count) stage\(symphony.stages.count == 1 ? "" : "s") · \(symphony.source)" + (symphony.problems.isEmpty ? "" : " · \(symphony.problems.count) problem\(symphony.problems.count == 1 ? "" : "s")"))
                    .font(.apogeeMeta)
                    .foregroundStyle(symphony.problems.isEmpty ? Palette.textTertiary : Palette.error)
            }
        } page: {
            if let symphony = model.current {
                ScrollView {
                    VStack(alignment: .leading, spacing: Spacing.m) {
                        HStack {
                            Text(symphony.name).font(.apogeeHeading)
                            Spacer()
                            if symphony.isOwnFile { Button("Edit…") { model.edit(symphony) } }
                            if symphony.source == "config" { Button("Delete…", role: .destructive) { model.pendingDelete = symphony } }
                        }
                        Text(symphony.description).font(.apogeeBody).textSelection(.enabled)
                        ForEach(symphony.problems, id: \.self) { Text($0).font(.apogeeSmall).foregroundStyle(Palette.error).textSelection(.enabled) }
                        ForEach(Array(symphony.stages.enumerated()), id: \.offset) { index, stage in
                            VStack(alignment: .leading, spacing: 2) {
                                Text("\(index + 1). \(stage.name) · " + (stage.plays.map { "plays \($0)" } ?? "asks \(stage.role ?? "?")"))
                                    .font(.apogeeSmall.weight(.medium))
                                if !stage.prompt.isEmpty {
                                    Text(stage.prompt).font(.apogeeCode).foregroundStyle(Palette.textSecondary).lineLimit(4).textSelection(.enabled)
                                }
                            }
                        }
                        if !symphony.isOwnFile {
                            Text(symphony.source == "shipped" ? "A shipped starter: apogee symphonies edit \(symphony.name) makes it yours, at a prompt." : "A config entry: apogee symphonies edit \(symphony.name) edits it, at a prompt.")
                                .font(.apogeeSmall)
                                .foregroundStyle(Palette.textTertiary)
                        }
                        HStack {
                            TextField("A copy's name", text: $model.copyName).textFieldStyle(.roundedBorder).frame(width: 200)
                            Button("Copy") { Task { await model.copy() } }.disabled(model.copyName.trimmingCharacters(in: .whitespaces).isEmpty)
                        }
                        Divider()
                        Text("PLAY").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                        Text(symphony.inputDescription).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                        TextField("Its input", text: $model.input, axis: .vertical).lineLimit(3...8).textFieldStyle(.roundedBorder)
                        HStack {
                            Button("Play") { model.playIt() }.disabled(model.isPlaying || model.input.trimmingCharacters(in: .whitespaces).isEmpty)
                            if let playing = model.shownProgress {
                                ProgressView().controlSize(.small)
                                Text(playing.busy ?? "starting").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textSecondary).lineLimit(1)
                                Button("Stop") { Task { await model.stop() } }
                            } else if model.isPlaying, let other = model.playingName {
                                Text("\(other) is playing -- one play at a time").font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                            }
                        }
                        if let play = model.shownPlay {
                            ForEach(play.stages) { stage in
                                VStack(alignment: .leading, spacing: 2) {
                                    Text("\(stage.name) · \(stage.role) (\(stage.backend))").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                                    Text(stage.answer).font(.apogeeSmall).textSelection(.enabled)
                                }
                            }
                            Text("OUTPUT").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                            Text(play.output).font(.apogeeBody).textSelection(.enabled)
                        }
                    }
                    .padding(Spacing.xl)
                }
            } else {
                Text("Choose a symphony.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary).padding(Spacing.xl)
            }
        }
        .confirmationDialog(
            "Delete the symphony \(model.pendingDelete?.name ?? "")?",
            isPresented: Binding(get: { model.pendingDelete != nil }, set: { if !$0 { model.pendingDelete = nil } }),
            titleVisibility: .visible
        ) {
            Button("Delete its config entry", role: .destructive) { Task { await model.confirmDelete() } }
            Button("Cancel", role: .cancel) { model.pendingDelete = nil }
        }
    }
}

struct MCPView: View {
    @Bindable var model: MCPViewModel
    @State private var names: [String: String] = [:]

    var body: some View {
        ListPage(rows: model.servers, selected: model.selected, pick: { model.selected = $0.name }, note: model.serversText) { server in
            VStack(alignment: .leading, spacing: 2) {
                Text(server.name).font(.apogeeBody)
                Text((server.enabled ? server.state : "disabled") + " · \(server.tools.count) tool\(server.tools.count == 1 ? "" : "s")")
                    .font(.apogeeMeta)
                    .foregroundStyle(server.state == "connected" || !server.enabled ? Palette.textTertiary : Palette.error)
            }
        } page: {
            ScrollView {
                VStack(alignment: .leading, spacing: Spacing.m) {
                    if let server = model.current {
                        HStack {
                            Text(server.name).font(.apogeeHeading)
                            Spacer()
                            Button(server.enabled ? "Disable" : "Enable") { Task { await model.setEnabled(server, !server.enabled) } }
                        }
                        Text(([server.command] + server.args).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
                        Text("Environment: " + (server.envSet ? "set (its values are never shown)" : "none")).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                        if let error = server.error { Text(error).font(.apogeeSmall).foregroundStyle(Palette.error) }
                        Text("TOOLS").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                        Picker("Tool", selection: $model.tool) {
                            Text("Choose a tool").tag("")
                            ForEach(server.tools, id: \.self) { Text($0).tag($0) }
                        }
                        .fixedSize()
                        TextField("Its arguments, as JSON", text: $model.arguments).textFieldStyle(.roundedBorder).font(.apogeeCode)
                        Button("Try it") { Task { await model.tryTool() } }.disabled(model.tool.isEmpty)
                        if let tried = model.tried {
                            Text(tried).font(.apogeeCode).textSelection(.enabled).padding(Spacing.m).frame(maxWidth: .infinity, alignment: .leading).background(Palette.window, in: RoundedRectangle(cornerRadius: Radius.card))
                        }
                        Divider()
                    }
                    Text("PRESETS APOGEE SHIPS").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                    if let presetsText = model.presetsText {
                        Text(presetsText).font(.apogeeSmall).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
                    }
                    ForEach(model.presets) { preset in
                        VStack(alignment: .leading, spacing: Spacing.xs) {
                            Text(preset.name).font(.apogeeBody.weight(.medium))
                            Text(preset.description).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                            Text("Provides: \(preset.provides)").font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                            if let refusal = preset.refusal {
                                Text(refusal).font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
                            } else {
                                HStack {
                                    TextField("Its name here", text: Binding(get: { names[preset.name] ?? "" }, set: { names[preset.name] = $0 }))
                                        .textFieldStyle(.roundedBorder)
                                        .frame(width: 180)
                                    Button("Register") {
                                        Task { if await model.register(preset, as: names[preset.name] ?? "") { names[preset.name] = nil } }
                                    }
                                        .disabled((names[preset.name] ?? "").trimmingCharacters(in: .whitespaces).isEmpty)
                                }
                            }
                        }
                    }
                }
                .padding(Spacing.xl)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
    }
}

/// A new agent: the scaffold's create, its command as typed.
struct AgentSheet: View {
    @Bindable var model: AgentsViewModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("New agent").font(.apogeeHeading)
            TextField("Name", text: $model.createForm.name).textFieldStyle(.roundedBorder)
            TextField("What it does", text: $model.createForm.description).textFieldStyle(.roundedBorder)
            TextField("Backend (blank for the default)", text: $model.createForm.model).textFieldStyle(.roundedBorder)
            Picker("Tools", selection: $model.createForm.tools) {
                Text("Read-only (the default)").tag(String?.none)
                Text("All").tag(String?.some("all"))
                Text("None").tag(String?.some("none"))
            }
            Toggle("Prose only: no output schema", isOn: $model.createForm.noSchema)
            if let arguments = model.createForm.arguments {
                Text((["apogee"] + arguments).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary)
            }
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Create") { Task { await model.create() } }.keyboardShortcut(.defaultAction).disabled(model.createForm.arguments == nil)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 520)
    }
}

/// A definition file in the app's editor: saved to the file the CLI reads,
/// then the binary's word on it.
struct EditorSheet: View {
    @Binding var editor: DefinitionEditor
    let save: () async -> Void
    let restore: (() async -> Void)?
    let close: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text(editor.title).font(.apogeeHeading)
            Text(editor.path).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
            TextEditor(text: $editor.text)
                .font(.apogeeCode)
                .frame(minHeight: 320)
                .overlay(RoundedRectangle(cornerRadius: Radius.card).strokeBorder(Palette.border))
                .disabled(editor.saving)
                .opacity(editor.saving ? 0.6 : 1)
            if editor.saving {
                HStack(spacing: Spacing.s) {
                    ProgressView().controlSize(.small)
                    Text("Saving: the editor waits for apogee's word on the file, then takes your typing again.")
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textSecondary)
                }
            } else if let verdict = editor.verdict {
                Text(verdict).font(.apogeeSmall).foregroundStyle(editor.refused ? Palette.error : Palette.textSecondary).textSelection(.enabled)
            }
            HStack {
                if let restore, editor.refused { Button("Put it back as it was") { Task { await restore() } }.disabled(editor.saving) }
                Spacer()
                Button("Close", action: close).keyboardShortcut(.cancelAction)
                Button("Save") { Task { await save() } }
                    .keyboardShortcut(.defaultAction)
                    .disabled(editor.saving || (!editor.changed && editor.verdict == nil))
            }
        }
        .padding(Spacing.xl)
        .frame(width: 720)
    }
}

#if DEBUG
#Preview("Agents, triage open") {
    let agents = LibraryPreview.agents()
    LibraryScreen(agents: agents, symphonies: LibraryPreview.symphonies(), mcp: LibraryPreview.mcp())
        .frame(width: 1100, height: 640)
        .task {
            await agents.connect(binary: previewBinary)
            agents.selected = "triage"
        }
}

#Preview("A list beside a page") {
    ListPage(rows: LibraryPreview.agentRows, selected: "triage", pick: { _ in }) { agent in
        Text(agent.name).font(.apogeeBody)
    } page: {
        Text("triage").font(.apogeeHeading).padding(Spacing.xl)
    }
    .frame(width: 900, height: 480)
}

#Preview("Agents: a shipped one, and the person's own") {
    let agents = LibraryPreview.agents()
    AgentsView(model: agents)
        .frame(width: 1000, height: 560)
        .task {
            await agents.connect(binary: previewBinary)
            agents.selected = "triage"
        }
}

#Preview("Symphonies: one played") {
    let symphonies = LibraryPreview.symphonies()
    SymphoniesView(model: symphonies)
        .frame(width: 1000, height: 760)
        .task {
            await symphonies.connect(binary: previewBinary)
            if let critique = symphonies.symphonies.first(where: { $0.name == "critique-revise" }) {
                symphonies.select(critique)
                symphonies.input = "A draft README for the tool."
                symphonies.playIt()
            }
        }
}

#Preview("Symphonies: playing") {
    let symphonies = LibraryPreview.symphonies(playing: true)
    SymphoniesView(model: symphonies)
        .frame(width: 1000, height: 600)
        .task {
            await symphonies.connect(binary: previewBinary)
            if let critique = symphonies.symphonies.first(where: { $0.name == "critique-revise" }) {
                symphonies.select(critique)
                symphonies.input = "A draft README for the tool."
                symphonies.playIt()
            }
        }
}

#Preview("Agents: the list could not be read") {
    let agents = LibraryPreview.agents(runner: LibraryPreview.badConfig)
    AgentsView(model: agents)
        .frame(width: 1000, height: 360)
        .task { await agents.connect(binary: previewBinary) }
}

#Preview("Agents: Apogee not installed") {
    let agents = LibraryPreview.agents()
    AgentsView(model: agents)
        .frame(width: 1000, height: 300)
        .task { await agents.connect(binary: nil) }
}

#Preview("Symphonies: the list could not be read") {
    let symphonies = LibraryPreview.symphonies(runner: LibraryPreview.badConfig)
    SymphoniesView(model: symphonies)
        .frame(width: 1000, height: 360)
        .task { await symphonies.connect(binary: previewBinary) }
}

#Preview("Symphonies: another one playing") {
    let symphonies = LibraryPreview.symphonies(playing: true)
    SymphoniesView(model: symphonies)
        .frame(width: 1000, height: 600)
        .task {
            await symphonies.connect(binary: previewBinary)
            if let critique = symphonies.symphonies.first(where: { $0.name == "critique-revise" }),
                let other = symphonies.symphonies.first(where: { $0.name != "critique-revise" })
            {
                symphonies.select(critique)
                symphonies.input = "A draft README for the tool."
                symphonies.playIt()
                symphonies.select(other)
            }
        }
}

#Preview("MCP: the servers could not be read") {
    let mcp = LibraryPreview.mcp(runner: LibraryPreview.badConfig)
    MCPView(model: mcp)
        .frame(width: 1000, height: 560)
        .task {
            mcp.use(binary: previewBinary)
            await mcp.refresh()
        }
}

#Preview("MCP servers and presets") {
    let mcp = LibraryPreview.mcp()
    MCPView(model: mcp)
        .frame(width: 1000, height: 620)
        .task {
            mcp.use(binary: previewBinary)
            await mcp.refresh()
            mcp.selected = "code"
        }
}

#Preview("New agent") {
    let agents = LibraryPreview.agents()
    AgentSheet(model: agents)
        .task {
            agents.createForm.name = "triage"
            agents.createForm.description = "Sort incoming issues by area"
        }
}

#Preview("Editing the person's own prompt") {
    EditorSheet(
        editor: .constant(
            DefinitionEditor(
                title: "triage", path: "/Users/someone/.apogee/prompts/triage.txt",
                original: LibraryPreview.triagePrompt, text: LibraryPreview.triagePrompt + "\nFile each issue under one area.\n")),
        save: {}, restore: nil, close: {})
}

#Preview("Saving: the editor read-only until apogee answers") {
    EditorSheet(
        editor: .constant(
            DefinitionEditor(
                title: "triage", path: "/Users/someone/.apogee/prompts/triage.txt",
                original: LibraryPreview.triagePrompt, text: LibraryPreview.triagePrompt + "\nFile each issue under one area.\n", saving: true)),
        save: {}, restore: nil, close: {})
}
#endif
