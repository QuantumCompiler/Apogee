import PresentationStyles
import SwiftUI
import UniformTypeIdentifiers

/// A backend's adapters, edited: each path and scale -- a GGUF LoRA file on a
/// llama.cpp entry, one adapter directory at its trained scale on an mlx one
/// -- added, changed and taken off, then set whole with `config set-backend
/// --adapters`, the command shown as typed. The CLI pins each to the weights
/// the entry runs now, and refuses in its words what it will not take.
struct AdaptersSheet: View {
    @Bindable var model: ModelsViewModel
    @State private var choosing = false

    var body: some View {
        if let form = model.editingAdapters {
            VStack(alignment: .leading, spacing: Spacing.m) {
                Text("Adapters on \(form.backend)").font(.apogeeHeading)
                Text(form.isMLX
                    ? "An mlx entry applies one adapter directory, at the scale it was trained at."
                    : "GGUF LoRA files, applied over the weights at load and never merged into them -- each at its scale, 1 unless given, up to 2.")
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textSecondary)
                    .fixedSize(horizontal: false, vertical: true)
                ForEach(form.entries) { entry in
                    HStack {
                        TextField("Path", text: binding(entry.id, \.path))
                        if !form.isMLX {
                            TextField("1", text: binding(entry.id, \.scale)).frame(width: 60)
                        }
                        Button {
                            model.editingAdapters?.remove(entry.id)
                        } label: {
                            Image(systemName: "minus.circle")
                        }
                        .buttonStyle(.borderless)
                        .accessibilityLabel("Take it off")
                    }
                    .textFieldStyle(.roundedBorder)
                }
                if form.entries.isEmpty {
                    Text("None: applying takes every adapter off (--adapters \"\").")
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textTertiary)
                }
                Button(form.isMLX ? "Add a directory…" : "Add a file…") { choosing = true }
                    .disabled(form.isMLX && !form.entries.isEmpty)
                Text((["apogee"] + form.arguments.dropLast() + [form.value.isEmpty ? "\"\"" : form.value]).joined(separator: " "))
                    .font(.apogeeCode)
                    .foregroundStyle(Palette.textTertiary)
                    .lineLimit(4)
                    .textSelection(.enabled)
                HStack {
                    Spacer()
                    Button("Cancel", role: .cancel) { model.editingAdapters = nil }.keyboardShortcut(.cancelAction)
                    Button(form.entries.isEmpty ? "Take them all off" : "Apply") { Task { await model.applyAdapters() } }
                        .keyboardShortcut(.defaultAction)
                }
            }
            .padding(Spacing.xl)
            .frame(width: 600)
            .fileImporter(isPresented: $choosing, allowedContentTypes: form.isMLX ? [.folder] : [.item]) { result in
                if case .success(let url) = result { model.editingAdapters?.add(url.path) }
            }
        }
    }

    private func binding(_ id: AdaptersForm.Entry.ID, _ key: WritableKeyPath<AdaptersForm.Entry, String>) -> Binding<String> {
        Binding(
            get: { model.editingAdapters?.entries.first { $0.id == id }?[keyPath: key] ?? "" },
            set: { value in
                guard let index = model.editingAdapters?.entries.firstIndex(where: { $0.id == id }) else { return }
                model.editingAdapters?.entries[index][keyPath: key] = value
            })
    }
}

#if DEBUG
#Preview("Two adapters, one at half scale") {
    let models = ModelsPreview.adapters()
    AdaptersSheet(model: models)
        .task { await ModelsPreview.editingAdapters(models) }
}

#Preview("Every adapter taken off") {
    let models = ModelsPreview.adapters()
    AdaptersSheet(model: models)
        .task {
            await ModelsPreview.editingAdapters(models)
            for entry in models.editingAdapters?.entries ?? [] { models.editingAdapters?.remove(entry.id) }
        }
}
#endif
