import DataProtocol
import Foundation

/// One row of `task list --output-format json`.
public struct TaskSummary: Sendable, Equatable, Identifiable {
    public init(id: String, goal: String, status: String, roundsUsed: Int, roundsBudget: Int) {
        self.id = id
        self.goal = goal
        self.status = status
        self.roundsUsed = roundsUsed
        self.roundsBudget = roundsBudget
    }

    public let id: String
    public let goal: String
    /// `planning`, `running`, `done`, `exhausted`, `stalled`, `failed`,
    /// `halted`, `cancelled` -- a string that may grow.
    public let status: String
    public let roundsUsed: Int
    public let roundsBudget: Int

    public static func list(from document: ReadDocument) -> [TaskSummary] {
        (document.entries ?? []).compactMap { entry in
            guard let id = entry["id"]?.stringValue else { return nil }
            return TaskSummary(
                id: id, goal: entry["goal"]?.stringValue ?? "", status: entry["status"]?.stringValue ?? "",
                roundsUsed: entry["rounds_used"]?.intValue ?? 0, roundsBudget: entry["rounds_budget"]?.intValue ?? 0)
        }
    }

    public var isRunning: Bool { TaskView.running.contains(status) }
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

    /// One turn of the task: the plan (round 0) or a round.
    public struct Round: Sendable, Equatable {
        public let round: Int
        /// `plan`, `execute`, `correct`.
        public let kind: String
        /// `in_flight`, `completed`, `failed` ...
        public let outcome: String
        public let checksPassed: Int
        public let selfReport: String
        public let allowed: [String]
        public let denied: [String]
        public let tokens: Int
        public let tokensEstimated: Bool
        public let startedAt: String
        public let endedAt: String

        init(_ value: JSONValue) {
            func strings(_ key: String) -> [String] {
                (value[key]?.arrayValue ?? []).map { $0.stringValue ?? $0.serialized }
            }
            round = value["round"]?.intValue ?? 0
            kind = value["kind"]?.stringValue ?? ""
            outcome = value["outcome"]?.stringValue ?? ""
            checksPassed = value["checks_passed"]?.intValue ?? 0
            selfReport = value["self_report"]?.stringValue ?? ""
            allowed = strings("allowed")
            denied = strings("denied")
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
    /// The tools granted up front.
    public let grants: [String]
    public let createdAt: String
    public let updatedAt: String
    /// The process running it, when one is.
    public let process: Int?

    /// The statuses a task is still under way in.
    public static let running: Set<String> = ["planning", "running"]

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

    public var isRunning: Bool { Self.running.contains(status) }
}
