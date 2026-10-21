import PresentationStyles
import SwiftUI

/// The message field, and Send -- or Stop while a turn is in flight.
struct InputBar: View {
    @Bindable var model: SessionViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.s) {
        SuggestionList(model: model)
        AttachmentChips(model: model)
        HStack(alignment: .bottom, spacing: Spacing.m) {
            Button {
                model.presenting = .files
            } label: {
                Image(systemName: "paperclip")
            }
            .buttonStyle(.plain)
            .foregroundStyle(Palette.textSecondary)
            .padding(.bottom, Spacing.m)
            .disabled(!isLive)
            .help("Attach a file, folder, image, recording or video")
            .accessibilityLabel("Attach")
            TextField(model.inputPrompt, text: $model.draft, axis: .vertical)
                .textFieldStyle(.plain)
                .font(.apogeeProse)
                .lineLimit(1...8)
                .disabled(!model.transcript.acceptsInput)
                .onSubmit {
                    // ⏎ on a suggestion runs it; otherwise it sends.
                    if let command = model.slashSuggestions.first {
                        model.run(command)
                    } else if let path = model.mentionSuggestions.first {
                        model.complete(mention: path)
                    } else {
                        model.submit()
                    }
                }
                .padding(Spacing.m)
                .background(Palette.control, in: RoundedRectangle(cornerRadius: Radius.control))
            if model.transcript.turnInFlight || isStopping {
                Button("Stop", systemImage: "stop.fill") { model.cancel() }
                    .disabled(!model.canCancel)
                    .keyboardShortcut(".", modifiers: .command)
                    .help("Stop the turn (Esc or ⌘.)")
            } else {
                Button("Send", systemImage: "arrow.up") { model.submit() }
                    .disabled(!model.canSend)
                    .keyboardShortcut(.return, modifiers: .command)
            }
        }
        }
        .padding(Spacing.l)
        .frame(maxWidth: Layout.conversationMax)
        .frame(maxWidth: .infinity)
    }

    private var isLive: Bool {
        switch model.transcript.status {
        case .connecting, .paused, .ended: false
        default: true
        }
    }

    private var isStopping: Bool {
        if case .stopping = model.transcript.status { true } else { false }
    }
}
