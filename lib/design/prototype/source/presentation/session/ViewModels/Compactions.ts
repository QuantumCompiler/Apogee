import type { JobRunning, JobStarting } from '../../../business/jobs/SupervisedJob'
import { ChildExit } from '../../../infrastructure/process/ChildExit'
import { Observable } from '../../styles/Observation/Observable'

/** How a compaction stands: the Mac app's `JobProgress.Status`, the cases a compaction reaches. */
export type CompactionStatus = 'running' | 'done' | 'stopped' | { readonly failed: string }

/** One compaction's progress: what it said, and how it stands. */
export interface Compaction {
  readonly said: readonly string[]
  readonly status: CompactionStatus
}

/**
 * Saved chats compacting as jobs, side by side (GUI 6b; design 1v): the Mac
 * app's `compactions`, `isCompacting(_:)`, `stopCompacting(_:)` and
 * `canCompact`, which its `SessionLibraryViewModel` holds. Here a small class
 * of the session module, held by the Sessions place's library (1g)
 * (`sessions` may import `session`, never the reverse).
 *
 * A compaction is `chats compact <id> --yes` -- the CLI asks nothing on a pipe
 * without `--yes`, and refuses -- not streamed: its said lines, then its end.
 * Done, it says its last line; failed, the CLI's words; stopped, that it was.
 * Plain TypeScript over `JobStarting`, never the stand-in.
 */
export class Compactions extends Observable {
  /** Each chat compacting now, by id. */
  compactions: Readonly<Record<string, Compaction>> = {}
  /** Where it says how each ended: the toast. */
  onSay: ((text: string) => void) | undefined = undefined
  private readonly jobs = new Map<string, JobRunning>()

  constructor(
    private readonly starter: JobStarting,
    /** The binary the stand-in found; `undefined` when none was. */
    private readonly binary: () => string | undefined,
  ) {
    super()
  }

  /** Whether a compaction of `id` runs now, as a job: its row is busy. */
  isCompacting(id: string): boolean {
    return this.compactions[id] !== undefined
  }

  /** Whether `id` can be compacted from here: a binary, and none of it running. */
  canCompact(id: string): boolean {
    return this.binary() !== undefined && !this.isCompacting(id)
  }

  /** Compacts `id` as a job; resolves once it has ended and said how. */
  async compact(id: string): Promise<void> {
    const binary = this.binary()
    if (binary === undefined || this.isCompacting(id)) return
    const job = this.starter.startJob(binary, ['chats', 'compact', id, '--yes'], false)
    this.jobs.set(id, job)
    this.set(id, { said: [], status: 'running' })
    for await (const update of job.updates) {
      const progress = this.compactions[id] ?? { said: [], status: 'running' }
      if (update.case === 'said') this.set(id, { ...progress, said: [...progress.said, update.line] })
      if (update.case === 'ended') {
        const end = update.end
        const status: CompactionStatus = end.interrupted ? 'stopped' : end.exit !== undefined && ChildExit.succeeded(end.exit) ? 'done' : { failed: end.stderrTail.lastLine ?? end.detail ?? (end.exit ? ChildExit.description(end.exit) : 'it never started') }
        this.set(id, { ...progress, status })
      }
    }
    const progress = this.compactions[id]
    this.jobs.delete(id)
    const rest = { ...this.compactions }
    delete rest[id]
    this.compactions = rest
    this.changed()
    const status = progress?.status
    if (status === 'done') this.onSay?.(progress?.said.at(-1) ?? `Compacted ${id}`)
    else if (status === 'stopped') this.onSay?.('Not compacted: stopped')
    else if (typeof status === 'object') this.onSay?.(`Not compacted: ${status.failed}`)
    else this.onSay?.('Not compacted: it ended without saying how')
  }

  /** Stops `id`'s compaction as its job is stopped; the CLI saves a compaction only once it has finished. */
  async stopCompacting(id: string): Promise<void> {
    await this.jobs.get(id)?.stop()
  }

  private set(id: string, compaction: Compaction): void {
    this.compactions = { ...this.compactions, [id]: compaction }
    this.changed()
  }
}
