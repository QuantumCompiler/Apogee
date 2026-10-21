import DataProtocol
import Foundation

/// One finding of a review, the finding contract's fields (the shipped
/// `code-review` agent's) plus where its comment anchors.
public struct ReviewFinding: Sendable, Equatable, Identifiable {
    public let severity: String
    public let kind: String
    public let file: String
    /// The line its comment anchors to; `nil` when it has none.
    public let line: Int?
    public let location: String
    public let claim: String
    public let suggestedFix: String
    public let confidence: String
    public let identity: String
    /// Every lens that found it.
    public let foundBy: [String]
    /// The confirm pass reworded it.
    public let tightened: Bool
    /// `false` past the cap: listed in the summary instead of inline.
    public let inline: Bool
    public var id: String { identity.isEmpty ? "\(file):\(location):\(claim)" : identity }

    public init?(_ value: JSONValue) {
        guard let claim = value["claim"]?.stringValue else { return nil }
        self.claim = claim
        severity = value["severity"]?.stringValue ?? ""
        kind = value["kind"]?.stringValue ?? ""
        file = value["file"]?.stringValue ?? ""
        line = value["line"]?.intValue
        location = value["location"]?.stringValue ?? ""
        suggestedFix = value["suggested_fix"]?.stringValue ?? ""
        confidence = value["confidence"]?.stringValue ?? ""
        identity = value["identity"]?.stringValue ?? ""
        foundBy = (value["found_by"]?.arrayValue ?? []).compactMap(\.stringValue)
        tightened = value["tightened"]?.boolValue ?? false
        inline = value["inline"]?.boolValue ?? true
    }
}

/// `review mr <ref> --output-format json` (the CLI's 48c): one review's
/// document, whether or not it posted.
public struct ReviewRun: Sendable, Equatable {
    /// What happened to the batch: posted, or why not -- in the CLI's words.
    public enum Posted: Sendable, Equatable {
        case posted(comments: Int, summary: Bool, batched: Bool)
        case notPosted(reason: String)
    }

    /// The canonical spelling: `github.com/owner/repo#12`.
    public let mergeRequest: String
    public let url: String
    public let title: String
    public let head: String
    /// `full`, `incremental` (with `since`) or `already reviewed`.
    public let mode: String
    public let since: String?
    public let model: String
    public let skills: [String]
    /// `named`, `hints`, `hints, narrowed`.
    public let plan: String
    public let leftOutSkills: [String]
    public let excludedFiles: [String]
    public let chunks: Int
    public let findings: [ReviewFinding]
    /// What each step left out, by its name: `candidates`, `off_change` ...
    public let counts: [(String, Int)]
    public let posted: Posted
    public let dryRun: Bool
    public let notes: [String]

    public init?(_ value: JSONValue) {
        guard value["object"]?.stringValue == "review.run" else { return nil }
        mergeRequest = value["merge_request"]?.stringValue ?? ""
        url = value["url"]?.stringValue ?? ""
        title = value["title"]?.stringValue ?? ""
        head = value["head"]?.stringValue ?? ""
        mode = value["mode"]?.stringValue ?? ""
        since = value["since"]?.stringValue
        model = value["model"]?.stringValue ?? ""
        skills = (value["skills"]?.arrayValue ?? []).compactMap(\.stringValue)
        plan = value["plan"]?.stringValue ?? ""
        leftOutSkills = (value["left_out_skills"]?.arrayValue ?? []).compactMap { $0.stringValue ?? $0["name"]?.stringValue }
        excludedFiles = (value["excluded_files"]?.arrayValue ?? []).compactMap(\.stringValue)
        chunks = value["chunks"]?.intValue ?? 0
        findings = (value["findings"]?.arrayValue ?? []).compactMap(ReviewFinding.init)
        let order = ["candidates", "off_change", "not_new", "known", "duplicates", "dismissed", "tightened"]
        counts = order.compactMap { key in value["counts"]?[key]?.intValue.map { (key, $0) } }
        if value["posted"]?["done"]?.boolValue == true {
            posted = .posted(
                comments: value["posted"]?["comments"]?.intValue ?? 0, summary: value["posted"]?["summary"]?.boolValue ?? false,
                batched: value["posted"]?["batched"]?.boolValue ?? false)
        } else {
            posted = .notPosted(reason: value["posted"]?["reason"]?.stringValue ?? "")
        }
        dryRun = value["dry_run"]?.boolValue ?? false
        notes = (value["notes"]?.arrayValue ?? []).compactMap(\.stringValue)
    }

    public init?(document: ReadDocument) {
        self.init(document.value)
    }

    /// A review that found nothing to say: the CLI's law -- silence is a
    /// complete review, never an empty-looking failure.
    public var isSilent: Bool { findings.isEmpty && mode != "already reviewed" }

    /// The findings that would go inline, and those past the cap.
    public var inlineFindings: [ReviewFinding] { findings.filter(\.inline) }
    public var overCap: [ReviewFinding] { findings.filter { !$0.inline } }

    public static func == (lhs: ReviewRun, rhs: ReviewRun) -> Bool {
        lhs.mergeRequest == rhs.mergeRequest && lhs.head == rhs.head && lhs.mode == rhs.mode && lhs.findings == rhs.findings
            && lhs.posted == rhs.posted && lhs.dryRun == rhs.dryRun && lhs.notes == rhs.notes
            && lhs.counts.map(\.0) == rhs.counts.map(\.0) && lhs.counts.map(\.1) == rhs.counts.map(\.1)
    }
}

