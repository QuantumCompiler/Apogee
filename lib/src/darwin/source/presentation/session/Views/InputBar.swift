import PresentationStyles
import SwiftUI

/// The message field, and Send -- or Stop while a turn is in flight.
struct InputBar: View {
    @Bindable var model: SessionViewModel

    var body: some View {
        HStack(alignment: .bottom, spacing: Spacing.m) {
            TextField(model.inputPrompt, text: $model.draft, axis: .vertical)
                .textFieldStyle(.plain)
                .font(.apogeeProse)
                .lineLimit(1...8)
                .disabled(!model.transcript.acceptsInput)
                .onSubmit { model.submit() }
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
        .padding(Spacing.l)
        .frame(maxWidth: Layout.conversationMax)
        .frame(maxWidth: .infinity)
    }

    private var isStopping: Bool {
        if case .stopping = model.transcript.status { true } else { false }
    }
}
