import DataProtocol
import Foundation

/// One row of `task list --output-format json`.
public struct TaskSummary: Sendable, Equatable, Identifiable {
    public init(id: String, goal: String, status: String, roundsUsed: Int, roundsBudget: Int, interrupted: Bool? = nil) {
        self.id = id
        self.goal = goal
        self.status = status
        self.roundsUsed = roundsUsed
        self.roundsBudget = roundsBudget
        self.interrupted = interrupted
    }

    public let id: String
    public let goal: String
    /// `planning`, `running`, `done`, `exhausted`, `stalled`, `failed`,
    /// `halted`, `cancelled` -- a string that may grow.
    public let status: String
    public let roundsUsed: Int
    public let roundsBudget: Int
    /// Whether the row says its task was killed mid-round -- planning or
    /// running by its ledger, with no process running it -- as the CLI's M32
    /// puts it on every row (`false` when not). `nil` from a binary before
    /// M32, whose rows don't say: there only the task's page, `task status`,
    /// knows (`TaskView.interrupted`), and the row reads as its status.
    public let interrupted: Bool?

    public static func list(from document: ReadDocument) -> [TaskSummary] {
        (document.entries ?? []).compactMap { entry in
            guard let id = entry["id"]?.stringValue else { return nil }
            return TaskSummary(
                id: id, goal: entry["goal"]?.stringValue ?? "", status: entry["status"]?.stringValue ?? "",
                roundsUsed: entry["rounds_used"]?.intValue ?? 0, roundsBudget: entry["rounds_budget"]?.intValue ?? 0,
                interrupted: entry["interrupted"]?.boolValue)
        }
    }

    /// Whether a process runs it now, as far as its row says: under way by
    /// its ledger, and not killed mid-round. A row that can't say (before
    /// M32) reads as its status, as it always did.
    public var isRunning: Bool { TaskView.running.contains(status) && interrupted != true }
}

/// A task's whole view: what `task status <id> --output-format json` prints,
/// and `task_started` and `task_finished` carry.
public struct TaskView: Sendable, Equatable {
    public struct Check: Sendable, Equatable {
        /// `require`, `require_file`.
        public let kind: String
        public let value: String
        public let description: String
        public let ran: Bool
        public let passed: Bool
        public let detail: String
    }

    /// One gated tool call a round let through or refused: what `task status`
    /// prints as `round 1: write_file on report.txt -- nobody present to
    /// allow it`, and its document carries as `{"tool","target","by"}`
    /// (`view.cpp`'s `uses_json`).
    ///
    /// Decoded field by field, a field it does not know passed over (the
    /// CLI's ADR 0006); an entry that is a bare string -- an older or a
    /// different writer's -- is kept as the tool's name, never dropped and
    /// never shown as JSON.
    public struct Use: Sendable, Equatable {
        public init(tool: String, target: String = "", by: String = "") {
            self.tool = tool
            self.target = target
            self.by = by
        }

        /// The tool called: `write_file`, `run_command` ...
        public let tool: String
        /// What it was called on -- a path, a command line -- as the model
        /// gave it; empty when the call named none.
        public let target: String
        /// On whose authority: `grant`, `person` or `config` for a call let
        /// through; `config`, `nobody` or `person` for a refusal -- a string
        /// that may grow, empty when the entry did not say.
        public let by: String

        /// The entry `value` holds: an object read by its fields, a string
        /// as the tool's name; `nil` for any other shape, which names no
        /// tool.
        public init?(_ value: JSONValue) {
            if let name = value.stringValue {
                self.init(tool: name)
                return
            }
            guard value.objectValue != nil else { return nil }
            self.init(
                tool: value["tool"]?.stringValue ?? "", target: value["target"]?.stringValue ?? "",
                by: value["by"]?.stringValue ?? "")
        }

        /// The entries of a turn's `allowed` or `denied`.
        static func list(_ value: JSONValue?) -> [Use] {
            (value?.arrayValue ?? []).compactMap(Use.init)
        }
    }

    /// One turn of the task: the plan (round 0) or a round.
    public struct Round: Sendable, Equatable {
        public let round: Int
        /// `plan`, `execute`, `correct`.
        public let kind: String
        /// `in_flight`, `completed`, `failed` ...
        public let outcome: String
        public let checksPassed: Int
        public let selfReport: String
        /// The gated calls this turn let through, each with its authority.
        public let allowed: [Use]
        /// The gated calls this turn refused, each with why.
        public let denied: [Use]
        public let tokens: Int
        public let tokensEstimated: Bool
        public let startedAt: String
        public let endedAt: String

