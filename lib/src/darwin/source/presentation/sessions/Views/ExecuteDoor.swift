import BusinessSession
import PresentationStyles
import SwiftUI

/// The execute door: `apogee execute` with a suite or bare on one model, the
/// folder it opens in, and orchestrate -- the command shown as it will run.
struct ExecuteDoorSheet: View {
    @Bindable var library: SessionLibraryViewModel
    @State private var choosingFolder = false
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.l) {
            VStack(alignment: .leading, spacing: Spacing.xs) {
                Text("New execute session").font(.apogeeHeading)
                Text("A session under a suite, its symphonies playable with /play -- or bare on one model.")
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textSecondary)
            }
            Grid(alignment: .leadingFirstTextBaseline, horizontalSpacing: Spacing.m, verticalSpacing: Spacing.m) {
                GridRow {
                    Text("Suite").foregroundStyle(Palette.textSecondary)
                    Picker("Suite", selection: $library.executeForm.suite) {
                        Text(configuredLabel).tag(String?.none)
                        ForEach(library.suites, id: \.self) { suite in
                            Text(suite).tag(String?.some(suite))
                        }
                    }
                    .labelsHidden()
                    .frame(maxWidth: 280, alignment: .leading)
                }
                GridRow {
                    Text("Model").foregroundStyle(Palette.textSecondary)
                    Picker("Model", selection: $library.executeForm.model) {
                        Text(library.executeForm.suite == nil ? "The default backend" : "The suite's chat member").tag("")
                        ForEach(library.modelChoices, id: \.self) { backend in
                            Text(backend).tag(backend)
                        }
                    }
                    .labelsHidden()
                    .frame(maxWidth: 280, alignment: .leading)
                }
                GridRow {
                    Text("Folder").foregroundStyle(Palette.textSecondary)
                    HStack(spacing: Spacing.s) {
                        Text(library.executeForm.folder?.path ?? "Your home folder")
                            .font(.apogeeCode)
                            .lineLimit(1)
                            .truncationMode(.middle)
                        Button("Choose…") { choosingFolder = true }
                        if library.executeForm.folder != nil {
                            Button("Clear") { library.executeForm.folder = nil }
                        }
                    }
                }
                GridRow {
                    Text("")
                    Toggle("Orchestrate: offer the suite's symphonies to its chat model, as tools", isOn: $library.executeForm.orchestrate)
                        .disabled(!library.canOrchestrate)
                }
            }
            .font(.apogeeSmall)
            Text(note)
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textTertiary)
                .fixedSize(horizontal: false, vertical: true)
            Text(command)
                .font(.apogeeCode)
                .foregroundStyle(Palette.textSecondary)
                .textSelection(.enabled)
                .padding(Spacing.s)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Palette.code, in: RoundedRectangle(cornerRadius: Radius.control))
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Button("Open") { Task { await library.openExecute() } }
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 560)
        .fileImporter(isPresented: $choosingFolder, allowedContentTypes: [.folder]) { result in
            if case .success(let url) = result { library.executeForm.folder = url }
        }
    }

    private var configuredLabel: String {
        switch library.defaultSuite {
        case .some(let suite) where !suite.isEmpty: "The default suite (\(suite))"
        case .some: "No suite: bare on one model"
        case .none: "As the config says"
        }
    }

    /// What opening in a folder brings, in the CLI's own terms.
    private var note: String {
        var lines = [
            "Opened in a project -- a git repository, or a folder holding AGENTS.md or CLAUDE.md -- the session reads the file into every request, carries the coder persona, and attaches the folder; its notices say so."
        ]
        if !library.canOrchestrate {
            lines.append("Orchestrate offers a suite's symphonies, and this session would run under none.")
        }
        return lines.joined(separator: " ")
    }

    private var command: String {
        let launch = library.executeForm.launch(orchestrates: library.canOrchestrate)
        let words = launch.arguments.filter { $0 != "--output-format" && $0 != "--input-format" && $0 != "stream-json" }
        let line = (["apogee"] + words).joined(separator: " ")
        guard let folder = library.executeForm.folder else { return line }
        return "cd \(folder.path) && \(line)"
    }
}
