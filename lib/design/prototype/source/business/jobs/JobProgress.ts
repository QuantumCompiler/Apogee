import type { JobEvent } from '../../data/protocol/JobEvent'
import type { JSONValue } from '../../data/protocol/JSONValue'
import { JobEnd } from './SupervisedJob'
import type { JobUpdate } from './SupervisedJob'

/** How a job stands: the Mac's `JobProgress.Status`. */
export type JobStatus =
  | { readonly case: 'running' }
  /** Stopped by its Stop; what it finished stands. */
  | { readonly case: 'stopped' }
  /** It failed: its last words. */
  | { readonly case: 'failed'; readonly message: string }
  | { readonly case: 'done' }

/** One step of a chain, as the stream named it: `started`, `done`, `failed`, or `stopped` once its run was -- or a pipeline stage's manifest word (design 1j). */
export interface JobStage {
  readonly label: string
  readonly state: string
  /** A pipeline stage's own run, once it has one: what `train eval` and `train promote` take (1j). */
  readonly runID?: string | undefined
  /** A pipeline stage's cumulative score as its latest event carried it, read as the gate's only once the stage is past its gate (1j). */
  readonly cumulativeScore?: number | undefined
}

/** The trainer's latest iteration, as it reported it (the CLI's 55h, design 1j). */
export interface JobIteration {
  readonly iteration: number
  readonly iterations: number | undefined
  readonly loss: number | undefined
  readonly lr: number | undefined
  /** Iterations a second. */
  readonly throughput: number | undefined
  /** The pipeline stage it belongs to, from 1. */
  readonly stage: number | undefined
}

/** A line of a cycle pass's narration: where the pass is, in its words (55h, design 1j). */
export interface JobPassLine {
  readonly state: string
  readonly text: string
}

/** A count of a whole, as a media run last said it: a denoising step of its steps (design 1k). */
export interface JobCount {
  readonly done: number
  readonly of: number
}

/** The runtime a media run loaded, as its `ready` event said it (design 1k). */
export interface JobReady {
  readonly device: string | undefined
  readonly dtype: string | undefined
  readonly mode: string | undefined
}

/** The frames a clip's driver wrote (design 1k). */
export interface JobFrames {
  readonly frames: number
  readonly width: number | undefined
  readonly height: number | undefined
}

/** Bytes a run has moved, as it last said; `total` only when known. */
export interface JobBytes {
  readonly done: number
  readonly total: number | undefined
  readonly file: string | undefined
  readonly index: number | undefined
  readonly count: number | undefined
  /** 0 to 1, when the total is known. */
  readonly fraction: number | undefined
}

/**
 * A job's progress, folded from its updates and nothing else (design 1h): its
 * steps, its bytes, what it said, its documents and how it ended. The Mac
 * app's `JobProgress` (`BusinessJobs`, `JobProgress.swift`), the parts a
 * store job and a pull read, and a training run's (design 1j): a pipeline's
 * stages as its manifest moved them, the run they belong to, the trainer's
 * latest iteration and a cycle pass's narration -- a pure reducer, so a
 * screen's whole view of a run is a function of what the run said. A clip's
 * media events (design 1k): the runtime that loaded, the denoising step, the
 * frames written and their assembly; a speech's segments and a
 * composition's scenes are 1m's and 1l's.
 */
export class JobProgress {
  status: JobStatus = { case: 'running' }
  /** The chain's stages, by index (1-based), as the stream named them. */
  readonly stages = new Map<number, JobStage>()
  stageCount: number | undefined = undefined
  bytes: JobBytes | undefined = undefined
  /** What the verb said for people, in order. */
  readonly notices: string[] = []
  /** The busy line: shown, never decided on. */
  busy: string | undefined = undefined
  /** The verb's documents, in order: a pull's one per set stored. */
  readonly documents: JSONValue[] = []
  /** What the run said last for people. */
  lastSaid: string | undefined = undefined
  /** Whether the run speaks the 52b stream. */
  streamed = false
  /** The stderr tail at the end. */
  stderr = ''
  /** The stream's own `error`, when one came: read before stderr's. */
  error: string | undefined = undefined
  /** The pipeline run its stages belong to (55h): a pipeline's own, or the one a cycle pass trains (1j). */
  pipelineRunID: string | undefined = undefined
  /** The trainer's latest iteration (55h, 1j). */
  iteration: JobIteration | undefined = undefined
  /** A cycle pass's narration so far, in order (55h, 1j). */
  readonly passes: JobPassLine[] = []
  /** The media events' latest (design 1k, the Mac's): the runtime loaded and its device, the denoising step, the frames written, and the frames going into the clip. */
  ready: JobReady | undefined = undefined
  device: string | undefined = undefined
  step: JobCount | undefined = undefined
  frames: JobFrames | undefined = undefined
  assembling: number | undefined = undefined

