import PresentationStyles
import SwiftUI

/// What the CLI said of a delete beyond what it removed -- a backend kept,
/// pointing at nothing, with its two doors; the backends removed with it --
/// every line as printed, until dismissed (GUI 4e). Stored weights and the
/// Backends tab both draw it.
struct DeleteNarrationCard: View {
    let narration: DeleteNarration
    let dismiss: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.s) {
            HStack(alignment: .firstTextBaseline) {
                Text(narration.title).font(.apogeeBody.weight(.semibold)).foregroundStyle(Palette.textPrimary)
                    .lineLimit(1).truncationMode(.middle)
                Spacer()
                Button("Dismiss", action: dismiss)
            }
            Text(narration.lines.joined(separator: "\n"))
                .font(.apogeeCode)
                .foregroundStyle(Palette.textSecondary)
                .textSelection(.enabled)
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(Spacing.s)
                .background(Palette.code, in: RoundedRectangle(cornerRadius: Radius.control))
        }
        .padding(Spacing.l)
        .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.card))
        .overlay(RoundedRectangle(cornerRadius: Radius.card).strokeBorder(Palette.border))
    }
}

#if DEBUG
#Preview("A delete's words: its backend kept, with the CLI's two doors") {
    if let narration = DeleteNarration(title: "Deleted \(ModelsPreview.deleteGemmaQ4)", said: ModelsDocuments.deleteKept) {
        DeleteNarrationCard(narration: narration) {}
            .padding(Spacing.xl)
            .frame(width: 860)
    }
}

#Preview("A delete's words: the backend removed with it") {
    if let narration = DeleteNarration(title: "Deleted \(ModelsPreview.deleteLlamaQ4)", said: ModelsDocuments.deleteAndBackends) {
        DeleteNarrationCard(narration: narration) {}
            .padding(Spacing.xl)
            .frame(width: 860)
    }
}
#endif
