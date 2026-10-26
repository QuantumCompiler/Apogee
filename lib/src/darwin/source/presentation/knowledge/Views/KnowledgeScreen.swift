import AppKit
import BusinessJobs
import BusinessReads
import PresentationShell
import PresentationStyles
import SwiftUI

/// The Knowledge place (⌘8): the decision records and the collections, each
/// over its own verbs.
public struct KnowledgeScreen: View {
    public enum Tab: String, CaseIterable, Identifiable {
        case records
        case collections
        public var id: String { rawValue }
        var title: String { self == .records ? "Records" : "Collections" }
    }

    @Bindable private var records: RecordsViewModel
    @Bindable private var collections: CollectionsViewModel
    @State private var tab: Tab = .records

    public init(records: RecordsViewModel, collections: CollectionsViewModel) {
        self.records = records
        self.collections = collections
    }

    public var body: some View {
        VStack(spacing: 0) {
            ScreenHeader(title: "Knowledge") {
                Picker("Show", selection: $tab) {
                    ForEach(Tab.allCases) { tab in Text(tab.title).tag(tab) }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .fixedSize()
                switch tab {
                case .records:
                    Button("Capture…") { records.newCapture() }.disabled(records.isRunning)
                    Menu("More") {
                        Button("Export as Markdown…") { export(markdown: true) }
                        Button("Export as JSON…") { export(markdown: false) }
                        Divider()
                        Button("Reindex the records' vectors") { records.reindex() }.disabled(records.isRunning)
                    }
                    .fixedSize()
                case .collections:
                    Button("Ingest…") { collections.newIngest() }.disabled(collections.isRunning)
                }
            }
            Divider()
            switch tab {
            case .records: RecordsView(model: records)
            case .collections: CollectionsView(model: collections)
            }
        }
        .sheet(isPresented: $records.capturing) { CaptureSheet(model: records) }
        .sheet(isPresented: $collections.ingesting) { IngestSheet(model: collections) }
        .alert("That didn't work", isPresented: Binding(get: { records.failure != nil }, set: { if !$0 { records.dismissFailure() } })) {
            Button("OK") { records.dismissFailure() }
        } message: {
            Text(records.failure ?? "")
        }
        .alert("That didn't work", isPresented: Binding(get: { collections.failure != nil }, set: { if !$0 { collections.dismissFailure() } })) {
            Button("OK") { collections.dismissFailure() }
        } message: {
            Text(collections.failure ?? "")
        }
    }

    /// Where the export goes, asked of the person; then the CLI writes it.
    private func export(markdown: Bool) {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = markdown ? "knowledge.md" : "knowledge.json"
        let anonymize = NSButton(checkboxWithTitle: "Anonymize: strip attribution and the local archive path", target: nil, action: nil)
        panel.accessoryView = anonymize
        guard panel.runModal() == .OK, let url = panel.url else { return }
        Task { await records.export(to: url, markdown: markdown, anonymize: anonymize.state == .on) }
    }
}

/// The records: the list by branch, a question answered with scores, a
/// record's page with its changes.
struct RecordsView: View {
    @Bindable var model: RecordsViewModel

