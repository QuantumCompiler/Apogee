// The binary the mod drives, found once a session: discovery's answer kept,
// and the decision to say its absence -- once a session, then quiet.
import type { EngineInterface } from 'claude-code'

import type { ApogeeBinary } from '../../../types'
import { markAbsenceSaid, recordBinary } from '../../data/state/binary'
import { locate } from '../../infrastructure/discovery/discovery'
import type { DiscoveryOptions } from '../../infrastructure/discovery/discovery'

/** What the session start learned: the binary, and whether its absence is to be said now. */
export type BinaryFound = { binary: ApogeeBinary; isAbsenceToSay: boolean }

/** Looks for the binary, keeps what was found, and decides whether its absence is said now. */
export async function findBinary($: EngineInterface, options: DiscoveryOptions): Promise<BinaryFound> {
  const binary = await locate($, options)
  await recordBinary($, binary)
  const isAbsenceToSay = binary.kind === 'not-found' && (await markAbsenceSaid($))

  return { binary, isAbsenceToSay }
}
