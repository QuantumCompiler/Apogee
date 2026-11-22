// The one thing the scaffold says: Apogee is not installed where the mod
// looks, every place passed over and why, with the install hint. Toasts are
// Presentation's (ADR 0001); whether to say it is Business's.
import type { EngineInterface } from 'claude-code'

import type { ApogeeBinary, ApogeeSkipped } from '../../../types'
import { findBinary } from '../../business/binary/binary'
import { DEV_INSTALL_HINT, INSTALL_HINT } from '../../infrastructure/discovery/discovery'
import type { DiscoveryOptions } from '../../infrastructure/discovery/discovery'

/** How long the absence stays up: long enough to read the install line. */
const ABSENCE_TOAST_MS = 10_000

/** Finds the binary for the session and, the first time none answers, says so; answers what was found. */
export async function findAndSayAbsence($: EngineInterface, options: DiscoveryOptions): Promise<ApogeeBinary> {
  const { binary, isAbsenceToSay } = await findBinary($, options)
  if (isAbsenceToSay && binary.kind === 'not-found') {
    $.ui.toast(absenceText(binary.skipped, options), { timeoutMs: ABSENCE_TOAST_MS })
  }

  return binary
}

/** The words: which binary is missing, where the mod looked and why each place failed, then how to install it. */
export function absenceText(skipped: readonly ApogeeSkipped[], options: DiscoveryOptions): string {
  const what = options.channel === 'dev' ? 'apogee-dev' : 'Apogee'
  const places = skipped.map(place => `${place.path} (${place.reason})`).join(', ')
  const hint = options.channel === 'dev' ? DEV_INSTALL_HINT : INSTALL_HINT

  return `${what} is not installed where the mod looks: ${places}. ${hint}`
}
