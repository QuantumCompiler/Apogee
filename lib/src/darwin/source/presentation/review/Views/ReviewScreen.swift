import BusinessJobs
import BusinessReads
import PresentationShell
import PresentationStyles
import SwiftUI

/// The Review place (⇧⌘R): a merge request reviewed, dry run first; the
/// sweep's targets and a sweep; the review skills.
public struct ReviewScreen: View {
    @Bindable private var model: ReviewViewModel

    public init(model: ReviewViewModel) {
        self.model = model
    }

    public var body: some View {
        VStack(spacing: 0) {
            ScreenHeader(title: "Review") {
                Picker("Show", selection: $model.tab) {
                    ForEach(ReviewViewModel.Tab.allCases) { Text($0.title).tag($0) }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .fixedSize()
                if model.tab == .targets {
                    Button("Add target…") { model.newTarget() }
                    Button("Sweep…") { model.newSweep() }.disabled(model.targets.isEmpty || model.isRunning)
                }
            }
            Divider()
            if let running = model.running {
                RunningLine(doing: running, busy: model.progress?.busy) { Task { await model.stop() } }
                Divider()
            }
            switch model.tab {
            case .runs: RunsView(model: model)
            case .targets: TargetsView(model: model)
            case .skills: SkillsView(model: model)
            }
        }
        .sheet(isPresented: Binding(get: { model.sheet == .addTarget }, set: { if !$0 { model.sheet = nil } })) {
            TargetSheet(model: model)
        }
        .sheet(isPresented: Binding(get: { model.sheet == .sweep }, set: { if !$0 { model.sheet = nil } })) {
            SweepSheet(model: model)
        }
        .confirmationDialog(
            pendingTitle, isPresented: Binding(get: { model.pending != nil }, set: { if !$0 { model.dismissPending() } }),
            titleVisibility: .visible
        ) {
            switch model.pending {
            case .post: Button("Post") { Task { await model.confirm() } }
            case .sweep: Button("Sweep and post") { Task { await model.confirm() } }
            case .deleteTarget: Button("Remove", role: .destructive) { Task { await model.confirm() } }
            case nil: EmptyView()
            }
            Button("Cancel", role: .cancel) { model.dismissPending() }
        } message: {
            Text(pendingMessage)
        }
        .alert("That didn't work", isPresented: Binding(get: { model.failure != nil }, set: { if !$0 { model.dismissFailure() } })) {
            Button("OK") { model.dismissFailure() }
        } message: {
            Text(model.failure ?? "")
        }
    }

    private var pendingTitle: String {
        switch model.pending {
        case .post(let run): "Post the review to \(run.mergeRequest)?"
        case .sweep(let allowPost): allowPost ? "Sweep, and post on every target it reaches?" : "Sweep, and post where a target grants it?"
        case .deleteTarget(let name): "Remove the target \(name)?"
        case nil: ""
        }
    }

    private var pendingMessage: String {
        switch model.pending {
        case .post(let run):
            return ReviewViewModel.postWords(run)
        case .sweep(let allowPost):
            let posting = model.postingTargets(allowPost: allowPost)
            return posting.isEmpty
                ? "No target this sweep reaches grants posting, so it posts nothing: its documents are kept."
                : "It posts on \(posting.joined(separator: ", ")) -- \(allowPost ? "--allow-post grants every target it sweeps" : "each grants it with post: true") -- and keeps a document for the rest."
        case .deleteTarget(let name):
            return "apogee review targets delete \(name)"
        case nil:
            return ""
        }
    }
}

/// What runs now, in the CLI's own narration, with Stop.
struct RunningLine: View {
    let doing: String
    let busy: String?
    let stop: () -> Void

