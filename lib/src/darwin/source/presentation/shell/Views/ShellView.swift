import PresentationStyles
import SwiftUI

/// The window around every screen: the sidebar, the place it shows, the
/// overlays and the toast.
///
/// The shell knows places, not screens: the composition (`PresentationApp`)
/// hands it each place's screen and the sidebar's session list, so a new
/// screen is mounted there and the shell never links it.
public struct ShellView<Screen: View, Sessions: View, OverlayContent: View>: View {
    @Bindable private var model: ShellViewModel
    private let sessions: () -> Sessions
    private let screen: (Destination) -> Screen
    private let overlay: (Overlay) -> OverlayContent

    public init(
        model: ShellViewModel, @ViewBuilder sessions: @escaping () -> Sessions,
        @ViewBuilder screen: @escaping (Destination) -> Screen,
        @ViewBuilder overlay: @escaping (Overlay) -> OverlayContent
    ) {
        self.model = model
        self.sessions = sessions
        self.screen = screen
        self.overlay = overlay
    }

    public var body: some View {
        let layout = model.layout
        HStack(spacing: 0) {
            if layout.sidebar != .hidden {
                Sidebar(model: model, compact: layout.sidebar == .icons, sessions: sessions)
                    .frame(width: layout.sidebar == .full ? Layout.sidebar : Layout.sidebarCollapsed)
                Divider()
            }
            screen(model.route)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .background(Palette.window)
        .onGeometryChange(for: Double.self) { Double($0.size.width) } action: { model.windowResized(width: $0) }
        .sheet(item: Binding(get: { model.overlay }, set: { if $0 == nil { model.closeOverlay() } })) { which in
            overlay(which)
        }
        .overlay(alignment: .bottom) {
            if let toast = model.toast {
                ToastView(toast: toast)
                    .padding(.bottom, Spacing.xl)
                    .task(id: toast.id) {
                        try? await Task.sleep(for: Toast.lifetime)
                        model.dismissToast(toast.id)
                    }
            }
        }
    }
}

/// A place whose screen is not built: its header, and what will be there.
public struct PlaceholderScreen: View {
    private let destination: Destination

    public init(_ destination: Destination) {
        self.destination = destination
    }

    public var body: some View {
        VStack(spacing: 0) {
            ScreenHeader(title: destination.title)
            Divider()
            if let placeholder = destination.placeholder {
                PlaceholderView(symbol: destination.symbol, placeholder: placeholder)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
    }
}

/// A screen's header: its title, and room for its own controls.
public struct ScreenHeader<Controls: View>: View {
    private let title: String
    private let controls: Controls

    public init(title: String, @ViewBuilder controls: () -> Controls) {
        self.title = title
        self.controls = controls()
    }

    public var body: some View {
        HStack(spacing: Spacing.s) {
            Text(title)
                .font(.apogeeTitle)
                .foregroundStyle(Palette.textPrimary)
            Spacer()
            controls
        }
        .padding(.horizontal, Spacing.l)
        .frame(height: Layout.header)
    }
}

extension ScreenHeader where Controls == EmptyView {
    public init(title: String) {
        self.init(title: title) { EmptyView() }
    }
}

/// What a placeholder says: not built, what will be here, and the command.
public struct PlaceholderView: View {
    let symbol: String
    let placeholder: Placeholder

    public init(symbol: String, placeholder: Placeholder) {
        self.symbol = symbol
        self.placeholder = placeholder
    }

    public var body: some View {
        VStack(spacing: Spacing.m) {
            EmptyState(symbol: symbol, title: placeholder.title, message: placeholder.about)
            Text("Until then, the command line has it:")
                .font(.apogeeMeta)
                .foregroundStyle(Palette.textTertiary)
            Text(placeholder.command)
                .font(.apogeeCode)
                .foregroundStyle(Palette.textSecondary)
                .textSelection(.enabled)
                .padding(.horizontal, Spacing.m)
                .padding(.vertical, Spacing.s)
                .background(Palette.code, in: RoundedRectangle(cornerRadius: Radius.control))
        }
        .padding(Spacing.xxl)
    }
}

/// The toast: one line at the bottom centre.
struct ToastView: View {
    let toast: Toast

    var body: some View {
        Text(toast.text)
            .font(.apogeeBody)
            .foregroundStyle(Palette.textPrimary)
            .padding(.horizontal, Spacing.l)
            .padding(.vertical, Spacing.s)
            .background(Palette.card, in: Capsule())
            .overlay(Capsule().strokeBorder(Palette.border))
            .shadow(color: .black.opacity(0.2), radius: 12, y: 4)
            .accessibilityAddTraits(.updatesFrequently)
    }
}

/// The sidebar's session list, until the library is built: says so.
public struct SessionListPlaceholder: View {
    public init() {}

    public var body: some View {
        VStack(alignment: .leading, spacing: Spacing.xs) {
            Text("Sessions")
                .font(.apogeeSmall.weight(.medium))
                .foregroundStyle(Palette.textSecondary)
            Text("Your earlier sessions will be listed here. Until then: apogee chats list")
                .font(.apogeeMeta)
                .foregroundStyle(Palette.textTertiary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(Spacing.m)
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

/// The Settings window, until its panes are built: says so.
public struct SettingsPlaceholder: View {
    public init() {}

    public var body: some View {
        PlaceholderView(
            symbol: "gearshape",
            placeholder: Placeholder(
                about: "General, Appearance, Backends & keys, Video & voice and Permissions will be here.",
                command: "apogee config get"))
            .frame(width: 600, height: 420)
            .background(Palette.window)
    }
}