    var body: some View {
        HStack(spacing: 0) {
            VStack(spacing: 0) {
                HStack(spacing: Spacing.s) {
                    TextField("Ask the records", text: $model.question)
                        .textFieldStyle(.roundedBorder)
                        .onSubmit { Task { await model.search() } }
                    if model.query != nil { Button("Clear") { model.clearSearch() } }
                    Picker("Branch", selection: $model.branch) {
                        Text("Every branch").tag(String?.none)
                        ForEach(KnowledgeRecord.statuses, id: \.self) { Text($0.capitalized).tag(String?.some($0)) }
                    }
                    .labelsHidden()
                    .fixedSize()
                }
                .padding(Spacing.m)
                if let run = model.run {
                    RunLine(progress: run) { Task { await model.stop() } }
                }
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 1) {
                        if let query = model.query {
                            Text("\(query.matches.count) found · \(query.retriever)\(query.reranked ? " · reranked" : "")")
                                .font(.apogeeMeta)
                                .foregroundStyle(Palette.textTertiary)
                                .padding(.horizontal, Spacing.m)
                            ForEach(query.matches) { match in
                                row(match.record, score: RecordsViewModel.score(match))
                            }
                        } else {
                            ForEach(model.shown) { record in row(record, score: nil) }
                        }
                        if model.query == nil, let empty = model.emptyText {
                            Text(empty).font(.apogeeSmall).foregroundStyle(Palette.textTertiary).padding(Spacing.m)
                        }
                    }
                    .padding(Spacing.s)
                }
            }
            .frame(minWidth: 360)
            Divider()
            Group {
                if let record = model.selectedRecord {
                    RecordPage(model: model, record: record)
                } else {
                    Text("Choose a record to read its reasoning and its decision.")
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textTertiary)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
            .frame(width: 380)
            .background(Palette.inspector)
        }
        .confirmationDialog(
            model.pendingDelete.map { "Delete \($0.id)?" } ?? "",
            isPresented: Binding(get: { model.pendingDelete != nil }, set: { if !$0 { model.pendingDelete = nil } }),
            titleVisibility: .visible
        ) {
            Button("Delete the record and its archive", role: .destructive) { Task { await model.confirmDelete() } }
            Button("Cancel", role: .cancel) { model.pendingDelete = nil }
        } message: {
            Text("apogee knowledge delete \(model.pendingDelete?.id ?? "") -- the record and its raw conversation are removed; this can't be undone.")
        }
    }

    private func row(_ record: KnowledgeRecord, score: String?) -> some View {
        Button {
            model.select(record)
        } label: {
            VStack(alignment: .leading, spacing: 2) {
                HStack {
                    Text(record.decision).font(.apogeeBody).lineLimit(2)
                    Spacer(minLength: Spacing.s)
                    if let score { Text(score).font(.apogeeMeta.monospacedDigit()).foregroundStyle(Palette.textTertiary) }
                }
                Text([record.status, record.discipline, String(record.timestamp.prefix(10))].filter { !$0.isEmpty }.joined(separator: " · "))
                    .font(.apogeeMeta)
                    .foregroundStyle(record.status == "shipped" ? Palette.textSecondary : Palette.textTertiary)
            }
            .padding(Spacing.m)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(model.selected == record.id ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// One record: the decision, its reasoning, its branch and provenance, and
/// what can be done.
struct RecordPage: View {
    let model: RecordsViewModel
    let record: KnowledgeRecord
    @State private var link = ""

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.l) {
                VStack(alignment: .leading, spacing: Spacing.xs) {
                    Text(record.decision).font(.apogeeHeading).textSelection(.enabled)
                    Text(record.id).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
                }
                fact("Why", record.intent)
                fact("Branch", record.status)
                fact("Discipline", record.discipline)
                fact("From", [record.source, record.attribution].filter { !$0.isEmpty }.joined(separator: " · "))
                fact("Linked to", record.link.isEmpty ? "nothing yet" : record.link)
                if let supersedes = record.supersedes { fact("Replaces", supersedes) }
                fact("Captured", record.timestamp)
                if record.rawRef != nil {
                    RawSection(model: model, record: record)
                }
                Divider()
                HStack {
                    TextField("Link it to a ticket, a commit, a file", text: $link).textFieldStyle(.roundedBorder)
                    Button("Link") { Task { await model.link(record, to: link); link = "" } }
                        .disabled(link.trimmingCharacters(in: .whitespaces).isEmpty)
                }
                HStack {
                    Menu("Move to") {
                        ForEach(KnowledgeRecord.statuses.filter { $0 != record.status }, id: \.self) { status in
                            Button(status.capitalized) { Task { await model.move(record, to: status) } }
                        }
                    }
                    .fixedSize()
                    Spacer()
                    Button("Delete…", role: .destructive) { model.pendingDelete = record }
                }
                .disabled(model.working != nil)
            }
            .padding(Spacing.l)
        }
    }

    private func fact(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label.uppercased()).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
            Text(value.isEmpty ? "—" : value).font(.apogeeBody).textSelection(.enabled)
        }
    }
}