    var body: some View {
        HStack(spacing: Spacing.m) {
            ProgressView().controlSize(.small)
            Text(doing).font(.apogeeSmall.weight(.medium))
            Text(busy ?? "").font(.apogeeCode).foregroundStyle(Palette.textSecondary).lineLimit(1)
            Spacer()
            Button("Stop", action: stop)
        }
        .padding(.horizontal, Spacing.l)
        .frame(height: 40)
    }
}

/// A merge request reviewed: the form, then the document.
struct RunsView: View {
    @Bindable var model: ReviewViewModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.l) {
                form
                if let run = model.run {
                    RunPage(run: run, post: model.askToPost, canPost: run.dryRun && !run.findings.isEmpty && !model.isRunning)
                } else {
                    Text("A review runs the lenses over a merge request's change. It posts nothing until you have seen what it found and chosen to post it.")
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textSecondary)
                }
            }
            .padding(Spacing.xl)
            .frame(maxWidth: Layout.conversationMax, alignment: .leading)
            .frame(maxWidth: .infinity)
        }
    }

    private var form: some View {
        VStack(alignment: .leading, spacing: Spacing.s) {
            HStack(spacing: Spacing.s) {
                TextField("A merge request: a URL, owner/repo#12 or group/project!4", text: $model.form.ref)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit { model.review() }
                Button("Review") { model.review() }
                    .keyboardShortcut(.return, modifiers: .command)
                    .disabled(model.form.arguments(posting: false) == nil || model.isRunning)
            }
            HStack(spacing: Spacing.m) {
                Menu(model.form.skills.isEmpty ? "Lenses: as the change's files say" : "Lenses: \(model.form.skills.sorted().joined(separator: ", "))") {
                    ForEach(model.skills) { skill in
                        Toggle(skill.name, isOn: Binding(
                            get: { model.form.skills.contains(skill.name) },
                            set: { if $0 { model.form.skills.insert(skill.name) } else { model.form.skills.remove(skill.name) } }))
                    }
                }
                .fixedSize()
                TextField("Model", text: $model.form.model, prompt: Text("the chat role"))
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 180)
            }
            .font(.apogeeSmall)
            if let arguments = model.form.arguments(posting: false) {
                Text((["apogee"] + arguments).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
            }
        }
    }
}

/// One review's document: what was read, what was found, what was posted.
struct RunPage: View {
    let run: ReviewRun
    let post: () -> Void
    let canPost: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            VStack(alignment: .leading, spacing: Spacing.xs) {
                Text(run.title).font(.apogeeHeading)
                Text("\(run.mergeRequest) · head \(String(run.head.prefix(8))) · \(run.mode)\(run.since.map { " since \(String($0.prefix(8)))" } ?? "")")
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textSecondary)
                Text("lenses: \(run.skills.isEmpty ? "none apply" : run.skills.joined(separator: ", ")) (\(run.plan)) · \(run.model.isEmpty ? "the chat role" : run.model)")
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
            }
            outcome
            ForEach(run.findings) { finding in
                FindingCard(finding: finding)
            }
            if !run.counts.isEmpty {
                Text(run.counts.map { "\($0.0.replacingOccurrences(of: "_", with: " ")) \($0.1)" }.joined(separator: " · "))
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
            }
            ForEach(run.notes, id: \.self) { note in
                Text(note).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
            }
            if !run.excludedFiles.isEmpty {
                Text("left out: \(run.excludedFiles.joined(separator: ", "))").font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
            }
        }
    }

    @ViewBuilder private var outcome: some View {
        switch run.posted {
        case .posted(let comments, let summary, _):
            Label("Posted \(comments) \(comments == 1 ? "comment" : "comments")\(summary ? " and a summary" : "") to \(run.mergeRequest)", systemImage: "checkmark.circle")
                .font(.apogeeBody)
                .foregroundStyle(Palette.accent)
        case .notPosted(let reason):
            if run.isSilent {
                // Silence is a complete review, never an empty-looking failure.
                Label("Nothing to say: the lenses found nothing in this change. That is a complete review.", systemImage: "checkmark.seal")
                    .font(.apogeeBody)
                    .foregroundStyle(Palette.accent)
            } else {
                HStack(spacing: Spacing.m) {
                    Text(reason).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                    Spacer()
                    if canPost {
                        Button("Post…", action: post)
                    }
                }
            }
        }
    }
}

/// A finding as the contract says it: severity, where, the claim, the fix.
struct FindingCard: View {
    let finding: ReviewFinding

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.xs) {
            HStack(spacing: Spacing.s) {
                Text(finding.severity).font(.apogeeMeta.weight(.semibold)).foregroundStyle(color)
                Text(finding.kind).font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
                Text("\(finding.file)\(finding.line.map { ":\($0)" } ?? "")").font(.apogeeCode).foregroundStyle(Palette.textPrimary)
                Spacer()
                Text("\(finding.confidence) confidence · \(finding.foundBy.joined(separator: ", "))\(finding.inline ? "" : " · summary only")")
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
            }
            Text(finding.claim).font(.apogeeBody).textSelection(.enabled)
            if !finding.suggestedFix.isEmpty {
                Text(finding.suggestedFix).font(.apogeeSmall).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
            }
        }
        .padding(Spacing.m)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.card))
        .overlay(RoundedRectangle(cornerRadius: Radius.card).strokeBorder(Palette.border))
    }

    private var color: Color {
        switch finding.severity {
        case "high", "critical": Palette.error
        case "medium": Palette.warning
        default: Palette.textSecondary
        }
    }
}

