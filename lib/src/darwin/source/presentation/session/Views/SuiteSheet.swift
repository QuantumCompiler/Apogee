import PresentationStyles
import SwiftUI

/// The suite a session runs under, chosen: each configured suite with the
/// CLI's own word on its fit here, or none. Machine mode carries no suite, so
/// moving an open conversation restarts the session under it -- `--resume`
/// and `--suite`, said in the transcript -- as the TUI's Suites does; one the
/// CLI prices over this machine's memory runs only when the person forces it.
struct SuiteSheet: View {
    @Bindable var model: SessionViewModel
    @State private var chosen: String
    @State private var force = false
    @Environment(\.dismiss) private var dismiss

    /// The chooser over `model`, on the suite the session runs under -- or on
    /// `choosing`, `forcing` it, where the caller names one.
    init(model: SessionViewModel, choosing: String? = nil, forcing: Bool = false) {
        self.model = model
        _chosen = State(initialValue: choosing ?? model.runningSuite ?? "")
        _force = State(initialValue: forcing)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.l) {
            Text("Run this session under").font(.apogeeHeading)
            Text("Now: " + (model.suiteWords ?? "the suite it last had, as apogee keeps it"))
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textSecondary)
            if model.suiteChoices.isEmpty {
                Text("No suites are configured. apogee config add-suite <name> --chat <backend> … makes one.")
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textTertiary)
            }
            VStack(alignment: .leading, spacing: Spacing.xs) {
                ForEach(model.suiteChoices) { choice in
                    row(choice.name, title: choice.name + (choice.isDefault ? " (the default)" : ""), said: choice.verdict, over: choice.overBudget)
                }
                if !model.isExecute {
                    row(SessionViewModel.noSuite, title: "No suite", said: "the roles follow the global pointers", over: false)
                }
            }
            if overBudget {
                Toggle("Run it anyway (--force): apogee prices it over this machine's memory", isOn: $force)
                if let total = model.suiteChoices.first(where: { $0.name == chosen })?.total {
                    Text(total).font(.apogeeMeta).foregroundStyle(Palette.error).fixedSize(horizontal: false, vertical: true)
                }
            }
            Text(footer)
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textTertiary)
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Switch") {
                    let suite = chosen
                    let forced = force && overBudget
                    Task { await model.switchSuite(to: suite, force: forced) }
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(chosen.isEmpty || (chosen == model.runningSuite && !force) || (overBudget && !force))
            }
        }
        .padding(Spacing.xl)
        .frame(width: 560)
    }

    /// The chosen suite is one the CLI refuses unless forced.
    private var overBudget: Bool {
        model.suiteChoices.first { $0.name == chosen }?.overBudget ?? false
    }

    private var footer: String {
        "Machine mode carries no suite: the session restarts under it -- the conversation carried on with --resume, on the suite's chat member -- and a local model loads again."
    }

    /// One choice: its name, and the CLI's word on its fit.
    private func row(_ value: String, title: String, said: String?, over: Bool) -> some View {
        Button {
            chosen = value
            force = false
        } label: {
            HStack(alignment: .firstTextBaseline) {
                Image(systemName: chosen == value ? "largecircle.fill.circle" : "circle")
                    .foregroundStyle(chosen == value ? Palette.accent : Palette.textTertiary)
                Text(title).font(.apogeeBody).foregroundStyle(Palette.textPrimary)
                if value == model.runningSuite {
                    Text("now").font(.apogeeMeta).foregroundStyle(Palette.accent)
                }
                Spacer()
                if let said {
                    Text(said).font(.apogeeMeta).foregroundStyle(over ? Palette.error : Palette.textTertiary).lineLimit(1)
                }
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

#if DEBUG
#Preview("The suites, apogee's verdict on each") {
    let session = SessionPreview.underSuite()
    SuiteSheet(model: session)
        .task { await SessionPreview.choosingSuite(session) }
}

#Preview("A suite that does not fit, forced") {
    let session = SessionPreview.underSuite()
    SuiteSheet(model: session, choosing: "vast", forcing: true)
        .task { await SessionPreview.choosingSuite(session) }
}

#Preview("An execute session: named suites only") {
    let session = SessionPreview.session(SessionPreview.executePlay, launch: SessionPreview.execute())
    SuiteSheet(model: session)
        .task { await SessionPreview.choosingSuite(session) }
}
#endif
