import BusinessReads
import PresentationShell
import PresentationStyles
import SwiftUI

/// The Suites place: the suites, a suite's members, and whether it fits.
public struct SuitesScreen: View {
    @Bindable private var model: SuitesViewModel

    public init(model: SuitesViewModel) {
        self.model = model
    }

    public var body: some View {
        VStack(spacing: 0) {
            ScreenHeader(title: "Suites") {
                Text("A main model plus small helpers, loaded together")
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textTertiary)
                if model.reading { ProgressView().controlSize(.small) }
            }
            Divider()
            HStack(spacing: 0) {
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 1) {
                        ForEach(model.suites) { suite in
                            let current = model.selected == suite.name
                            Button {
                                model.select(suite.name)
                            } label: {
                                VStack(alignment: .leading, spacing: 2) {
                                    HStack {
                                        Text(suite.name).font(.apogeeBody).foregroundStyle(Palette.textPrimary)
                                        if model.defaultSuite == suite.name {
                                            Text("default").font(.apogeeMeta).foregroundStyle(Palette.accent)
                                        }
                                    }
                                    Text(SuitesViewModel.words(model.fit(suite)))
                                        .font(.apogeeMeta)
                                        .foregroundStyle(Palette.textTertiary)
                                        .lineLimit(2)
                                }
                                .padding(Spacing.m)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .background(current ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                                .contentShape(Rectangle())
                            }
                            .buttonStyle(.plain)
                        }
                        if model.suites.isEmpty {
                            Text(model.failure ?? "No suites yet. apogee config add-suite <name> --chat <backend> … makes one.")
                                .font(.apogeeSmall)
                                .foregroundStyle(model.failure == nil ? Palette.textTertiary : Palette.error)
                                .padding(Spacing.m)
                        }
                    }
                    .padding(Spacing.s)
                }
                .frame(width: 280)
                .background(Palette.sidebar)
                Divider()
                if let suite = model.current {
                    SuiteDetail(model: model, suite: suite)
                } else {
                    EmptyState(symbol: "square.stack.3d.up", title: "Suites", message: "A suite names one backend per role -- chat, embedding, vision, utility and the rest -- run together.")
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
        }
    }
}

struct SuiteDetail: View {
    let model: SuitesViewModel
    let suite: Suite

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.l) {
                HStack {
                    Text(suite.name).font(.apogeeHeading)
                    Spacer()
                    if model.defaultSuite == suite.name {
                        Button("Stop using it by default") { Task { await model.makeDefault(nil) } }
                    } else {
                        Button("Use for new sessions") { Task { await model.makeDefault(suite) } }
                    }
                }
                Text(SuitesViewModel.words(model.fit(suite)))
                    .font(.apogeeBody)
                    .foregroundStyle(Self.color(model.fit(suite)))
                Grid(alignment: .leadingFirstTextBaseline, horizontalSpacing: Spacing.l, verticalSpacing: Spacing.s) {
                    GridRow {
                        ForEach(["ROLE", "MODEL", "WEIGHTS", "CACHE", "WINDOW"], id: \.self) {
                            Text($0).font(.system(size: TypeScale.label, weight: .medium, design: .monospaced)).foregroundStyle(Palette.textTertiary)
                        }
                    }
                    ForEach(model.members(suite)) { fit in
                        GridRow {
                            Text(fit.member.role)
                            Text(fit.member.backend).font(.apogeeCode)
                            Text(Self.weights(fit.memory)).font(.apogeeMeta.monospacedDigit())
                            Text(Self.cache(fit.memory)).font(.apogeeMeta.monospacedDigit())
                            Text(fit.member.window.map { "\($0) pinned" } ?? "its own").font(.apogeeMeta)
                        }
                        .font(.apogeeSmall)
                    }
                }
                ForEach(suite.unread, id: \.self) { line in
                    Text(line).font(.apogeeCode).foregroundStyle(Palette.textTertiary)
                }
                Text("Worked out here from each member's models info and this machine's system read; apogee chat --suite reaches its own verdict and refuses one that does not fit.")
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .padding(Spacing.xl)
            .frame(maxWidth: Layout.mediaContentMax, alignment: .leading)
            .frame(maxWidth: .infinity)
        }
    }

    static func color(_ fit: SuitesViewModel.Fit) -> Color {
        switch fit {
        case .fits: Palette.accent
        case .doesNotFit: Palette.error
        default: Palette.textSecondary
        }
    }

    static func weights(_ memory: SuitesViewModel.Memory) -> String {
        switch memory {
        case .none: "none here"
        case .known(let weights, _): ByteCountFormatter.string(fromByteCount: Int64(weights), countStyle: .memory)
        case .unknown: "unknown"
        }
    }

    static func cache(_ memory: SuitesViewModel.Memory) -> String {
        switch memory {
        case .none: "none here"
        case .known(_, let cache): ByteCountFormatter.string(fromByteCount: Int64(cache), countStyle: .memory)
        case .unknown: "unknown"
        }
    }
}