/// A record's archived raw conversation, asked of the CLI when the person
/// wants it -- `knowledge info --raw` -- and shown as it printed it.
struct RawSection: View {
    let model: RecordsViewModel
    let record: KnowledgeRecord

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.xs) {
            HStack {
                Text("RAW CONVERSATION").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                Spacer()
                if let raw = model.raw, raw.record == record.id, raw.state != .reading {
                    Button("Hide") { model.hideRaw() }.font(.apogeeSmall)
                }
            }
            switch model.raw?.record == record.id ? model.raw?.state : nil {
            case .reading?:
                ProgressView().controlSize(.small)
            case .shown(.text(let text))?:
                Text(text)
                    .font(.apogeeCode)
                    .textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
                    .padding(Spacing.m)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(Palette.window, in: RoundedRectangle(cornerRadius: Radius.card))
            case .shown(.unavailable)?:
                Text("apogee can't read its archive: the raw conversation is unavailable.")
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textTertiary)
            case .failed(let message)?:
                Text(message).font(.apogeeSmall).foregroundStyle(Palette.error).textSelection(.enabled)
            case nil:
                Button("Show the conversation it was distilled from") { Task { await model.showRaw(record) } }
                    .font(.apogeeSmall)
            }
        }
    }
}

/// The collections: the store's rows, a collection's health and sources, a
/// search as the CLI prints it.
struct CollectionsView: View {
    @Bindable var model: CollectionsViewModel

    var body: some View {
        HStack(spacing: 0) {
            VStack(spacing: 0) {
                if let run = model.run {
                    RunLine(progress: run) { Task { await model.stop() } }
                }
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 1) {
                        ForEach(model.collections) { collection in
                            Button {
                                Task { await model.select(collection) }
                            } label: {
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(collection.name).font(.apogeeBody)
                                    Text("\(collection.sources) source\(collection.sources == 1 ? "" : "s") · \(collection.chunks) chunk\(collection.chunks == 1 ? "" : "s")")
                                        .font(.apogeeMeta)
                                        .foregroundStyle(Palette.textTertiary)
                                }
                                .padding(Spacing.m)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .background(model.selected == collection.name ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                                .contentShape(Rectangle())
                            }
                            .buttonStyle(.plain)
                        }
                        if let empty = model.emptyText {
                            Text(empty).font(.apogeeSmall).foregroundStyle(Palette.textTertiary).padding(Spacing.m)
                        }
                    }
                    .padding(Spacing.s)
                }
            }
            .frame(width: 280)
            .background(Palette.sidebar)
            Divider()
            if let info = model.info {
                CollectionPage(model: model, info: info)
            } else {
                Text("Choose a collection to see its sources and search it.")
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textTertiary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .confirmationDialog(
            deleteTitle,
            isPresented: Binding(get: { model.pendingDelete != nil }, set: { if !$0 { model.pendingDelete = nil } }),
            titleVisibility: .visible
        ) {
            Button("Delete", role: .destructive) { Task { await model.confirmDelete() } }
            Button("Cancel", role: .cancel) { model.pendingDelete = nil }
        } message: {
            Text("apogee " + (model.pendingDelete?.arguments.joined(separator: " ") ?? "") + " -- this can't be undone.")
        }
    }

    private var deleteTitle: String {
        guard let pending = model.pendingDelete else { return "" }
        return pending.source.map { "Remove \($0) from \(pending.collection)?" } ?? "Delete the collection \(pending.collection)?"
    }
}

