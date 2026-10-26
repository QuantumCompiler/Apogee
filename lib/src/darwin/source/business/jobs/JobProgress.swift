import DataProtocol
import Foundation

/// A job's progress, folded from its updates and nothing else: the steps,
/// the bytes, what it said, its documents, how it ended -- and, for a
/// training run (the CLI's 55h), a pipeline's stages as its manifest moved
/// them, the trainer's latest iteration and a cycle pass's narration. A pure
/// reducer, so a screen's whole view of a run is a function of what the run
/// said.
public struct JobProgress: Sendable, Equatable {
    public enum Status: Sendable, Equatable {
        case running
        /// Stopped by its Stop; what it finished stands.
        case stopped
        /// It failed: its last words.
        case failed(String)
        case done
    }

    public struct Stage: Sendable, Equatable {
        public let label: String
        /// `started`, `done`, `failed` -- or a pipeline stage's manifest word:
        /// `pending`, `training`, `evaluating`, `fusing`, `passed`, `failed`.
        public var state: String
        /// A pipeline stage's own run, once it has one: what `train eval`
        /// and `train promote` take.
        public var runID: String? = nil
        /// A pipeline stage's cumulative score, 0 to 1, as its latest event
        /// carried it -- a resume's first write repeats a stage's last
        /// attempt's, so a screen reads it as the gate's only once the stage
        /// is past its gate.
        public var cumulativeScore: Double? = nil
    }

    /// The trainer's latest iteration, as it reported it.
    public struct Iteration: Sendable, Equatable {
        public let iteration: Int
        /// Absent when the trainer did not say.
        public let iterations: Int?
        public let loss: Double?
        public let lr: Double?
        /// Iterations a second.
        public let throughput: Double?
        /// The pipeline stage it belongs to, from 1.
        public let stage: Int?
    }

    /// A line of a cycle pass's narration: where the pass is, in its words.
    public struct PassLine: Sendable, Equatable {
        public let state: String
        public let text: String
    }

    public struct Bytes: Sendable, Equatable {
        public let done: Int
        /// Only when known.
        public let total: Int?
        public let file: String?
        public let index: Int?
        public let count: Int?

        /// 0...1, when the total is known.
        public var fraction: Double? {
            guard let total, total > 0 else { return nil }
            return min(1, Double(done) / Double(total))
        }
    }

    public private(set) var status: Status = .running
    /// The chain's stages, by index (1-based), as the stream named them.
    public private(set) var stages: [Int: Stage] = [:]
    public private(set) var stageCount: Int?
    public private(set) var bytes: Bytes?
    /// What the verb said for people, in order.
    public private(set) var notices: [String] = []
    /// The busy line: stderr's last line, shown and never decided on.
    public private(set) var busy: String?
    /// The verb's documents, in order: a pull's one per set stored.
    public private(set) var documents: [JSONValue] = []
    /// The media events' latest: denoising steps, a speech's segments, a scene.
    public private(set) var step: Count?
    public private(set) var scene: SceneState?
    public private(set) var segment: Count?
    public private(set) var device: String?
    /// The pipeline run its stages belong to (55h): a pipeline's own, or
    /// the one a cycle pass trains.
    public private(set) var pipelineRunID: String?
    /// The trainer's latest iteration (55h).
    public private(set) var iteration: Iteration?
    /// A cycle pass's narration so far, in order (55h).
    public private(set) var passes: [PassLine] = []
    /// What the run said last for people -- a line, a notice or a pass's
    /// narration -- whichever came last.
    public private(set) var lastSaid: String?
    /// Whether the run speaks the 52b stream.
    public private(set) var streamed = false
    /// The stderr tail at the end, for the details.
    public private(set) var stderr = ""

    /// `done` of `of`.
    public struct Count: Sendable, Equatable {
        public let done: Int
        public let of: Int
    }

    /// A composition's scene and what it is doing.
    public struct SceneState: Sendable, Equatable {
        public let scene: Int
        public let scenes: Int
        public let state: String
    }