  apply(update: JobUpdate): void {
    switch (update.case) {
      case 'said':
        if (update.line.trim() !== '') {
          this.notices.push(update.line)
          this.lastSaid = update.line
        }
        return
      case 'busy':
        this.busy = update.line
        return
      case 'event':
        this.streamed = true
        this.applyEvent(update.event)
        return
      case 'ended':
        this.end(update.end)
        return
    }
  }

  private end(end: JobEnd): void {
    this.stderr = end.stderrTail.text
    if (JobEnd.succeeded(end)) this.status = { case: 'done' }
    else if (end.interrupted) this.status = { case: 'stopped' }
    else if (this.status.case !== 'failed') {
      const how = end.exit ? `it ${end.exit.reason === 'exited' ? `exited with status ${end.exit.status}` : `ended by signal ${end.exit.status}`}` : 'it did not start'
      this.status = { case: 'failed', message: end.detail ?? end.stderrTail.lastLine ?? how }
    }
    if (this.status.case === 'done') return
    const word = this.status.case === 'stopped' ? 'stopped' : 'failed'
    for (const [index, stage] of this.stages) if (stage.state === 'started') this.stages.set(index, { ...stage, state: word })
  }

  private applyEvent(event: JobEvent): void {
    switch (event.case) {
      case 'stage': {
        this.stageCount = event.count
        const runID = event.runID ?? this.stages.get(event.index)?.runID
        this.stages.set(event.index, runID === undefined && event.cumulativeScore === undefined ? { label: event.label, state: event.state } : { label: event.label, state: event.state, runID, cumulativeScore: event.cumulativeScore })
        if (event.pipelineRunID !== undefined) this.pipelineRunID = event.pipelineRunID
        if (event.state === 'started') this.bytes = undefined
        return
      }
      case 'iteration':
        this.iteration = { iteration: event.iteration, iterations: event.iterations, loss: event.loss, lr: event.lr, throughput: event.throughput, stage: event.stage }
        return
      case 'pass':
        this.passes.push({ state: event.state, text: event.text })
        if (event.text !== '') this.lastSaid = event.text
        return
      case 'bytes': {
        const fraction = event.total !== undefined && event.total > 0 ? Math.min(1, event.done / event.total) : undefined
        this.bytes = { done: event.done, total: event.total, file: event.file, index: event.index, count: event.count, fraction }
        return
      }
      case 'ready':
        this.ready = { device: event.device, dtype: event.dtype, mode: event.mode }
        this.device = event.device
        return
      case 'step':
        this.step = { done: event.step, of: event.steps }
        return
      case 'frames':
        this.frames = { frames: event.frames, width: event.width, height: event.height }
        return
      case 'assembling':
        this.assembling = event.frames
        return
      case 'notice':
        this.notices.push(event.text)
        this.lastSaid = event.text
        return
      case 'document':
        this.documents.push(event.document)
        return
      case 'error':
        this.error = event.message
        this.status = { case: 'failed', message: event.message }
        return
      default:
        return
    }
  }

  /** The stages in order, 1 to the count the stream gave. */
  orderedStages(): { readonly index: number; readonly stage: JobStage }[] {
    return [...this.stages.keys()].sort((a, b) => a - b).map((index) => ({ index, stage: this.stages.get(index) as JobStage }))
  }

  /** The latest pass line of a cycle pass: where it is now (1j). */
  get pass(): JobPassLine | undefined {
    return this.passes.at(-1)
  }

  /** The first stage that did not finish: the one a resume runs again. */
  get unfinishedStage(): number | undefined {
    return this.orderedStages().find(({ stage }) => stage.state !== 'done')?.index
  }

  /**
   * The command a notice names for carrying on -- "the one command that
   * resumes it", in the CLI's own words -- on a run that stopped or failed. A
   * run still going or done names none: a chain says `from here on, if this
   * stops, resume with: …` mid-run, which is no resume while it runs.
   */
  get resumeCommand(): string[] | undefined {
    if (this.status.case !== 'stopped' && this.status.case !== 'failed') return undefined
    for (const notice of [...this.notices].reverse()) {
      const at = notice.indexOf('apogee ')
      if (at < 0) continue
      const rest = notice.slice(at + 'apogee '.length)
      const command = rest.split("'")[0] ?? rest
      const words = command
        .replace(/^[\s`.]+|[\s`.]+$/g, '')
        .split(' ')
        .filter((word) => word !== '')
      const verb = words[0]
      if (verb !== undefined && ['models', 'video', 'voice'].includes(verb) && words.length > 1) return words
    }
    return undefined
  }
}
