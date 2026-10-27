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
                                    Text(SuitesViewModel.summary(model.fit(suite)))
                                        .font(.apogeeMeta)
                                        .foregroundStyle(SuitesViewModel.overBudget(model.fit(suite)) ? Palette.error : Palette.textTertiary)
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
        let fit = model.fit(suite)
        let over = SuitesViewModel.overBudget(fit)
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
                HStack {
                    Button(over ? "Open a chat under it anyway" : "Open a chat under it") { model.openChat(under: suite) }
                        .help("apogee chat --suite \(suite.name)\(over ? " --force: run it over this machine's memory" : "")")
                    Button(over ? "Switch the session to it anyway" : "Switch the session to it") { model.switchSession(to: suite) }
                        .disabled(!model.canSwitchSession)
                        .help("Carries the open conversation on under \(suite.name), as /suite does: the session restarts with --resume and --suite\(over ? " --force" : "")")
                }
                Text(SuitesViewModel.words(fit))
                    .font(.apogeeBody)
                    .foregroundStyle(Self.color(fit))
                    .textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
                if let footprint = model.footprint(suite) {
                    priced(footprint)
                } else {
                    estimated
                }
                ForEach(suite.unread, id: \.self) { line in
                    Text(line).font(.apogeeCode).foregroundStyle(Palette.textTertiary)
                }
                Text(model.footprint(suite) == nil
                    ? "Estimated here, as no read of apogee's states this suite's fit: from each member's models info, a weights file two members share counted once and adapters by their files, against this machine's system read. apogee chat --suite reaches its own verdict and refuses one that does not fit."
                    : "apogee's own verdict, from models status --suite \(suite.name)\(model.footprint(suite)?.isTyped == true ? "'s typed footprint" : ""): the one chat --suite admits a session by, refusing a suite that does not fit unless forced.")
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .padding(Spacing.xl)
            .frame(maxWidth: Layout.mediaContentMax, alignment: .leading)
            .frame(maxWidth: .infinity)
        }
    }

    /// Each member as the CLI priced it: its roles, its pin, and what it
    /// holds in the CLI's words -- a base another member shares said so.
    private func priced(_ footprint: SuiteFootprint) -> some View {
        Grid(alignment: .leadingFirstTextBaseline, horizontalSpacing: Spacing.l, verticalSpacing: Spacing.s) {
            GridRow {
                ForEach(["ROLE", "MODEL", "WINDOW", "HOLDS"], id: \.self) {
                    Text($0).font(.system(size: TypeScale.label, weight: .medium, design: .monospaced)).foregroundStyle(Palette.textTertiary)
                }
            }
            ForEach(footprint.members) { member in
                GridRow {
                    Text(member.roles.joined(separator: ", "))
                    Text(member.backend).font(.apogeeCode)
                    Text(window(of: member.backend)).font(.apogeeMeta)
                    Text(member.said)
                        .font(.apogeeMeta)
                        .foregroundStyle(Palette.textSecondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .font(.apogeeSmall)
            }
        }
    }

    /// The members as the estimate sized them.
    private var estimated: some View {
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
    }

    /// The window the suite pins on a backend, from its `config get`.
    private func window(of backend: String) -> String {
        suite.members.first { $0.backend == backend }?.window.map { "\($0) pinned" } ?? "its own"
    }

    static func color(_ fit: SuitesViewModel.Fit) -> Color {
        switch fit {
        case .apogee(let footprint):
            switch footprint.verdict {
            case .fits: Palette.accent
            case .doesNotFit: Palette.error
            default: Palette.textSecondary
            }
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

#if DEBUG
#Preview("Two suites, coding the default and open") {
    let suites = SuitesPreview.model()
    SuitesScreen(model: suites)
        .frame(width: 1100, height: 560)
        .task { await suites.connect(binary: previewBinary) }
}

#Preview("No suites yet") {
    let suites = SuitesPreview.model(over: SuitesPreview.empty)
    SuitesScreen(model: suites)
        .frame(width: 1100, height: 480)
        .task { await suites.connect(binary: previewBinary) }
}

#Preview("Apogee isn't installed") {
    let suites = SuitesPreview.model()
    SuitesScreen(model: suites)
        .frame(width: 1100, height: 480)
        .task { await suites.connect(binary: nil) }
}

#Preview("The default suite, too big for this Mac") {
    let suites = SuitesPreview.model()
    SuiteDetail(model: suites, suite: SuitesPreview.suite("coding"))
        .frame(width: 820, height: 420)
        .task { await suites.connect(binary: previewBinary) }
}

#Preview("A suite whose fit is unknown: a member states no size") {
    let suites = SuitesPreview.model()
    SuiteDetail(model: suites, suite: SuitesPreview.suite("research"))
        .frame(width: 820, height: 420)
        .task { await suites.connect(binary: previewBinary) }
}

#Preview("Recorded: a shared base admitted, as apogee says") {
    let suites = SuitesPreview.recordedModel(sessionOpen: true)
    SuitesScreen(model: suites)
        .frame(width: 1100, height: 560)
        .task {
            await suites.connect(binary: previewBinary)
            suites.select("shared")
        }
}

#Preview("A shared-base suite fitting, its members priced by apogee") {
    let suites = SuitesPreview.recordedModel(sessionOpen: true)
    SuiteDetail(model: suites, suite: SuitesPreview.recordedSuite("shared"))
        .frame(width: 820, height: 460)
        .task { await suites.connect(binary: previewBinary) }
}

#Preview("The fit from the typed footprint: a shared base, its exact bytes") {
    let suites = SuitesPreview.typedModel(sessionOpen: true)
    SuiteDetail(model: suites, suite: SuitesPreview.recordedSuite("shared"))
        .frame(width: 820, height: 460)
        .task { await suites.connect(binary: previewBinary) }
}

#Preview("The typed footprint: a member's size unknown, fits as far as known") {
    let suites = SuitesPreview.typedModel()
    SuiteDetail(model: suites, suite: SuitesPreview.recordedSuite("partial"))
        .frame(width: 820, height: 420)
        .task { await suites.connect(binary: previewBinary) }
}

#Preview("The typed footprint: every suite, vast over budget") {
    let suites = SuitesPreview.typedModel(sessionOpen: true)
    SuitesScreen(model: suites)
        .frame(width: 1100, height: 560)
        .task {
            await suites.connect(binary: previewBinary)
            suites.select("vast")
        }
}

#Preview("A suite apogee says does not fit: open it anyway") {
    let suites = SuitesPreview.recordedModel()
    SuiteDetail(model: suites, suite: SuitesPreview.recordedSuite("vast"))
        .frame(width: 820, height: 420)
        .task { await suites.connect(binary: previewBinary) }
}

#Preview("A suite whose members hold nothing here, no session open") {
    let suites = SuitesPreview.recordedModel()
    SuiteDetail(model: suites, suite: SuitesPreview.recordedSuite("fast"))
        .frame(width: 820, height: 380)
        .task { await suites.connect(binary: previewBinary) }
}

#Preview("A suite that fits, one backend in two roles") {
    let suites = SuitesPreview.model(over: SuitesPreview.one)
    SuiteDetail(model: suites, suite: SuitesPreview.suite("duo"))
        .frame(width: 820, height: 420)
        .task { await suites.connect(binary: previewBinary) }
}
#endif
