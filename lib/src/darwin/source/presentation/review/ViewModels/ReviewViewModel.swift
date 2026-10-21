import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Observation

/// The Review place: a merge request reviewed -- a dry run first, posting
/// only on the person's click on the batch it found -- the sweep's targets and
/// a sweep supervised, and the review skills.
///
/// SwiftUI-free (ADR 0002). Every run is the person's command, supervised on
/// the jobs core with the CLI's narration as its busy line, and its document
/// read from its stdout. `--yes` and `--allow-post` are passed only from a
/// consent the person gave on a shown batch; a clean review's silence is a
/// complete result.
@MainActor
@Observable
public final class ReviewViewModel {
    public enum Tab: String, CaseIterable, Identifiable, Sendable {
        case runs
        case targets
        case skills
        public var id: String { rawValue }
        public var title: String {
            switch self {
            case .runs: "Reviews"
            case .targets: "Targets and sweeps"
            case .skills: "Skills"
            }
        }
    }

    /// `review mr <ref>`, as a person types it.
    public struct RunForm: Equatable, Sendable {
        public var ref = ""
        /// `--skills a,b`; empty lets the plan decide by the change's files.
        public var skills: Set<String> = []
        /// `-m`; empty is the chat role.
        public var model = ""

        public init() {}

        /// The run's argv: `--dry-run`, or `--yes` on a consented post.
        public func arguments(posting: Bool) -> [String]? {
            let ref = ref.trimmingCharacters(in: .whitespaces)
            guard !ref.isEmpty else { return nil }
            var arguments = ["review", "mr", ref]
            if !skills.isEmpty { arguments += ["--skills", skills.sorted().joined(separator: ",")] }
            let model = model.trimmingCharacters(in: .whitespaces)
            if !model.isEmpty { arguments += ["-m", model] }
            arguments.append(posting ? "--yes" : "--dry-run")
            return arguments + ["--output-format", "json"]
        }
    }

    /// `review targets add <name> --repo|--owner …`.
    public struct TargetForm: Equatable, Sendable {
        public var name = ""
        /// `true` for `--owner`, `false` for `--repo`.
        public var isOwner = false
        public var place = ""
        /// `github` (the CLI's default) or `gitlab`.
        public var forge = "github"
        public var host = ""
        public var skills: Set<String> = []
        /// `--post`: the target's own grant, asked for by name.
        public var post = false
        public var drafts = false
        public var model = ""

        public init() {}

        public var arguments: [String]? {
            let name = name.trimmingCharacters(in: .whitespaces)
            let place = place.trimmingCharacters(in: .whitespaces)
            guard !name.isEmpty, !place.isEmpty else { return nil }
            var arguments = ["review", "targets", "add", name, isOwner ? "--owner" : "--repo", place]
            if forge != "github" { arguments += ["--forge", forge] }
            let host = host.trimmingCharacters(in: .whitespaces)
            if !host.isEmpty { arguments += ["--host", host] }
            if !skills.isEmpty { arguments += ["--skills", skills.sorted().joined(separator: ",")] }
            if post { arguments.append("--post") }
            if drafts { arguments.append("--drafts") }
            let model = model.trimmingCharacters(in: .whitespaces)
            if !model.isEmpty { arguments += ["-m", model] }
            return arguments
        }
    }

    /// `review sweep`, as a person types it.
    public struct SweepForm: Equatable, Sendable {
        /// `--target`, each; empty sweeps every target.
        public var targets: Set<String> = []
        /// `--since <date>` for a target seen the first time; empty is now.
        public var since = ""
        public var model = ""

        public init() {}

        /// `--dry-run`, or -- consented -- a sweep that posts where granted,
        /// and with `allowPost` on every target it sweeps.
        public func arguments(posting: Bool, allowPost: Bool) -> [String] {
            var arguments = ["review", "sweep"]
            for target in targets.sorted() { arguments += ["--target", target] }
            let since = since.trimmingCharacters(in: .whitespaces)
            if !since.isEmpty { arguments += ["--since", since] }
            let model = model.trimmingCharacters(in: .whitespaces)
            if !model.isEmpty { arguments += ["-m", model] }
            if !posting { arguments.append("--dry-run") } else if allowPost { arguments.append("--allow-post") }
            return arguments + ["--output-format", "json"]
        }
    }