/// One collection: its health, its sources, and a search.
struct CollectionPage: View {
    @Bindable var model: CollectionsViewModel
    let info: CollectionInfo

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.l) {
                HStack(alignment: .firstTextBaseline) {
                    Text(info.name).font(.apogeeHeading)
                    Spacer()
                    Button("Ingest more…") { model.newIngest(into: info.name) }.disabled(model.isRunning)
                    Button("Delete…", role: .destructive) { model.pendingDelete = .init(collection: info.name, source: nil) }
                }
                HStack(spacing: Spacing.xl) {
                    fact("Chunks", "\(info.chunks)")
                    fact("Text index", info.index)
                    fact("Vectors", CollectionsViewModel.vectors(info))
                }
                VStack(alignment: .leading, spacing: Spacing.s) {
                    HStack {
                        TextField("Search \(info.name)", text: $model.question)
                            .textFieldStyle(.roundedBorder)
                            .onSubmit { Task { await model.search() } }
                        Button("Search") { Task { await model.search() } }
                    }
                    if let results = model.results {
                        Text(results)
                            .font(.apogeeCode)
                            .textSelection(.enabled)
                            .padding(Spacing.m)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(Palette.window, in: RoundedRectangle(cornerRadius: Radius.card))
                    }
                }
                VStack(alignment: .leading, spacing: Spacing.xs) {
                    Text("SOURCES · \(info.sources.count)").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                    ForEach(info.sources, id: \.self) { source in
                        HStack {
                            Text(source).font(.apogeeBody).textSelection(.enabled)
                            Spacer()
                            Button("Remove…") { model.pendingDelete = .init(collection: info.name, source: source) }
                                .buttonStyle(.borderless)
                                .font(.apogeeSmall)
                        }
                    }
                }
            }
            .padding(Spacing.xl)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private func fact(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label.uppercased()).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
            Text(value).font(.apogeeBody)
        }
    }
}

/// A capture or an ingest running: its busy line and Stop.
struct RunLine: View {
    let progress: JobProgress
    let stop: () -> Void

    var body: some View {
        HStack(spacing: Spacing.s) {
            ProgressView().controlSize(.small)
            Text(progress.busy ?? progress.notices.last ?? "Working")
                .font(.apogeeMeta.monospaced())
                .foregroundStyle(Palette.textSecondary)
                .lineLimit(1)
                .truncationMode(.middle)
            Spacer()
            Button("Stop", action: stop)
        }
        .padding(.horizontal, Spacing.m)
        .padding(.vertical, Spacing.s)
    }
}

/// The capture sheet: the conversation, and the clerk's reading overridden
/// only where the person says.
struct CaptureSheet: View {
    @Bindable var model: RecordsViewModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("Capture a decision").font(.apogeeHeading)
            Text("The clerk -- your utility model -- distils the conversation into one record: the reasoning, the decision, its branch.")
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textSecondary)
            TextField("The conversation", text: $model.captureForm.text, axis: .vertical).lineLimit(4...10)
            HStack {
                Picker("Branch", selection: $model.captureForm.status) {
                    Text("As the clerk reads it").tag(String?.none)
                    ForEach(KnowledgeRecord.statuses, id: \.self) { Text($0).tag(String?.some($0)) }
                }
                Picker("Discipline", selection: $model.captureForm.discipline) {
                    Text("As the clerk reads it").tag(String?.none)
                    ForEach(KnowledgeRecord.disciplines, id: \.self) { Text($0).tag(String?.some($0)) }
                }
            }
            TextField("Linked to (optional)", text: $model.captureForm.link).textFieldStyle(.roundedBorder)
            if let arguments = model.captureForm.arguments {
                Text((["apogee"] + arguments).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary).lineLimit(3)
            }
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Capture") { model.capture() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(model.captureForm.arguments == nil)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 560)
    }
}