    public init() {}

    public mutating func apply(_ update: JobUpdate) {
        switch update {
        case .said(let line):
            if !line.trimmingCharacters(in: .whitespaces).isEmpty {
                notices.append(line)
                lastSaid = line
            }
        case .busy(let line):
            busy = line
        case .event(let event):
            streamed = true
            apply(event)
        case .ended(let end):
            stderr = end.stderrTail.text
            if end.succeeded {
                status = .done
            } else if end.interrupted {
                status = .stopped
            } else if case .failed = status {
                // Its error event said why.
            } else {
                status = .failed(end.detail ?? end.stderrTail.lastLine ?? "it \(end.exit.map { "\($0)" } ?? "did not start")")
            }
            if status != .done {
                for (index, stage) in stages where stage.state == "started" {
                    stages[index]?.state = status == .stopped ? "stopped" : "failed"
                }
            }
        }
    }

    private mutating func apply(_ event: JobEvent) {
        switch event {
        case .job: break
        case .stage(let index, let count, let label, let state, let pipelineRunID, let runID, let score):
            stageCount = count
            stages[index] = Stage(label: label, state: state, runID: runID ?? stages[index]?.runID, cumulativeScore: score)
            if let pipelineRunID { self.pipelineRunID = pipelineRunID }
            if state == "started" { bytes = nil }
        case .iteration(let iteration, let iterations, let loss, let lr, let throughput, let stage):
            self.iteration = Iteration(
                iteration: iteration, iterations: iterations, loss: loss, lr: lr, throughput: throughput, stage: stage)
        case .pass(let state, let text):
            passes.append(PassLine(state: state, text: text))
            if !text.isEmpty { lastSaid = text }
        case .bytes(let done, let total, let file, let index, let count):
            bytes = Bytes(done: done, total: total, file: file, index: index, count: count)
        case .ready(let device, _, _, _):
            self.device = device
        case .step(let step, let steps, _):
            self.step = Count(done: step, of: steps)
        case .frames, .assembling, .other:
            break
        case .segment(let index, let of, _, _):
            segment = Count(done: index, of: of)
        case .scene(let scene, let scenes, let state):
            self.scene = SceneState(scene: scene, scenes: scenes, state: state)
            step = nil
        case .notice(let text):
            notices.append(text)
            lastSaid = text
        case .document(let document):
            documents.append(document)
        case .error(let message):
            status = .failed(message)
        }
    }

    /// The stages in order, 1 to the count the stream gave.
    public var orderedStages: [(index: Int, stage: Stage)] {
        stages.keys.sorted().map { ($0, stages[$0]!) }
    }

    /// The latest pass line of a cycle pass: where it is now.
    public var pass: PassLine? { passes.last }

    /// The first stage that did not finish -- the one a resume runs again.
    public var unfinishedStage: Int? {
        orderedStages.first { $0.stage.state != "done" }?.index
    }

    /// A command a notice names for carrying on -- "the one command that
    /// resumes it", in the CLI's own words -- when it names one, on a run
    /// that stopped or failed. A run still going or done has none: a chain
    /// says `from here on, if this stops, resume with: …` mid-run, and a
    /// conversion its next step at its end, on success too (the CLI's
    /// `model_chain.cpp`, `models convert`), and neither is a resume
    /// (GUI 4e).
    public var resumeCommand: [String]? {
        switch status {
        case .stopped, .failed: break
        case .running, .done: return nil
        }
        for notice in notices.reversed() {
            guard let range = notice.range(of: "apogee ") else { continue }
            let rest = notice[range.upperBound...]
            let command = rest.split(separator: "'").first.map(String.init) ?? String(rest)
            let words = command.trimmingCharacters(in: .whitespacesAndNewlines.union(CharacterSet(charactersIn: "`.")))
                .split(separator: " ").map(String.init)
            if let verb = words.first, ["models", "video", "voice"].contains(verb), words.count > 1 { return words }
        }
        return nil
    }
}