/// The targets, and the last sweep's document.
struct TargetsView: View {
    @Bindable var model: ReviewViewModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.l) {
                if model.targets.isEmpty {
                    Text("No sweep targets yet. A target is a repository or an owner; a sweep reviews what is new on each since its watermark.")
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textSecondary)
                }
                ForEach(model.targets) { target in
                    HStack(alignment: .firstTextBaseline, spacing: Spacing.m) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(target.name).font(.apogeeBody.weight(.medium))
                            Text(Self.facts(target)).font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                        }
                        Spacer()
                        Text(target.post ? "posts when swept" : "documents only").font(.apogeeMeta).foregroundStyle(target.post ? Palette.warning : Palette.textSecondary)
                        Button("Remove…") { model.askToDelete(target: target.name) }
                    }
                    .padding(Spacing.m)
                    .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.card))
                }
                if let sweep = model.sweep {
                    SweepPage(sweep: sweep)
                }
            }
            .padding(Spacing.xl)
            .frame(maxWidth: Layout.conversationMax, alignment: .leading)
            .frame(maxWidth: .infinity)
        }
    }

    static func facts(_ target: ReviewTarget) -> String {
        var parts = [target.forge, target.repo.map { "repository \($0)" } ?? target.owner.map { "owner \($0)" } ?? ""]
        if let host = target.host { parts.append(host) }
        parts.append(target.skills.isEmpty ? "lenses by the change" : "lenses \(target.skills.joined(separator: ", "))")
        if target.drafts { parts.append("drafts too") }
        if let model = target.model { parts.append(model) }
        return parts.filter { !$0.isEmpty }.joined(separator: " · ")
    }
}

/// A sweep's document: per target, its grant and projects, each merge
/// request it reviewed, and where the document was kept.
struct SweepPage: View {
    let sweep: ReviewSweep

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.s) {
            Text("The last sweep").font(.apogeeHeading)
            Text("\(sweep.dryRun ? "dry run · " : "")\(sweep.reviewed) reviewed · \(sweep.posted) posted · \(sweep.deferred) deferred · \(sweep.errors) \(sweep.errors == 1 ? "error" : "errors") · started \(sweep.started)")
                .font(.apogeeMeta)
                .foregroundStyle(Palette.textSecondary)
            ForEach(sweep.targets) { target in
                VStack(alignment: .leading, spacing: Spacing.xs) {
                    Text("\(target.name) · \(target.kind) · grant \(target.grant)").font(.apogeeSmall.weight(.medium))
                    if let error = target.error { Text(error).font(.apogeeSmall).foregroundStyle(Palette.error) }
                    ForEach(target.projects) { project in
                        VStack(alignment: .leading, spacing: 2) {
                            Text(project.project).font(.apogeeCode)
                            Text(Self.watermark(project)).font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                            if let error = project.error { Text(error).font(.apogeeSmall).foregroundStyle(Palette.error) }
                            ForEach(Array(project.reviewed.enumerated()), id: \.offset) { _, run in
                                Text(Self.line(run)).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                            }
                        }
                    }
                }
                .padding(Spacing.m)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.card))
            }
            if !sweep.document.isEmpty {
                Text("kept at \(sweep.document)").font(.apogeeMeta).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
            }
        }
    }

    static func watermark(_ project: ReviewSweep.Project) -> String {
        if project.firstSweep { return "first sweep: started at now -- history stays unreviewed" }
        return "watermark \(project.watermarkBefore ?? "none") → \(project.watermarkAfter ?? "unchanged")"
    }

    static func line(_ run: ReviewRun) -> String {
        let found = run.isSilent ? "nothing to say" : "\(run.findings.count) \(run.findings.count == 1 ? "finding" : "findings")"
        switch run.posted {
        case .posted(let comments, _, _): return "\(run.mergeRequest) -- \(run.title): \(found), \(comments) posted"
        case .notPosted(let reason): return "\(run.mergeRequest) -- \(run.title): \(found); \(reason)"
        }
    }
}

/// The review skills: each lens, where it comes from, what it applies to,
/// its prompt read-only.
struct SkillsView: View {
    @Bindable var model: ReviewViewModel

