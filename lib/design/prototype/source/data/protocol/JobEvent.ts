import { EventDecoder } from './EventDecoder'
import { JSONValue } from './JSONValue'
import type { JSONObject } from './JSONValue'

const { int, number, string } = JSONValue

/**
 * One line a long job's stream says (the CLI's 52b, `--output-format
 * stream-json`: `models pull`, `video generate`, `video compose`, `voice say`,
 * `train pipeline run|resume`, `train cycle run`), read tolerantly: an event
 * this client does not know is kept, never dropped. The Mac app's `JobEvent`
 * (`BusinessJobs`, `JobEvent.swift`); here decoding is Data's, as design 1d
 * places it, under the same name.
 */
export type JobEvent =
  /** First: the verb. */
  | { readonly case: 'job'; readonly verb: string }
  /** A step of a pull's chain, or a pipeline's stage as its manifest records it. */
  | {
      readonly case: 'stage'
      readonly index: number
      readonly count: number
      readonly label: string
      readonly state: string
      readonly pipelineRunID: string | undefined
      readonly runID: string | undefined
      readonly cumulativeScore: number | undefined
    }
  /** A training iteration, as the trainer reported it. */
  | {
      readonly case: 'iteration'
      readonly iteration: number
      readonly iterations: number | undefined
      readonly loss: number | undefined
      readonly lr: number | undefined
      readonly throughput: number | undefined
      readonly stage: number | undefined
    }
  /** A line of a cycle pass's narration. */
  | { readonly case: 'pass'; readonly state: string; readonly text: string }
  /** Bytes moved; `total` only when known, never guessed. */
  | {
      readonly case: 'bytes'
      readonly done: number
      readonly total: number | undefined
      readonly file: string | undefined
      readonly index: number | undefined
      readonly count: number | undefined
    }
  /** A runtime loaded. */
  | {
      readonly case: 'ready'
      readonly device: string | undefined
      readonly dtype: string | undefined
      readonly mode: string | undefined
      readonly scene: number | undefined
    }
  /** A denoising step done. */
  | { readonly case: 'step'; readonly step: number; readonly steps: number; readonly scene: number | undefined }
  | {
      readonly case: 'frames'
      readonly frames: number
      readonly width: number | undefined
      readonly height: number | undefined
      readonly scene: number | undefined
    }
  | { readonly case: 'assembling'; readonly frames: number; readonly scene: number | undefined }
  /** Speech made: segment `index` of `of`, its `seconds`. */
  | { readonly case: 'segment'; readonly index: number; readonly of: number; readonly seconds: number | undefined; readonly scene: number | undefined }
  /** A composition's scene begun: `speaking`, `rendering` or `asset`. */
  | { readonly case: 'scene'; readonly scene: number; readonly scenes: number; readonly state: string }
  /** A line the verb printed for people. */
  | { readonly case: 'notice'; readonly text: string }
  /** The verb's own document: last on success. */
  | { readonly case: 'document'; readonly document: JSONValue }
  /** The run failed, in stderr's words. */
  | { readonly case: 'error'; readonly message: string }
  /** A line this client does not read here, kept whole. */
  | { readonly case: 'other'; readonly type: string; readonly fields: JSONObject }

export const JobEvent = {
  /** The line `line` is. */
  decode(line: string): JobEvent {
    const event = EventDecoder.decode(line)
    const f = event.fields
    switch (event.kind.case) {
      case 'notice':
        return { case: 'notice', text: event.kind.text }
      case 'error':
        return { case: 'error', message: event.kind.message }
      case 'unreadable':
        return { case: 'other', type: 'unreadable', fields: { line: event.kind.line } }
      default:
        break
    }
    return read(event.type, f) ?? { case: 'other', type: event.type, fields: f }
  },
}

function read(type: string, f: JSONObject): JobEvent | undefined {
  switch (type) {
    case 'job':
      return { case: 'job', verb: string(f.verb) ?? '' }
    case 'stage': {
      const index = int(f.index)
      const count = int(f.count)
      if (index === undefined || count === undefined) return undefined
      return {
        case: 'stage',
        index,
        count,
        label: string(f.label) ?? '',
        state: string(f.state) ?? '',
        pipelineRunID: string(f.pipeline_run_id),
        runID: string(f.run_id),
        cumulativeScore: number(f.cumulative_score),
      }
    }
    case 'iteration': {
      const iteration = int(f.iteration)
      if (iteration === undefined) return undefined
      return {
        case: 'iteration',
        iteration,
        iterations: int(f.iterations),
        loss: number(f.loss),
        lr: number(f.lr),
        throughput: number(f.throughput),
        stage: int(f.stage),
      }
    }
    case 'pass': {
      const state = string(f.state)
      return state === undefined ? undefined : { case: 'pass', state, text: string(f.text) ?? '' }
    }
    case 'bytes': {
      const done = int(f.done)
      if (done === undefined) return undefined
      return { case: 'bytes', done, total: int(f.total), file: string(f.file), index: int(f.index), count: int(f.count) }
    }
    case 'ready':
      return { case: 'ready', device: string(f.device), dtype: string(f.dtype), mode: string(f.mode), scene: int(f.scene) }
    case 'step': {
      const step = int(f.step)
      const steps = int(f.steps)
      return step === undefined || steps === undefined ? undefined : { case: 'step', step, steps, scene: int(f.scene) }
    }
    case 'frames': {
      const frames = int(f.frames)
      return frames === undefined ? undefined : { case: 'frames', frames, width: int(f.width), height: int(f.height), scene: int(f.scene) }
    }
    case 'assembling': {
      const frames = int(f.frames)
      return frames === undefined ? undefined : { case: 'assembling', frames, scene: int(f.scene) }
    }
    case 'segment': {
      const index = int(f.index)
      const of = int(f.of)
      return index === undefined || of === undefined ? undefined : { case: 'segment', index, of, seconds: number(f.seconds), scene: int(f.scene) }
    }
    case 'scene': {
      const scene = int(f.scene)
      const scenes = int(f.scenes)
      return scene === undefined || scenes === undefined ? undefined : { case: 'scene', scene, scenes, state: string(f.state) ?? '' }
    }
    case 'document':
      return f.document === undefined ? undefined : { case: 'document', document: f.document }
    default:
      return undefined
  }
}
