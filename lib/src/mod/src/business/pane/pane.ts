// The pane's reads and decisions (1c). Its four documents are read when it
// opens, when the person asks, and after a confirmed command ends (read back,
// never mirrored); its training view is polled only while the pane is shown,
// and the poll ends with the pane. Business decides what the pane offers --
// which runs Promote takes, into which backends, which ledgers Roll back
// takes -- and Presentation draws it (ADR 0001).
import type { EngineInterface, Timer } from 'claude-code'

import type { ApogeeJob, ApogeeLedger, ApogeeModelsList, ApogeePaneState, ApogeeRunSummary } from '../../../types'
import type { Decoded } from '../../data/documents/document'
import { decodeModelsList, decodeModelsStatus } from '../../data/documents/models'
import { decodeTrainStatus } from '../../data/documents/training'
import { decodeTrainVersions } from '../../data/documents/versions'
import { readJobs } from '../../data/state/jobs'
import { readPaneState, recordPaneRead, recordPromoting } from '../../data/state/pane'
import type { PaneDocumentName, PaneValues } from '../../data/state/pane'
import { after, every, now } from '../../infrastructure/timer/timer'
import { failureOf, readDocument } from '../client/client'
import type { FoundBinary } from '../client/client'
import { READ_ARGS } from '../commands/plan'
import { MODELS_STATUS_ARGS } from '../status/status'

export type { PaneDocumentName } from '../../data/state/pane'

/** How often the training view is read while the pane is shown. */
export const PANE_POLL_MS = 15_000

/** `train versions`' arguments: the ledgers as a document. */
export const TRAIN_VERSIONS_ARGS: readonly string[] = ['train', 'versions', '--output-format', 'json']

/** Each document of the pane, the command it is read by. */
export const PANE_ARGS: Readonly<Record<PaneDocumentName, readonly string[]>> = {
  models: READ_ARGS.models,
  roles: MODELS_STATUS_ARGS,
  training: READ_ARGS['train status'],
  ledger: TRAIN_VERSIONS_ARGS,
}

/** Every document of the pane, in the order the pane reads them. */
export const EVERY_DOCUMENT: readonly PaneDocumentName[] = ['roles', 'models', 'training', 'ledger']

/** Where the pane stands with the person: shown, open but hidden (a tab behind another, undrawn), or closed. */
export type PaneVisibility = 'shown' | 'hidden' | 'closed'

/**
 * The read in flight, if any: every read of the pane waits for the one before
 * it, so two never run at once and the last begun is kept last (a read-back
 * after a command is never overwritten by a poll read begun before it wrote).
 */
let latest: Promise<unknown> = Promise.resolve()

/** Reads `names` again, after any read of the pane in flight, each kept as it lands. */
export function readPane($: EngineInterface, binary: FoundBinary, names: readonly PaneDocumentName[] = EVERY_DOCUMENT): Promise<void> {
  const next = latest.then(
    () => readEach($, binary, names),
    () => readEach($, binary, names),
  )
  latest = next.catch(() => undefined)

  return next
}

/** The read-back from work that outlived its call: not awaited; one that cannot finish (the module unloading) has nobody to tell. */
export function readPaneLater($: EngineInterface, binary: FoundBinary): void {
  readPane($, binary).catch(() => undefined)
}

async function readEach($: EngineInterface, binary: FoundBinary, names: readonly PaneDocumentName[]): Promise<void> {
  for (const name of names) {
    if (name === 'models') await readOne($, binary, name, decodeModelsList)
    else if (name === 'roles') await readOne($, binary, name, decodeModelsStatus)
    else if (name === 'training') await readOne($, binary, name, decodeTrainStatus)
    else await readOne($, binary, name, decodeTrainVersions)
  }
}

async function readOne<K extends PaneDocumentName>($: EngineInterface, binary: FoundBinary, name: K, decode: (text: string) => Decoded<PaneValues[K]>): Promise<void> {
  const read = await readDocument($, binary, PANE_ARGS[name], decode)
  const at = await now($)
  const failure = failureOf(binary, PANE_ARGS[name], read)
  await recordPaneRead($, name, read.kind === 'read' ? read : { kind: 'failed', failure: failure ?? 'the read did not answer' }, at)
}

/** This copy's poll: one at a time, so a pane opened again polls once. */
let polling: Timer | undefined

/**
 * Reads every document now, then the training view every PANE_POLL_MS while
 * `look` says the pane is shown: a tick while it is hidden reads nothing, and
 * one that finds it closed ends the poll. A read still running when the next
 * tick comes is not started twice; a poll this copy started before is ended.
 */
export function startPanePoll($: EngineInterface, binary: FoundBinary, look: () => Promise<PaneVisibility>): void {
  polling?.cancel()
  let isReading = false
  // Begun from a timer, outside the dispatch that opened the pane.
  const first = after($, 0, () => readPaneLater($, binary))
  const tick = async (): Promise<void> => {
    if (isReading) return
    const seen = await look()
    if (seen === 'closed') {
      poll.cancel()

      return
    }
    if (seen === 'hidden') return
    isReading = true
    try {
      await readPane($, binary, ['training'])
    } finally {
      isReading = false
    }
  }
  // A tick that cannot finish (the module unloading under it) has nobody to tell.
  const ticks = every($, PANE_POLL_MS, () => void tick().catch(() => undefined))
  const poll: Timer = {
    cancel: () => {
      first.cancel()
      ticks.cancel()
      if (polling === poll) polling = undefined
    },
  }
  polling = poll
}

/** What the pane draws: its state, the confirmed commands running now, and the time now. */
export type PaneShown = { state: ApogeePaneState; actions: ApogeeJob[]; at: number }

/** The pane's state as kept, with the promote and rollback jobs still running. */
export async function shownPane($: EngineInterface): Promise<PaneShown> {
  const state = await readPaneState($)
  const actions = (await readJobs($)).filter(job => job.state === 'running' && (job.kind === 'promote' || job.kind === 'rollback'))

  return { state, actions, at: await now($) }
}

/** Starts the pick of a backend to promote `run` into. */
export async function pickPromotion($: EngineInterface, run: string): Promise<void> {
  await recordPromoting($, run)
}

/** Ends the pick: cancelled, or a backend picked. */
export async function dropPromotion($: EngineInterface): Promise<void> {
  await recordPromoting($, undefined)
}

/** Whether Promote is offered on a run: one that ran to its end (`complete`); the CLI's gate decides the rest. */
export function isPromotable(run: ApogeeRunSummary): boolean {
  return run.status === 'complete'
}

/**
 * The backends a run may be promoted into, as `models list` names them: the
 * configured `llamacpp` and `mlx` entries (a promotion makes a GGUF for the
 * one, an MLX model for the other; training.md), each once, in its order.
 */
export function promoteTargets(models: ApogeeModelsList): string[] {
  const names = models.rows.filter(row => isConfigured(row.backend) && (row.backendType === 'llamacpp' || row.backendType === 'mlx')).map(row => row.backend)

  return [...new Set(names)]
}

/**
 * Whether Roll back is offered on a ledger: it lists a version below its
 * active one. The CLI's rule decides which (the highest below) and refuses in
 * its own words what else it must (a pruned version, a kind that differs).
 */
export function canRollBack(ledger: ApogeeLedger): boolean {
  const active = ledger.activeVersion

  return active !== undefined && ledger.versions.some(version => version.version !== undefined && version.version < active)
}

/** Whether a `models list` row names a configured backend, not a stored set no backend runs. */
export function isConfigured(backend: string): boolean {
  return backend !== '' && backend !== '(not configured)'
}
