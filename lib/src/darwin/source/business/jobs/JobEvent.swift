import DataProtocol
import Foundation

/// One line a long job's stream says (the CLI's 52b: `models pull`,
/// `video generate`, `video compose`, `voice say` under
/// `--output-format stream-json`), read tolerantly: an event this client does
/// not know is kept, never dropped.
public enum JobEvent: Sendable, Equatable {
    /// First: the verb, and the versions.
    case job(verb: String)
    /// A step of a pull's chain: `index` of `count`, its label in the chain's
    /// words, `started`, `done` or `failed`.
    case stage(index: Int, count: Int, label: String, state: String)
    /// Bytes moved; `total` only when known, never guessed.
    case bytes(done: Int, total: Int?, file: String?, index: Int?, count: Int?)
    /// A runtime loaded.
    case ready(device: String?, dtype: String?, mode: String?, scene: Int?)
    /// A denoising step done.
    case step(step: Int, steps: Int, scene: Int?)
    case frames(frames: Int, width: Int?, height: Int?, scene: Int?)
    case assembling(frames: Int, scene: Int?)
    /// Speech made: segment `index` of `of`, its `seconds`.
    case segment(index: Int, of: Int, seconds: Double?, scene: Int?)
    /// A composition's scene begun: `speaking`, `rendering` or `asset`.
    case scene(scene: Int, scenes: Int, state: String)
    /// A line the verb printed for people.
    case notice(String)
    /// The verb's own document -- last on success; a pull's, one per set stored.
    case document(JSONValue)
    /// The run failed, in stderr's words.
    case error(String)
    /// A line this client does not read here -- a task's lifecycle, a chat
    /// turn's events, one a later build adds -- kept whole.
    case other(type: String, fields: [String: JSONValue])

    /// The line `line` is.
    public static func decode(line: String) -> JobEvent {
        let event = EventDecoder.decode(line: line)
        let f = event.fields
        func int(_ key: String) -> Int? { f[key]?.intValue }
        func string(_ key: String) -> String? { f[key]?.stringValue }
        func number(_ key: String) -> Double? { if case .number(let n)? = f[key] { n } else { nil } }
        switch event.kind {
        case .notice(let text): return .notice(text)
        case .error(let message): return .error(message)
        case .unreadable(let line): return .other(type: "unreadable", fields: ["line": .string(line)])
        default: break
        }
        switch event.type {
        case "job":
            return .job(verb: string("verb") ?? "")
        case "stage":
            guard let index = int("index"), let count = int("count") else { break }
            return .stage(index: index, count: count, label: string("label") ?? "", state: string("state") ?? "")
        case "bytes":
            guard let done = int("done") else { break }
            return .bytes(done: done, total: int("total"), file: string("file"), index: int("index"), count: int("count"))
        case "ready":
            return .ready(device: string("device"), dtype: string("dtype"), mode: string("mode"), scene: int("scene"))
        case "step":
            guard let step = int("step"), let steps = int("steps") else { break }
            return .step(step: step, steps: steps, scene: int("scene"))
        case "frames":
            guard let frames = int("frames") else { break }
            return .frames(frames: frames, width: int("width"), height: int("height"), scene: int("scene"))
        case "assembling":
            guard let frames = int("frames") else { break }
            return .assembling(frames: frames, scene: int("scene"))
        case "segment":
            guard let index = int("index"), let of = int("of") else { break }
            return .segment(index: index, of: of, seconds: number("seconds"), scene: int("scene"))
        case "scene":
            guard let scene = int("scene"), let scenes = int("scenes") else { break }
            return .scene(scene: scene, scenes: scenes, state: string("state") ?? "")
        case "document":
            guard let document = f["document"] else { break }
            return .document(document)
        default:
            break
        }
        return .other(type: event.type, fields: f)
    }
}