/// The ingest sheet: a collection, what to read, how to index it.
struct IngestSheet: View {
    @Bindable var model: CollectionsViewModel
    @State private var choosing = false
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("Ingest into a collection").font(.apogeeHeading)
            TextField("Collection", text: $model.ingestForm.collection).textFieldStyle(.roundedBorder)
            HStack {
                Text(model.ingestForm.path.isEmpty ? "Nothing chosen" : model.ingestForm.path)
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textSecondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
                Spacer()
                Button("Choose a file or folder…") { choosing = true }
            }
            Picker("Index", selection: $model.ingestForm.retriever) {
                Text("The collection's own").tag(String?.none)
                Text("Text only (lexical)").tag(String?.some("lexical"))
                Text("Also embed every chunk (vector)").tag(String?.some("vector"))
                Text("Auto").tag(String?.some("auto"))
            }
            Toggle("Build the collection's graph after (a model reads every chunk)", isOn: $model.ingestForm.graph)
            if let arguments = model.ingestForm.arguments {
                Text((["apogee"] + arguments).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary).lineLimit(3)
            }
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Ingest") { model.ingest() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(model.ingestForm.arguments == nil)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 560)
        .fileImporter(isPresented: $choosing, allowedContentTypes: [.item, .folder]) { result in
            if case .success(let url) = result { model.ingestForm.path = url.path }
        }
    }
}

#if DEBUG
#Preview("Records, one open") {
    let records = KnowledgePreview.records()
    KnowledgeScreen(records: records, collections: KnowledgePreview.collections())
        .frame(width: 1100, height: 640)
        .task {
            await records.connect(binary: previewBinary)
            records.select(KnowledgePreview.record("kr-20261021T045944Z-3346a9"))
        }
}

#Preview("A fresh install: no records yet") {
    let records = KnowledgePreview.records(runner: KnowledgePreview.fresh)
    KnowledgeScreen(records: records, collections: KnowledgePreview.collections(runner: KnowledgePreview.fresh))
        .frame(width: 1100, height: 480)
        .task { await records.connect(binary: previewBinary) }
}

#Preview("Apogee not installed") {
    let records = KnowledgePreview.records()
    KnowledgeScreen(records: records, collections: KnowledgePreview.collections())
        .frame(width: 1100, height: 480)
        .task { await records.connect(binary: nil) }
}

#Preview("Records: every branch") {
    let records = KnowledgePreview.records()
    RecordsView(model: records)
        .frame(width: 900, height: 560)
        .task { await records.connect(binary: previewBinary) }
}

#Preview("Records: a question answered, with scores") {
    let records = KnowledgePreview.records()
    RecordsView(model: records)
        .frame(width: 900, height: 560)
        .task {
            await records.connect(binary: previewBinary)
            records.question = KnowledgePreview.recordsQuestion
            await records.search()
            if let first = records.query?.matches.first { records.select(first.record) }
        }
}

#Preview("Records: nothing captured yet") {
    let records = KnowledgePreview.records(runner: KnowledgePreview.fresh)
    RecordsView(model: records)
        .frame(width: 900, height: 420)
        .task { await records.connect(binary: previewBinary) }
}

#Preview("Records: a branch with none") {
    let records = KnowledgePreview.records()
    RecordsView(model: records)
        .frame(width: 900, height: 480)
        .task {
            await records.connect(binary: previewBinary)
            records.branch = "superseded"
        }
}

#Preview("Records: a capture running") {
    let records = KnowledgePreview.records()
    RecordsView(model: records)
        .frame(width: 900, height: 560)
        .task {
            await records.connect(binary: previewBinary)
            records.newCapture()
            records.captureForm.text = KnowledgePreview.conversation
            records.capture()
        }
}

#Preview("A shipped record, linked") {
    RecordPage(model: KnowledgePreview.records(), record: KnowledgePreview.record("kr-20261021T045944Z-3346a9"))
        .frame(width: 380, height: 640)
        .background(Palette.inspector)
}

#Preview("A record's raw conversation, as the CLI printed it") {
    let records = KnowledgePreview.records(runner: KnowledgePreview.rawRunner())
    RecordPage(model: records, record: KnowledgePreview.captured)
        .frame(width: 380, height: 760)
        .task { await KnowledgePreview.openRaw(records) }
}

#Preview("Raw conversation: not asked for yet") {
    RawSection(model: KnowledgePreview.records(runner: KnowledgePreview.rawRunner()), record: KnowledgePreview.captured)
        .padding(Spacing.l)
        .frame(width: 380)
}