        init(_ value: JSONValue) {
            round = value["round"]?.intValue ?? 0
            kind = value["kind"]?.stringValue ?? ""
            outcome = value["outcome"]?.stringValue ?? ""
            checksPassed = value["checks_passed"]?.intValue ?? 0
            selfReport = value["self_report"]?.stringValue ?? ""
            allowed = Use.list(value["allowed"])
            denied = Use.list(value["denied"])
            tokens = value["tokens"]?.intValue ?? 0
            tokensEstimated = value["tokens_estimated"]?.boolValue ?? false
            startedAt = value["started_at"]?.stringValue ?? ""
            endedAt = value["ended_at"]?.stringValue ?? ""
        }
    }

    public let id: String
    public let goal: String
    public let status: String
    public let reason: String
    public let plan: String
    public let roundsUsed: Int
    public let roundsBudget: Int
    public let checks: [Check]
    public let rounds: [Round]
    public let conversation: String
    public let folder: String
    public let tools: Bool
    /// The tools granted up front: `policy.grants`, which the CLI writes
    /// only for a task with tools -- empty for one granted none.
    public let grants: [String]
    public let createdAt: String
    public let updatedAt: String
    /// The process running it, when one is.
    public let process: Int?
    /// Planning or running by its ledger with no process running it: killed
    /// mid-round, and resumable (`view.cpp`'s `interrupted`). `false` from a
    /// binary that never says.
    public let interrupted: Bool

    /// The statuses a task is still under way in, by its ledger.
    public static let running: Set<String> = ["planning", "running"]
    /// The stopped statuses `task resume` continues: halted, cancelled,
    /// stalled, failed. Never `done` or `exhausted`, which the CLI refuses
    /// by construction (`runner.cpp`'s `resume_refusal`); a planning or
    /// running one only once it is `interrupted`, since a live one's process
    /// holds the task lock.
    public static let resumable: Set<String> = ["halted", "cancelled", "stalled", "failed"]

    public init?(_ value: JSONValue) {
        guard let id = value["id"]?.stringValue else { return nil }
        func text(_ key: String) -> String { value[key]?.stringValue ?? "" }
        self.id = id
        goal = text("goal")
        status = text("status")
        reason = text("reason")
        plan = text("plan")
        roundsUsed = value["rounds_used"]?.intValue ?? 0
        roundsBudget = value["rounds_budget"]?.intValue ?? 0
        checks = (value["checks"]?.arrayValue ?? []).map { Self.check($0) }
        rounds = (value["turns"]?.arrayValue ?? []).map(Round.init)
        conversation = text("conversation")
        folder = text("folder")
        tools = value["tools"]?.boolValue ?? false
        grants = (value["policy"]?["grants"]?.arrayValue ?? []).compactMap(\.stringValue)
        createdAt = text("created_at")
        updatedAt = text("updated_at")
        process = value["process"]?.intValue
        interrupted = value["interrupted"]?.boolValue ?? false
    }

    public init?(document: ReadDocument) {
        self.init(document.value)
    }

    static func check(_ value: JSONValue) -> Check {
        Check(
            kind: value["kind"]?.stringValue ?? "", value: value["value"]?.stringValue ?? "",
            description: value["description"]?.stringValue ?? "", ran: value["ran"]?.boolValue ?? false,
            passed: value["passed"]?.boolValue ?? false, detail: value["detail"]?.stringValue ?? "")
    }

    public static func checks(_ value: JSONValue?) -> [Check] {
        (value?.arrayValue ?? []).map(check)
    }

    public static func round(_ value: JSONValue) -> Round { Round(value) }

    /// Whether a process runs it now: under way by its ledger, and not
    /// killed mid-round.
    public var isRunning: Bool { Self.running.contains(status) && !interrupted }

    /// Whether `task resume` continues it, by its state: the CLI's set --
    /// "halted, cancelled, stalled, failed, or killed mid-round" -- and no
    /// other. Where it resumes is `folder`, and nowhere else.
    public var isResumable: Bool {
        Self.resumable.contains(status) || (Self.running.contains(status) && interrupted)
    }
}
