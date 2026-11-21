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

/** One step of a chain, as the stream named it: `started`, `done`, `failed`, or `stopped` once its run was. */
export interface JobStage {
  readonly label: string
  readonly state: string
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
 * store job and a pull read -- a pure reducer, so a screen's whole view of a
 * run is a function of what the run said. The training run's iterations and
 * passes, and the media events, are the items that port those places.
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
      case 'stage':
        this.stageCount = event.count
        this.stages.set(event.index, { label: event.label, state: event.state })
        if (event.state === 'started') this.bytes = undefined
        return
      case 'bytes': {
        const fraction = event.total !== undefined && event.total > 0 ? Math.min(1, event.done / event.total) : undefined
        this.bytes = { done: event.done, total: event.total, file: event.file, index: event.index, count: event.count, fraction }
        return
      }
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
