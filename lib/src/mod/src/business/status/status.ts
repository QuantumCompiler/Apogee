// The status line's reading: `models status` says which backend the chat
// role resolves to, `train status` that backend's promoted version and what
// runs. Read at the session's start, then once a minute (the item's recorded
// default, conservative: two read-only runs a minute), never two at once, and
// again straight after a confirmed command ends (read back, never mirrored).
import type { EngineInterface, Timer } from 'claude-code'

import type { ApogeeStatusReading, ApogeeStatusState } from '../../../types'
import { decodeModelsStatus } from '../../data/documents/models'
import type { ModelsStatus } from '../../data/documents/models'
import { decodeTrainStatus } from '../../data/documents/training'
import type { TrainStatus } from '../../data/documents/training'
import { readStatus, recordFailure, recordReading } from '../../data/state/status'
import { after, every, now } from '../../infrastructure/timer/timer'
import { readDocument } from '../client/client'
import type { FoundBinary, Read } from '../client/client'
import { READ_ARGS, shownCommand } from '../commands/plan'

/** How often the line is read: once a minute. */
export const POLL_MS = 60_000

/** A reading older than this is stale even when no read has failed: two polls missed. */
export const STALE_AFTER_MS = 2 * POLL_MS

/** `models status`'s arguments. */
export const MODELS_STATUS_ARGS: readonly string[] = ['models', 'status', '--output-format', 'json']

/** What the line shows: the status as kept, and the time it is shown at. */
export type StatusShown = { state: ApogeeStatusState; at: number }

/**
 * The read in flight, if any: every read waits for the one before it, so two
 * never run at once and the last begun is kept last (a read-back after a
 * command is never overwritten by a poll's read begun before the command wrote).
 */
let latest: Promise<unknown> = Promise.resolve()

/** Reads the line once, after any read in flight, and keeps what it read, or why it could not. */
export function refreshStatus($: EngineInterface, binary: FoundBinary): Promise<StatusShown> {
  const next = latest.then(
    () => readOnce($, binary),
    () => readOnce($, binary),
  )
  latest = next.catch(() => undefined)

  return next
}

/** One read of the line: `models status`, then `train status`, kept. */
async function readOnce($: EngineInterface, binary: FoundBinary): Promise<StatusShown> {
  const models = await readDocument($, binary, MODELS_STATUS_ARGS, decodeModelsStatus)
  const training = models.kind === 'read' ? await readDocument($, binary, READ_ARGS['train status'], decodeTrainStatus) : undefined
  const at = await now($)
  const failure = failureOf(binary, MODELS_STATUS_ARGS, models) ?? (training === undefined ? undefined : failureOf(binary, READ_ARGS['train status'], training))
  if (failure !== undefined || models.kind !== 'read' || training?.kind !== 'read') {
    return { state: await recordFailure($, failure ?? 'the reads did not answer', at), at }
  }

  return { state: await recordReading($, readingOf(models.value, models.missing, training.value, training.missing), at), at }
}

/** This copy's poll: one at a time, so a session started again in the same copy polls once. */
let polling: Timer | undefined

/**
 * Starts the line's reads: one now, then once a minute, each kept and handed
 * to `show`. A read still running when the next comes is not started twice,
 * and a poll this copy started before is stopped first.
 */
export function startStatusPoll($: EngineInterface, binary: FoundBinary, show: (shown: StatusShown) => void): Timer {
  polling?.cancel()
  let isReading = false
  const tick = async (): Promise<void> => {
    if (isReading) return
    isReading = true
    try {
      show(await refreshStatus($, binary))
    } finally {
      isReading = false
    }
  }
  // A tick that cannot finish (the module unloading under it) has nobody to tell.
  const first = after($, 0, () => void tick().catch(() => undefined))
  const minutely = every($, POLL_MS, () => void tick().catch(() => undefined))
  polling = {
    cancel: () => {
      first.cancel()
      minutely.cancel()
    },
  }

  return polling
}

/** The status as last kept, at the time now. */
export async function keptStatus($: EngineInterface): Promise<StatusShown> {
  return { state: await readStatus($), at: await now($) }
}

/** The reading the two documents make. */
export function readingOf(models: ModelsStatus, modelsMissing: readonly string[], training: TrainStatus, trainingMissing: readonly string[]): ApogeeStatusReading {
  const missing = [...modelsMissing.map(field => `models status ${field}`), ...trainingMissing.map(field => `train status ${field}`)]
  const chat = models.fields.find(field => field.field === 'chat')
  if (chat === undefined) {
    missing.unshift('models status chat')
  }
  const backend: ApogeeStatusReading['backend'] =
    chat === undefined ? undefined : chat.value.trim() === '' || chat.value.trim().startsWith('(') ? { kind: 'unset' } : { kind: 'set', name: chat.value.trim().split(/\s+/)[0] ?? '' }
  const version = backend?.kind === 'set' ? training.versions.find(one => one.backend === backend.name)?.activeVersion : undefined
  const run = trainingMissing.includes('running') ? undefined : runOf(training)

  return {
    ...(backend === undefined ? {} : { backend }),
    ...(version === undefined ? {} : { version }),
    ...(run === undefined ? {} : { run }),
    missing,
  }
}

/** What runs, said: the runs, the pipeline a live process runs, a cycle pass; else idle. */
function runOf(training: TrainStatus): string {
  const parts: string[] = []
  if (training.running.length > 0) parts.push(`${training.running.length} run${training.running.length === 1 ? '' : 's'}`)
  if (training.activePipeline !== undefined) parts.push(`pipeline ${training.latestPipeline?.id === training.activePipeline ? (training.latestPipeline.specName ?? training.activePipeline) : training.activePipeline}`)
  if (training.isCycleActive === true) parts.push('cycle pass')

  return parts.length === 0 ? 'idle' : `training: ${parts.join(', ')}`
}

function failureOf<T>(binary: FoundBinary, args: readonly string[], read: Read<T>): string | undefined {
  if (read.kind === 'read') return undefined

  return `${shownCommand(binary.path, args)} ${read.stage === 'run' ? 'could not run' : 'answered no document'}: ${read.reason}`
}