    /// What waits on the person's word.
    public enum Pending: Equatable, Sendable {
        /// Post a review: the batch its dry run found, run again with `--yes`.
        case post(ReviewRun)
        /// A sweep that posts: where targets grant it, or with `--allow-post`
        /// on every target it reaches.
        case sweep(allowPost: Bool)
        case deleteTarget(String)
    }

    public enum Sheet: Equatable, Sendable {
        case addTarget
        case sweep
    }

    public var tab: Tab = .runs
    public var form = RunForm()
    public var targetForm = TargetForm()
    public var sweepForm = SweepForm()
    public private(set) var targets: [ReviewTarget] = []
    public private(set) var skills: [ReviewSkill] = []
    public private(set) var skillProblems: [String] = []
    /// The skill open in full, `review skills show`.
    public private(set) var shownSkill: ReviewSkill?
    public var selectedSkill: String?
    /// The review on screen: the last run's document.
    public private(set) var run: ReviewRun?
    /// The arguments it ran with -- what a post repeats with `--yes`.
    public private(set) var runArguments: [String]?
    /// The last sweep's document.
    public private(set) var sweep: ReviewSweep?
    public var sheet: Sheet?
    public var pending: Pending?
    /// The act running now, and its progress -- the CLI's narration its busy line.
    public private(set) var running: String?
    public private(set) var progress: JobProgress?
    /// A refusal or failure, verbatim -- a forge's included, with its fix.
    public private(set) var failure: String?
    public var say: (@MainActor (String) -> Void)?

    private let runner: any CommandRunning
    private let starter: any JobStarting
    private var binary: URL?
    private var job: (any JobRunning)?

    public init(runner: any CommandRunning, starter: any JobStarting) {
        self.runner = runner
        self.starter = starter
    }

    public func connect(binary: URL?) async {
        self.binary = binary
        await refresh()
    }

    public func appeared() async {
        await refresh()
    }

    /// The targets and the skills, read again.
    public func refresh() async {
        guard let binary else {
            failure = "Apogee isn't installed, so there is nothing to review with."
            return
        }
        targets = await runner.read(binary: binary, ["review", "targets"]).document.map(ReviewTarget.list) ?? targets
        if let document = await runner.read(binary: binary, ["review", "skills", "list"]).document {
            skills = ReviewSkill.list(from: document)
            skillProblems = (document.value["problems"]?.arrayValue ?? []).compactMap { $0.stringValue ?? $0["message"]?.stringValue }
        }
    }

    /// `review skills show <name>`: the lens whole, its prompt read-only.
    public func show(skill name: String) async {
        selectedSkill = name
        guard let binary else { return }
        let outcome = await runner.read(binary: binary, ["review", "skills", "show", name])
        if let document = outcome.document, let skill = ReviewSkill(document: document) {
            shownSkill = skill
        } else {
            failure = outcome.failure
        }
    }

    // MARK: - A review

    /// Reviews the merge request and posts nothing: `--dry-run`, the default
    /// posture.
    public func review() {
        guard let arguments = form.arguments(posting: false) else { return }
        start(arguments, doing: "Reviewing \(form.ref.trimmingCharacters(in: .whitespaces))") { [weak self] document in
            guard let self, let run = ReviewRun(document: document) else { return false }
            self.run = run
            self.runArguments = arguments
            return true
        }
    }

    /// Asks before posting what the shown run found -- never for a silent
    /// one, which has nothing to post.
    public func askToPost() {
        guard let run, !run.findings.isEmpty, run.dryRun else { return }
        pending = .post(run)
    }

    /// What the post's consent says: what goes, and that it is a fresh run.
    public static func postWords(_ run: ReviewRun) -> String {
        let inline = run.inlineFindings.count
        let over = run.overCap.count
        var text = "The dry run found \(inline) \(inline == 1 ? "finding" : "findings") to post inline"
        if over > 0 { text += " and \(over) for the summary alone" }
        text += ". Posting runs the review again with --yes and posts what that run finds, as one batch on \(run.mergeRequest): its inline comments and a summary."
        return text
    }

