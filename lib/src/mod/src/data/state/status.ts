// The status line's reading: what the last read of `models status` and
// `train status` said, and when, and why the latest attempt failed if it did.
// One $.state value, declared in the contract (types/index.d.ts).
import { atom, read, update } from 'claude-code'
import type { EngineInterface } from 'claude-code'

import type { ApogeeStatusReading, ApogeeStatusState } from '../../../types'

// Change the tag when the value's fields change.
const STATUS = atom({ plugin: 'apogee', key: 'status' } as const, {}, { shape: 'status-1' })

/** Keeps a reading that succeeded at `at`, the failure before it cleared. */
export async function recordReading($: EngineInterface, reading: ApogeeStatusReading, at: number): Promise<ApogeeStatusState> {
  return keep($, () => ({ reading, readAt: at, attemptAt: at }))
}

/** Keeps a failed attempt at `at`, the last good reading kept beside it. */
export async function recordFailure($: EngineInterface, failure: string, at: number): Promise<ApogeeStatusState> {
  return keep($, state => ({ ...state, failure, attemptAt: at }))
}

/** The status as last kept. */
export async function readStatus($: EngineInterface): Promise<ApogeeStatusState> {
  return read($, STATUS)
}

async function keep($: EngineInterface, change: (state: ApogeeStatusState) => ApogeeStatusState): Promise<ApogeeStatusState> {
  let kept: ApogeeStatusState = {}
  await update($, STATUS, state => {
    kept = change(state)

    return kept
  })

  return kept
}
