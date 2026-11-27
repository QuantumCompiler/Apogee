import PresentationStyles
import SwiftUI

/// The sidebar: the session list on top, the places, the system strip and
/// Settings below -- or, in a narrow window, a column of icons.
struct Sidebar<Sessions: View>: View {
    @Bindable var model: ShellViewModel
    let compact: Bool
    let sessions: () -> Sessions
    @Environment(\.openSettings) private var openSettings

    var body: some View {
        VStack(spacing: 0) {
            if compact {
                VStack(spacing: Spacing.xxs) {
                    ForEach(Destination.allCases) { PlaceButton(destination: $0, model: model) }
                }
                .padding(.vertical, Spacing.s)
                Spacer(minLength: 0)
            } else {
                sessions()
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
                Divider()
                ScrollView(.horizontal) {
                    HStack(spacing: Spacing.xxs) {
                        ForEach(Destination.allCases) { PlaceButton(destination: $0, model: model, fill: true) }
                    }
                    .padding(.horizontal, Spacing.s)
                    .padding(.vertical, Spacing.xs)
                }
                .scrollIndicators(.hidden)
            }
            Divider()
            footer
        }
        .background(Palette.sidebar)
    }

    private var footer: some View {
        HStack(spacing: Spacing.xs) {
            Button(action: { openSettings() }) {
                Label("Settings", systemImage: "gearshape")
                    .labelStyle(compact ? AnyLabelStyle(.iconOnly) : AnyLabelStyle(.titleAndIcon))
                    .frame(maxWidth: compact ? nil : .infinity, alignment: .leading)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .foregroundStyle(Palette.textSecondary)
            .help("Settings ⌘,")
            if !compact {
                Spacer(minLength: 0)
                Button(action: { model.perform(.toggleMonitor) }) {
                    Image(systemName: "waveform.path.ecg")
                }
                .buttonStyle(.plain)
                .foregroundStyle(Palette.textTertiary)
                .help("System monitor \(KeyMap.chord(for: .toggleMonitor)?.symbols ?? "")")
                .accessibilityLabel("System monitor")
                Button(action: { model.perform(.toggleConsole) }) {
                    Image(systemName: "terminal")
                }
                .buttonStyle(.plain)
                .foregroundStyle(Palette.textTertiary)
                .help("Console \(KeyMap.chord(for: .toggleConsole)?.symbols ?? "")")
                .accessibilityLabel("Console")
            }
        }
        .font(.apogeeBody)
        .padding(.horizontal, Spacing.m)
        .frame(height: 36)
    }
}

/// One place: its icon, its name and key on hover, the current one marked.
struct PlaceButton: View {
    let destination: Destination
    let model: ShellViewModel
    /// Shares the row's width with its siblings, rather than the design's 40 points.
    var fill = false

    var body: some View {
        let current = model.route == destination
        Button(action: { model.go(destination) }) {
            Image(systemName: destination.symbol)
                .font(.system(size: 15))
                .frame(width: fill ? nil : Layout.placeWidth, height: Layout.placeHeight)
                .padding(.horizontal, fill ? Layout.placePadding : 0)
                .frame(maxWidth: fill ? .infinity : nil)
                .background(current ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.control))
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .foregroundStyle(current ? Palette.textPrimary : Palette.textSecondary)
        .help("\(destination.title) \(destination.key.symbols)")
        .accessibilityLabel(destination.title)
        .accessibilityAddTraits(current ? .isSelected : [])
    }
}

/// A label style chosen at run time.
struct AnyLabelStyle: LabelStyle {
    private let make: (Configuration) -> AnyView

    init<Style: LabelStyle>(_ style: Style) {
        make = { AnyView(style.makeBody(configuration: $0)) }
    }

    func makeBody(configuration: Configuration) -> some View {
        make(configuration)
    }
}

#if DEBUG
#Preview("The sidebar") {
    Sidebar(model: ShellPreview.shell(), compact: false) {
        Text("The saved sessions").font(.apogeeSmall).padding(Spacing.m)
    }
    .frame(width: 256, height: 640)
}

#Preview("A place, current and not") {
    let shell = ShellPreview.shell(at: .models)
    HStack {
        PlaceButton(destination: .models, model: shell)
        PlaceButton(destination: .library, model: shell)
    }
    .padding(Spacing.l)
}
#endif