    var body: some View {
        HStack(spacing: 0) {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 1) {
                    ForEach(model.skills) { skill in
                        Button { Task { await model.show(skill: skill.name) } } label: {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(skill.name).font(.apogeeBody)
                                Text("\(skill.source) · \(skill.everywhere ? "every file" : Self.applies(skill))").font(.apogeeMeta).foregroundStyle(Palette.textTertiary).lineLimit(1)
                            }
                            .padding(Spacing.m)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(model.selectedSkill == skill.name ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                    }
                    ForEach(model.skillProblems, id: \.self) { problem in
                        Text(problem).font(.apogeeSmall).foregroundStyle(Palette.warning).padding(Spacing.m)
                    }
                }
                .padding(Spacing.s)
            }
            .frame(width: 300)
            .background(Palette.sidebar)
            Divider()
            ScrollView {
                if let skill = model.shownSkill {
                    VStack(alignment: .leading, spacing: Spacing.m) {
                        Text(skill.name).font(.apogeeHeading)
                        Text(skill.description).font(.apogeeBody).foregroundStyle(Palette.textSecondary)
                        Text(skill.everywhere ? "Applies to every file." : "Applies to: \(Self.applies(skill))").font(.apogeeSmall)
                        if let path = skill.path { Text(path).font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled) }
                        Text(skill.prompt ?? "").font(.apogeeCode).textSelection(.enabled)
                            .padding(Spacing.m)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(Palette.code, in: RoundedRectangle(cornerRadius: Radius.control))
                        Text("apogee review skills show \(skill.name)").font(.apogeeCode).foregroundStyle(Palette.textTertiary)
                    }
                    .padding(Spacing.xl)
                } else {
                    Text("Every lens a review can run, shipped and your own. Choose one to read its prompt.")
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textSecondary)
                        .padding(Spacing.xl)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        }
    }

    static func applies(_ skill: ReviewSkill) -> String {
        (skill.extensions + skill.languages + skill.paths).joined(separator: ", ")
    }
}

/// `review targets add`, as typed.
struct TargetSheet: View {
    @Bindable var model: ReviewViewModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("Add a sweep target").font(.apogeeHeading)
            Form {
                TextField("Name", text: $model.targetForm.name)
                Picker("Watch", selection: $model.targetForm.isOwner) {
                    Text("A repository").tag(false)
                    Text("An owner's repositories").tag(true)
                }
                TextField(model.targetForm.isOwner ? "Owner" : "Repository", text: $model.targetForm.place, prompt: Text(model.targetForm.isOwner ? "acme" : "owner/repo or group/project"))
                Picker("Forge", selection: $model.targetForm.forge) {
                    Text("GitHub").tag("github")
                    Text("GitLab").tag("gitlab")
                }
                TextField("Host", text: $model.targetForm.host, prompt: Text("the forge's own"))
                Toggle("Post when swept -- with nobody at the terminal", isOn: $model.targetForm.post)
                Toggle("Review drafts too", isOn: $model.targetForm.drafts)
                TextField("Model", text: $model.targetForm.model, prompt: Text("the chat role"))
            }
            if let arguments = model.targetForm.arguments {
                Text((["apogee"] + arguments).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
            }
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Add") { Task { await model.addTarget() } }
                    .keyboardShortcut(.defaultAction)
                    .disabled(model.targetForm.arguments == nil)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 520)
    }
}

/// `review sweep`, as typed: a dry run, or -- asked first -- one that posts.
struct SweepSheet: View {
    @Bindable var model: ReviewViewModel
    @State private var allowPost = false
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("Sweep the targets").font(.apogeeHeading)
            Text("Reviews what is new on each target since its watermark, once.").font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
            Form {
                ForEach(model.targets) { target in
                    Toggle(target.name, isOn: Binding(
                        get: { model.sweepForm.targets.contains(target.name) },
                        set: { if $0 { model.sweepForm.targets.insert(target.name) } else { model.sweepForm.targets.remove(target.name) } }))
                }
                TextField("A first sweep starts from", text: $model.sweepForm.since, prompt: Text("now -- e.g. 2026-10-01"))
                TextField("Model", text: $model.sweepForm.model, prompt: Text("each target's, else the chat role"))
                Toggle("When posting, grant every target it sweeps (--allow-post)", isOn: $allowPost)
            }
            Text((["apogee"] + model.sweepForm.arguments(posting: false, allowPost: false)).joined(separator: " "))
                .font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Sweep and post…") { model.askToSweep(allowPost: allowPost) }
                Button("Sweep (dry run)") { model.sweepDry() }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 560)
    }
}
