import PresentationStyles
import SwiftUI

/// The window's content: one chat session, or why there is none yet.
/// `accessory` sits at the header's end: the controls of whatever hosts it.
public struct SessionWindow<Accessory: View>: View {
    @Bindable private var model: SessionViewModel
    private let accessory: Accessory

    public init(model: SessionViewModel, @ViewBuilder accessory: () -> Accessory) {
        self.model = model
        self.accessory = accessory()
    }

    public var body: some View {
        Group {
            switch model.connection {
            case .idle, .locating:
                EmptyState(symbol: "hourglass", title: "Starting Apogee", message: "Looking for the apogee command.")
            case .missing(let hint, let places):
                MissingBinaryView(hint: hint, places: places, retry: model.start)
            case .connected:
                ConversationView(model: model, accessory: accessory)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Palette.window)
        // Esc stops the turn in flight, through the protocol's cancel.
        .onExitCommand { model.cancel() }
    }
}

extension SessionWindow where Accessory == EmptyView {
    public init(model: SessionViewModel) {
        self.init(model: model) { EmptyView() }
    }
}

/// The header, the transcript and the input.
struct ConversationView<Accessory: View>: View {
    @Bindable var model: SessionViewModel
    let accessory: Accessory

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: Spacing.m) {
                        ForEach(model.transcript.items) { item in
                            TranscriptRow(item: item, model: model).id(item.id)
                        }
                    }
                    .padding(Spacing.xl)
                    .frame(maxWidth: Layout.conversationMax)
                    .frame(maxWidth: .infinity)
                }
                .onChange(of: model.transcript.revision) {
                    if let last = model.transcript.items.last {
                        proxy.scrollTo(last.id, anchor: .bottom)
                    }
                }
            }
            Divider()
            InputBar(model: model)
        }
    }

    private var header: some View {
        HStack(spacing: Spacing.s) {
            Text(model.transcript.model ?? "apogee")
                .font(.apogeeTitle)
                .foregroundStyle(Palette.textPrimary)
            Spacer()
            if model.transcript.turnInFlight {
                ProgressView().controlSize(.small)
            }
            Text(model.statusText)
                .font(.apogeeMeta)
                .foregroundStyle(model.transcript.status == .ended ? Palette.error : Palette.textSecondary)
            accessory
        }
        .padding(.horizontal, Spacing.l)
        .frame(height: 44)
    }
}

/// No binary where the app looks: how to install it, and where it looked.
struct MissingBinaryView: View {
    let hint: String
    let places: [String]
    let retry: () -> Void

    var body: some View {
        VStack(spacing: Spacing.l) {
            EmptyState(symbol: "terminal", title: "Apogee isn't installed", message: "The app drives the apogee command, and found none where it looks.")
            Text(hint)
                .font(.apogeeCode)
                .textSelection(.enabled)
                .padding(Spacing.m)
                .background(Palette.code, in: RoundedRectangle(cornerRadius: Radius.control))
            VStack(alignment: .leading, spacing: Spacing.xs) {
                ForEach(places, id: \.self) { place in
                    Text(place).font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                }
            }
            Button("Try again", action: retry)
                .keyboardShortcut(.defaultAction)
        }
        .padding(Spacing.xxl)
        .frame(maxWidth: Layout.conversationMax)
    }
}
