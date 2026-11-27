// The async law (the mod spike's W4): anything long launches, says so and is
// polled; a turn is never blocked. A completion is given a window: work that
// ends inside it is answered there, within the turn; work that does not is
// kept as a job, answered at once with its id, and when it ends its outcome is
// kept for a poll and handed to `onLater` (a toast, a re-read). Work that
// writes the store is a job from its start, begun outside the dispatch that
// asked for it (`detachJob`): the API ends a spawn when that dispatch is
// abandoned (its `next.signal`), so nothing but its own end or the module's
// unload ends it.
import type { EngineInterface, Timer } from 'claude-code'

import type { ApogeeJob, ApogeeJobKind } from '../../../types'
import { addJob, endJob, markJobsLost, readJob } from '../../data/state/jobs'
import { after, now } from '../../infrastructure/timer/timer'

/** How long a call waits on its work before the work goes on as a job. */
export const ASYNC_WINDOW_MS = 30_000

/**
 * The jobs this copy of the mod launched: only their work can still end. A
 * copy the engine loads afresh starts with none, so a job an earlier copy
 * left running is known lost.
 */
const LAUNCHED_HERE = new Set<string>()

/** What launching came to: the work's outcome, inside the window; or the job it goes on as. */
export type Launched<T> = { kind: 'ended'; outcome: T } | { kind: 'running'; job: ApogeeJob }

/** How a job ended, as kept for its poll. */
export type JobEnd = { state: 'done' | 'failed'; text?: string; reason?: string }

/**
 * Waits up to the window for `work`; past it, keeps a job and answers it at
 * once, the work going on. `endOf` says how the outcome is kept; `onLater`
 * hears a job's end, never an outcome answered inside the window.
 */
export async function launchJob<T>(
  $: EngineInterface,
  spec: { kind: ApogeeJobKind; title: string },
  work: Promise<T>,
  endOf: (outcome: T) => JobEnd,
  onLater: (job: ApogeeJob, outcome: T) => void,
): Promise<Launched<T>> {
  const startedAt = await now($)
  let timer: Timer | undefined
  const window = new Promise<undefined>(resolve => {
    timer = after($, ASYNC_WINDOW_MS, () => resolve(undefined))
  })
  const first = await Promise.race([work.then(outcome => ({ outcome })), window])
  if (first !== undefined) {
    timer?.cancel()

    return { kind: 'ended', outcome: first.outcome }
  }
  const job = await addJob($, spec.kind, spec.title, startedAt)
  LAUNCHED_HERE.add(job.id)
  keepEnd($, job, work, endOf, onLater)

  return { kind: 'running', job }
}

/**
 * Keeps a job at once and answers it, then begins its work from a timer, so
 * the work belongs to no dispatch: one abandoned (the person interrupting,
 * its budget spent) aborts its `next.signal`, and the API ends a spawn begun
 * under it then. The work's end is kept and handed to `onLater`.
 */
export async function detachJob<T>(
  $: EngineInterface,
  spec: { kind: ApogeeJobKind; title: string },
  begin: () => Promise<T>,
  endOf: (outcome: T) => JobEnd,
  onLater: (job: ApogeeJob, outcome: T) => void,
): Promise<ApogeeJob> {
  const job = await addJob($, spec.kind, spec.title, await now($))
  LAUNCHED_HERE.add(job.id)
  after($, 0, () => keepEnd($, job, begin(), endOf, onLater))

  return job
}

/** Keeps `work`'s end on `job` when it comes, and hands it to `onLater`. */
function keepEnd<T>($: EngineInterface, job: ApogeeJob, work: Promise<T>, endOf: (outcome: T) => JobEnd, onLater: (job: ApogeeJob, outcome: T) => void): void {
  // An end that cannot be kept (the module unloading under it) has nobody to
  // tell; the next copy's session start says the job lost.
  work
    .then(async outcome => {
      const ended = await endJob($, job.id, { ...endOf(outcome), endedAt: await now($) })
      if (ended !== undefined) {
        onLater(ended, outcome)
      }
    })
    .catch(() => undefined)
}

/** Says lost each job left running by a copy of the mod that is gone: its end can never be heard. */
export async function forgetLostJobs($: EngineInterface): Promise<void> {
  await markJobsLost($, 'the mod was loaded again while it ran, so its end went unheard; run it again', LAUNCHED_HERE)
}

/** A job by its id, as kept: what a poll reads. */
export async function jobOf($: EngineInterface, id: string): Promise<ApogeeJob | undefined> {
  return readJob($, id)
}