/// A sweep target, `review targets --output-format json` (the CLI's 48d):
/// the `review:` section's entry.
public struct ReviewTarget: Sendable, Equatable, Identifiable {
    public let name: String
    public let forge: String
    /// `owner/repo` for a repository target.
    public let repo: String?
    /// The owner whose repositories are swept.
    public let owner: String?
    public let host: String?
    /// Empty: each merge request's plan decides.
    public let skills: [String]
    /// The declared grant: a sweep posts here with nobody at the terminal.
    public let post: Bool
    public let drafts: Bool
    public let model: String?
    public let cli: String?
    public var id: String { name }

    public init?(_ value: JSONValue) {
        guard let name = value["name"]?.stringValue else { return nil }
        self.name = name
        forge = value["forge"]?.stringValue ?? ""
        repo = value["repo"]?.stringValue
        owner = value["owner"]?.stringValue
        host = value["host"]?.stringValue
        skills = (value["skills"]?.arrayValue ?? []).compactMap(\.stringValue)
        post = value["post"]?.boolValue ?? false
        drafts = value["drafts"]?.boolValue ?? false
        model = value["model"]?.stringValue
        cli = value["cli"]?.stringValue
    }

    public static func list(from document: ReadDocument) -> [ReviewTarget] {
        (document.entries ?? []).compactMap { ReviewTarget($0) }
    }
}

/// `review sweep --output-format json` (the CLI's 48d): every target swept
/// once, its watermarks, and each merge request's review.
public struct ReviewSweep: Sendable, Equatable {
    public struct Project: Sendable, Equatable, Identifiable {
        /// `github.com/owner/repo`.
        public let project: String
        public let watermarkBefore: String?
        public let watermarkAfter: String?
        /// It started at now: history stays unreviewed.
        public let firstSweep: Bool
        public let reviewed: [ReviewRun]
        public let skippedDrafts: Int
        public let error: String?
        public var id: String { project }
    }

    public struct Target: Sendable, Equatable, Identifiable {
        public let name: String
        /// `repo` or `owner`.
        public let kind: String
        /// `target`, `--allow-post` or `none`.
        public let grant: String
        public let projects: [Project]
        public let error: String?
        public var id: String { name }
    }

    public let started: String
    public let dryRun: Bool
    public let allowPost: Bool
    public let cap: Int
    public let targets: [Target]
    public let reviewed: Int
    public let posted: Int
    public let deferred: Int
    public let errors: Int
    /// Where the CLI kept the document.
    public let document: String

    public init?(document: ReadDocument) {
        let v = document.value
        guard v["object"]?.stringValue == "review.sweep" else { return nil }
        started = v["started"]?.stringValue ?? ""
        dryRun = v["dry_run"]?.boolValue ?? false
        allowPost = v["allow_post"]?.boolValue ?? false
        cap = v["cap"]?.intValue ?? 0
        targets = (v["targets"]?.arrayValue ?? []).compactMap { t in
            t["name"]?.stringValue.map { name in
                Target(
                    name: name, kind: t["kind"]?.stringValue ?? "", grant: t["grant"]?.stringValue ?? "",
                    projects: (t["projects"]?.arrayValue ?? []).compactMap { p in
                        p["project"]?.stringValue.map { project in
                            Project(
                                project: project, watermarkBefore: p["watermark_before"]?.stringValue,
                                watermarkAfter: p["watermark_after"]?.stringValue, firstSweep: p["first_sweep"]?.boolValue ?? false,
                                reviewed: (p["reviewed"]?.arrayValue ?? []).compactMap(ReviewRun.init),
                                skippedDrafts: p["skipped_drafts"]?.arrayValue?.count ?? p["skipped_drafts"]?.intValue ?? 0,
                                error: p["error"]?.stringValue)
                        }
                    },
                    error: t["error"]?.stringValue)
            }
        }
        reviewed = v["reviewed"]?.intValue ?? 0
        posted = v["posted"]?.intValue ?? 0
        deferred = v["deferred"]?.intValue ?? 0
        errors = v["errors"]?.intValue ?? 0
        self.document = v["document"]?.stringValue ?? ""
    }
}

/// A review skill, `review skills list | show --output-format json` (the
/// CLI's 48b): a markdown lens, where it comes from and what it applies to.
public struct ReviewSkill: Sendable, Equatable, Identifiable {
    public let name: String
    public let description: String
    /// `shipped`, `seeded` or `user`.
    public let source: String
    public let path: String?
    public let extensions: [String]
    public let paths: [String]
    public let languages: [String]
    /// No hints: it applies to every file.
    public let everywhere: Bool
    /// The hunt prompt -- `show`'s alone.
    public let prompt: String?
    public var id: String { name }

    public init?(_ value: JSONValue) {
        guard let name = value["name"]?.stringValue else { return nil }
        self.name = name
        description = value["description"]?.stringValue ?? ""
        source = value["source"]?.stringValue ?? ""
        path = value["path"]?.stringValue
        extensions = (value["applies"]?["extensions"]?.arrayValue ?? []).compactMap(\.stringValue)
        paths = (value["applies"]?["paths"]?.arrayValue ?? []).compactMap(\.stringValue)
        languages = (value["applies"]?["languages"]?.arrayValue ?? []).compactMap(\.stringValue)
        everywhere = value["everywhere"]?.boolValue ?? false
        prompt = value["prompt"]?.stringValue
    }

    public init?(document: ReadDocument) {
        guard document.value["object"]?.stringValue == "review.skill" else { return nil }
        self.init(document.value)
    }

    public static func list(from document: ReadDocument) -> [ReviewSkill] {
        (document.entries ?? []).compactMap { ReviewSkill($0) }
    }
}
