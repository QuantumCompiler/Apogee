// The binary as the session knows it: one $.state value, declared in the
// contract (types/index.d.ts). Atoms are Data's (ADR 0001).
import { atom, update } from 'claude-code'
import type { EngineInterface } from 'claude-code'

import type { ApogeeBinary } from '../../../types'

// Change the tag when the value's fields change: it declines what an earlier
// version stored, which outlives a reload.
const BINARY = atom({ plugin: 'apogee', key: 'binary' } as const, { isAbsenceSaid: false }, { shape: 'binary-1' })

/** Keeps what discovery found, leaving whether the absence was said as it was. */
export async function recordBinary($: EngineInterface, binary: ApogeeBinary): Promise<void> {
  await update($, BINARY, state => ({ ...state, binary }))
}

/**
 * Marks the not-installed condition said for the session, and answers whether
 * this call was the one that marked it: true once, false ever after.
 */
export async function markAbsenceSaid($: EngineInterface): Promise<boolean> {
  let isFirst = false
  await update($, BINARY, state => {
    isFirst = !state.isAbsenceSaid

    return { ...state, isAbsenceSaid: true }
  })

  return isFirst
}