#Preview("Raw conversation: shown") {
    let records = KnowledgePreview.records(runner: KnowledgePreview.rawRunner())
    RawSection(model: records, record: KnowledgePreview.captured)
        .padding(Spacing.l)
        .frame(width: 380)
        .task { await KnowledgePreview.openRaw(records) }
}

#Preview("Raw conversation: its archive unavailable") {
    let records = KnowledgePreview.records(runner: KnowledgePreview.rawRunner(unavailable: true))
    RawSection(model: records, record: KnowledgePreview.captured)
        .padding(Spacing.l)
        .frame(width: 380)
        .task { await KnowledgePreview.openRaw(records) }
}

#Preview("A rejected record, linked to nothing yet") {
    RecordPage(model: KnowledgePreview.records(), record: KnowledgePreview.record("kr-20261021T045944Z-1e2354"))
        .frame(width: 380, height: 640)
        .background(Palette.inspector)
}

#Preview("Collections: read, none chosen") {
    let collections = KnowledgePreview.collections()
    CollectionsView(model: collections)
        .frame(width: 1000, height: 560)
        .task { await collections.connect(binary: previewBinary) }
}

#Preview("Collections: notes, searched") {
    let collections = KnowledgePreview.collections()
    CollectionsView(model: collections)
        .frame(width: 1000, height: 620)
        .task {
            await KnowledgePreview.openNotes(collections)
            collections.question = KnowledgePreview.collectionQuestion
            await collections.search()
        }
}

#Preview("Collections: an ingest running") {
    let collections = KnowledgePreview.collections(said: [KnowledgePreview.ingesting])
    CollectionsView(model: collections)
        .frame(width: 1000, height: 620)
        .task {
            await KnowledgePreview.openNotes(collections)
            collections.newIngest(into: "notes")
            collections.ingestForm.path = "/Users/someone/notes"
            collections.ingest()
        }
}

#Preview("Collections: Apogee not installed") {
    let collections = KnowledgePreview.collections()
    CollectionsView(model: collections)
        .frame(width: 1000, height: 480)
        .task { await collections.connect(binary: nil) }
}

#Preview("A text-only collection") {
    CollectionPage(model: KnowledgePreview.collections(), info: KnowledgePreview.notes)
        .frame(width: 720, height: 560)
}

#Preview("A collection searched: the CLI's own lines") {
    let collections = KnowledgePreview.collections()
    CollectionPage(model: collections, info: KnowledgePreview.notes)
        .frame(width: 720, height: 720)
        .task {
            await KnowledgePreview.openNotes(collections)
            collections.question = KnowledgePreview.collectionQuestion
            await collections.search()
        }
}

#Preview("A run started, nothing said yet") {
    RunLine(progress: JobProgress()) {}
        .frame(width: 520)
}

#Preview("An ingest's busy line") {
    RunLine(progress: KnowledgePreview.progress([KnowledgePreview.ingesting])) {}
        .frame(width: 520)
}

#Preview("Capture: nothing written yet") {
    let records = KnowledgePreview.records()
    CaptureSheet(model: records)
        .task { records.newCapture() }
}

#Preview("Capture: the clerk overridden where the person says") {
    let records = KnowledgePreview.records()
    CaptureSheet(model: records)
        .task {
            records.newCapture()
            records.captureForm.text = KnowledgePreview.conversation
            records.captureForm.status = "rejected"
            records.captureForm.discipline = "ux"
            records.captureForm.link = "PROJ-9"
        }
}

#Preview("Ingest: nothing chosen yet") {
    let collections = KnowledgePreview.collections()
    IngestSheet(model: collections)
        .task { collections.newIngest() }
}

#Preview("Ingest: a folder, embedded, its graph after") {
    let collections = KnowledgePreview.collections()
    IngestSheet(model: collections)
        .task {
            collections.newIngest(into: "notes")
            collections.ingestForm.path = "/Users/someone/notes"
            collections.ingestForm.retriever = "vector"
            collections.ingestForm.graph = true
        }
}
#endif
