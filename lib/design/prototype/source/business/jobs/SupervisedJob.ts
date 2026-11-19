import { JobEvent } from '../../data/protocol/JobEvent'
import type { Clock } from '../../infrastructure/clock/Clock'
import { AsyncStream } from '../../infrastructure/process/AsyncStream'
import type { Continuation } from '../../infrastructure/process/AsyncStream'
import { BoundedTail } from '../../infrastructure/process/BoundedTail'
import { ChildExit } from '../../infrastructure/process/ChildExit'
import type { CapturedRun, StandInChild } from '../../infrastructure/process/StandInChild'
import { LineFramer } from '../../infrastructure/process/LineFramer'

/** How a job ended: the Mac app's `JobEnd` (`SupervisedJob.swift:140-176`). */
export interface JobEnd {
  /** `undefined` when it never started. */
  readonly exit: ChildExit | undefined
  readonly stderrTail: BoundedTail
  /** Whether it was stopped by its Stop. */
  readonly interrupted: boolean
  /** Why it never started. */
  readonly detail: string | undefined
}

export const JobEnd = {
  of(exit: ChildExit | undefined, stderrTail: BoundedTail, interrupted: boolean, detail?: string): JobEnd {
    return { exit, stderrTail, interrupted, detail }
  },

  /** A run that never started, `detail` saying why. */
  notStarted(detail: string): JobEnd {
    return { exit: undefined, stderrTail: new BoundedTail(), interrupted: false, detail }
  },

  succeeded(end: JobEnd): boolean {
    return end.exit !== undefined && ChildExit.succeeded(end.exit)
  },
}

/** What a supervised job says: the Mac app's `JobUpdate` (`SupervisedJob.swift:129-138`). */
export type JobUpdate =
  /** An event of the 52b stream. */
  | { readonly case: 'event'; readonly event: JobEvent }
  /** A line the verb printed for people, on a build before the stream. */
  | { readonly case: 'said'; readonly line: string }
  /** stderr's last line changed: the busy line, for showing only. */
  | { readonly case: 'busy'; readonly line: string }
  /** It ended; always last. */
  | { readonly case: 'ended'; readonly end: JobEnd }

export const JobUpdate = {
  event: (event: JobEvent): JobUpdate => ({ case: 'event', event }),
  said: (line: string): JobUpdate => ({ case: 'said', line }),
  busy: (line: string): JobUpdate => ({ case: 'busy', line }),
  ended: (end: JobEnd): JobUpdate => ({ case: 'ended', end }),
}

/** A job as a screen sees it: the Mac app's `JobRunning` (`SupervisedJob.swift:186-194`). */
export interface JobRunning {
  /** What the job says, in order, `.ended` last. */
  readonly updates: AsyncStream<JobUpdate>
  /** Stops it as Ctrl-C does. `grace` is in milliseconds. */
  stop(grace?: number): Promise<void>
  /** Its child's process id, when it started. A replay has none. */
  readonly processIdentifier?: number
}

/** Where jobs come from: the Mac app's `JobStarting` (`SupervisedJob.swift:177-185`). */
export interface JobStarting {
  /** `streaming` says the arguments ask for the 52b stream, so stdout lines are events; `folder` is the run's working directory. */
  startJob(binary: string, args: readonly string[], streaming: boolean, folder?: string): JobRunning
}

/**
 * The prototype's job starter (design 1d): each job is its rule's recording,
 * replayed on the clock by a `SupervisedJob`. The Mac app's `JobStarter`
 * starts real children; this one starts none.
 */
export class JobStarter implements JobStarting {
  constructor(
    readonly child: StandInChild,
    readonly clock: Clock,
  ) {}

  startJob(_binary: string, args: readonly string[], streaming: boolean, _folder?: string): JobRunning {
    const answer = this.child.run('jobs', args)
    if (!answer.started) return SupervisedJob.notStarted(answer.reason)
    return SupervisedJob.replay(answer.run, streaming, this.clock, answer.replay.exitAfter)
  }
}

/**
 * A long run of the binary, as the Mac app's `SupervisedJob` says it: its
 * stdout lines -- events of the 52b stream, or the lines it prints for
 * people -- then `.ended` with its exit. Here the run is a recording, one
 * line a tick (or the preset's pace), its end on the tick after its last
 * line -- held back that many ticks more where its rule's `exitAfter` says
 * (design 1v), so a still frame can stand mid-job. Stop ends it at once, as
 * an interrupted run's stream ends (`PreviewJob`'s 130).
 */
export class SupervisedJob implements JobRunning {
  readonly updates: AsyncStream<JobUpdate>
  private readonly continuation: Continuation<JobUpdate>
  private readonly lines: string[]
  private next = 0
  /** Ticks still to wait after the last line before `.ended`: the rule's `exitAfter`. */
  private held: number
  private leave: () => void = () => {}

  private constructor(
    private readonly run: CapturedRun | undefined,
    private readonly streaming: boolean,
    exitAfter = 0,
  ) {
    this.held = exitAfter
    const { stream, continuation } = AsyncStream.make<JobUpdate>()
    this.updates = stream
    this.continuation = continuation
    this.lines = run ? LineFramer.lines(run.stdout) : []
  }

  /** A run that never started: it ends at once, `detail` saying why. */
  static notStarted(detail: string): SupervisedJob {
    const job = new SupervisedJob(undefined, false)
    job.end(JobEnd.notStarted(detail))
    return job
  }

  /** `run`, replayed on `clock`; in a still frame, as many ticks in as the frame, however late it opened. */
  static replay(run: CapturedRun, streaming: boolean, clock: Clock, exitAfter = 0): SupervisedJob {
    const job = new SupervisedJob(run, streaming, exitAfter)
    job.leave = clock.onTick(() => job.step(clock.linesPerTick))
    // After `leave` is set, so a recording shorter than the frame's ticks ends, and leaves, once.
    for (let tick = 0; tick < clock.missedTicks && !job.continuation.finished; tick += 1) job.step(clock.linesPerTick)
    return job
  }

  async stop(_grace?: number): Promise<void> {
    if (this.continuation.finished) return
    this.end(JobEnd.of(ChildExit.exited(130), new BoundedTail(), true))
  }

  private step(pace: number): void {
    if (this.next >= this.lines.length) {
      if (this.held > 0) {
        this.held -= 1
        return
      }
      if (this.run) this.end(JobEnd.of(this.run.exit, this.run.stderrTail, false))
      return
    }
    for (const line of this.lines.slice(this.next, this.next + pace)) {
      this.continuation.yield(this.streaming ? JobUpdate.event(JobEvent.decode(line)) : JobUpdate.said(line))
    }
    this.next += pace
  }

  private end(end: JobEnd): void {
    this.leave()
    this.continuation.yield(JobUpdate.ended(end))
    this.continuation.finish()
  }
}
