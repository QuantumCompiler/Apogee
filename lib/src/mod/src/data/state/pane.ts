// The pane's state: each document it shows as last read (the value, what it
// lacked, when; why the latest read failed, the last good read kept beside
// it), and the run whose promotion target the person is picking. One $.state
// value, declared in the contract (types/index.d.ts): the pane's render hook
// reads it, so each write draws the pane again.
import { atom, read, update } from 'claude-code'
import type { EngineInterface } from 'claude-code'

import type { ApogeeModelsList, ApogeeModelsStatus, ApogeePaneRead, ApogeePaneState, ApogeeTrainStatus, ApogeeTrainVersions } from '../../../types'

// Change the tag when the value's fields change.
const PANE = atom({ plugin: 'apogee', key: 'pane' } as const, {}, { shape: 'pane-1' })

/** What each of the pane's documents holds once read. */
export type PaneValues = { models: ApogeeModelsList; roles: ApogeeModelsStatus; training: ApogeeTrainStatus; ledger: ApogeeTrainVersions }

/** The pane's documents, by the name the state keeps each under. */
export type PaneDocumentName = keyof PaneValues

/** One read of a document: its value and what it lacked, or why there is none. */
export type PaneRead<K extends PaneDocumentName> = { kind: 'read'; value: PaneValues[K]; missing: string[] } | { kind: 'failed'; failure: string }

/** Keeps one read of `name` made at `at`: a good read replaces what was kept; a failure is kept beside it. */
export async function recordPaneRead<K extends PaneDocumentName>($: EngineInterface, name: K, done: PaneRead<K>, at: number): Promise<void> {
  await update($, PANE, state => {
    const before = state[name] as ApogeePaneRead<PaneValues[K]> | undefined
    const kept: ApogeePaneRead<PaneValues[K]> =
      done.kind === 'read' ? { value: done.value, missing: done.missing, readAt: at, attemptAt: at } : { ...(before ?? { missing: [] }), failure: done.failure, attemptAt: at }

    return { ...state, [name]: kept }
  })
}

/** Keeps the run Promote was pressed on, or none once the pick is over. */
export async function recordPromoting($: EngineInterface, run: string | undefined): Promise<void> {
  await update($, PANE, state => {
    const { promoting: _, ...rest } = state

    return run === undefined ? rest : { ...rest, promoting: run }
  })
}

/** The pane's state as last kept. */
export async function readPaneState($: EngineInterface): Promise<ApogeePaneState> {
  return read($, PANE)
}
