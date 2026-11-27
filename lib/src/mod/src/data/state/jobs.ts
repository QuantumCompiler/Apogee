// The long runs the mod launched and did not wait on (the async law): each
// job's state, kept so a later call reads its answer. One $.state value,
// declared in the contract (types/index.d.ts).
import { atom, read, update } from 'claude-code'
import type { EngineInterface } from 'claude-code'

import type { ApogeeJob, ApogeeJobKind } from '../../../types'

/** How many jobs are kept: the oldest ended ones leave first. */
export const JOBS_KEPT = 20

// Change the tag when the value's fields change.
const JOBS = atom({ plugin: 'apogee', key: 'jobs' } as const, { jobs: [], count: 0 }, { shape: 'jobs-1' })

/** Keeps a job launched now, running, and answers it with its id. */
export async function addJob($: EngineInterface, kind: ApogeeJobKind, title: string, startedAt: number): Promise<ApogeeJob> {
  let added: ApogeeJob | undefined
  await update($, JOBS, state => {
    const job: ApogeeJob = { id: `${kind}-${state.count + 1}`, kind, title, state: 'running', startedAt }
    added = job

    return { jobs: trimmed([...state.jobs, job]), count: state.count + 1 }
  })

  return added ?? { id: `${kind}-0`, kind, title, state: 'running', startedAt }
}

/** Marks a job ended with what it left; answers it as kept, or undefined when it is gone. */
export async function endJob(
  $: EngineInterface,
  id: string,
  ended: { state: 'done' | 'failed'; endedAt: number; text?: string; reason?: string },
): Promise<ApogeeJob | undefined> {
  let kept: ApogeeJob | undefined
  await update($, JOBS, state => ({
    ...state,
    jobs: state.jobs.map(job => {
      if (job.id !== id || job.state !== 'running') return job
      kept = { ...job, ...ended }

      return kept
    }),
  }))

  return kept
}

/** Marks each job still running but those `kept` lost, with why: the copy of the mod that ran it is gone. */
export async function markJobsLost($: EngineInterface, reason: string, kept: ReadonlySet<string>): Promise<void> {
  const isLost = (job: ApogeeJob): boolean => job.state === 'running' && !kept.has(job.id)
  await update($, JOBS, state =>
    state.jobs.some(isLost) ? { ...state, jobs: state.jobs.map((job): ApogeeJob => (isLost(job) ? { ...job, state: 'lost', reason } : job)) } : state,
  )
}

/** Every job kept, oldest first. */
export async function readJobs($: EngineInterface): Promise<ApogeeJob[]> {
  return (await read($, JOBS)).jobs
}

/** One job by its id, as kept. */
export async function readJob($: EngineInterface, id: string): Promise<ApogeeJob | undefined> {
  return (await read($, JOBS)).jobs.find(job => job.id === id)
}

/** The newest jobs kept, the running ones always among them. */
function trimmed(jobs: readonly ApogeeJob[]): ApogeeJob[] {
  let over = jobs.length - JOBS_KEPT

  return jobs.filter(job => {
    if (over > 0 && job.state !== 'running') {
      over -= 1

      return false
    }

    return true
  })
}