    // MARK: - Targets and sweeps

    public func newTarget() {
        targetForm = TargetForm()
        sheet = .addTarget
    }

    public func addTarget() async {
        guard let arguments = targetForm.arguments else { return }
        sheet = nil
        await change(arguments)
    }

    public func askToDelete(target name: String) {
        pending = .deleteTarget(name)
    }

    public func newSweep() {
        sweepForm = SweepForm()
        sheet = .sweep
    }

    /// A sweep that posts nothing: `--dry-run`.
    public func sweepDry() {
        sheet = nil
        let arguments = sweepForm.arguments(posting: false, allowPost: false)
        startSweep(arguments)
    }

    /// Asks before a sweep that posts.
    public func askToSweep(allowPost: Bool) {
        sheet = nil
        pending = .sweep(allowPost: allowPost)
    }

    /// The targets a posting sweep would post on: those it reaches whose grant
    /// is their own `post: true` -- or every one with `--allow-post`.
    public func postingTargets(allowPost: Bool) -> [String] {
        let swept = sweepForm.targets.isEmpty ? targets : targets.filter { sweepForm.targets.contains($0.name) }
        return swept.filter { allowPost || $0.post }.map(\.name)
    }

    private func startSweep(_ arguments: [String]) {
        start(arguments, doing: "Sweeping") { [weak self] document in
            guard let self, let sweep = ReviewSweep(document: document) else { return false }
            self.sweep = sweep
            return true
        }
    }

    // MARK: - Consent

    /// The person's yes: the one place `--yes`, `--allow-post` and a delete
    /// are passed.
    public func confirm() async {
        guard let pending else { return }
        self.pending = nil
        switch pending {
        case .post(let shown):
            guard let arguments = runArguments else { return }
            let posting = arguments.map { $0 == "--dry-run" ? "--yes" : $0 }
            start(posting, doing: "Posting to \(shown.mergeRequest)") { [weak self] document in
                guard let self, let run = ReviewRun(document: document) else { return false }
                self.run = run
                self.runArguments = posting
                return true
            }
        case .sweep(let allowPost):
            startSweep(sweepForm.arguments(posting: true, allowPost: allowPost))
        case .deleteTarget(let name):
            await change(["review", "targets", "delete", name])
        }
    }

    public func dismissPending() {
        pending = nil
    }

    // MARK: - Running

    public var isRunning: Bool { job != nil }

    public func stop() async {
        await job?.stop(grace: .seconds(10))
    }

    /// Runs `arguments` supervised; its stdout's document goes to `read`,
    /// which says whether it was the document it expected. A run that leaves
    /// none failed, in the CLI's own last words -- a forge's refusal with its
    /// fix among them.
    private func start(_ arguments: [String], doing: String, read: @escaping @MainActor (ReadDocument) -> Bool) {
        guard let binary, job == nil else { return }
        failure = nil
        running = doing
        progress = JobProgress()
        let job = starter.startJob(binary: binary, arguments: arguments, streaming: false, folder: nil)
        self.job = job
        Task {
            var stdout: [String] = []
            for await update in job.updates {
                if case .said(let line) = update { stdout.append(line) }
                progress?.apply(update)
            }
            self.job = nil
            let ended = progress
            progress = nil
            running = nil
            if let document = ReadDocument(text: stdout.joined(separator: "\n")), read(document) {
                if case .failed(let message)? = ended?.status { failure = message }
            } else if case .failed(let message)? = ended?.status {
                failure = message
            } else if case .stopped? = ended?.status {
                say?("Stopped")
            }
            await refresh()
        }
    }

    private func change(_ arguments: [String]) async {
        guard let binary else { return }
        let outcome = await runner.command(binary: binary, arguments)
        await refresh()
        if let failure = outcome.failure {
            self.failure = failure
        } else if case .done(let stdout) = outcome {
            say?(stdout.split(whereSeparator: \.isNewline).first.map(String.init) ?? "Done")
        }
    }

    public func dismissFailure() {
        failure = nil
    }
}
