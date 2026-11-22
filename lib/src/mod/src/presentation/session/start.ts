// What the session's start sets up, in order: the binary found (its absence
// said once), then the mod's surfaces registered over it, all awaited so they
// stand by the first turn.
import type { EngineInterface } from 'claude-code'

import { forgetLostJobs } from '../../business/jobs/jobs'
import { offerSurfaces } from '../../business/surfaces/surfaces'
import type { DiscoveryOptions } from '../../infrastructure/discovery/discovery'
import { registerApogee } from '../commands/apogee'
import { findAndSayAbsence } from '../notices/absence'
import { startStatusLine } from '../status/line'
import { registerTools } from '../tools/tools'

/** How long a refused registration stays said. */
const REFUSAL_TOAST_MS = 10_000

/**
 * Finds the binary and sets up the mod's surfaces for the session. A
 * registration the engine refuses is said once and the rest go on: the
 * session starts whatever the mod meets.
 */
export async function startSession($: EngineInterface, discovery: DiscoveryOptions): Promise<void> {
  const binary = await findAndSayAbsence($, discovery)
  await forgetLostJobs($)
  try {
    await registerApogee($)
  } catch (error) {
    $.ui.toast(`/apogee could not be registered: ${reasonOf(error)}`, { timeoutMs: REFUSAL_TOAST_MS })
  }
  if (binary.kind !== 'found') {
    return
  }
  try {
    const refused = await registerTools($, await offerSurfaces($, binary))
    if (refused !== undefined) {
      const which = refused.names.length === refused.total ? '' : ` all be registered (${refused.names.length} of ${refused.total}: ${refused.names.join(', ')})`
      $.ui.toast(`Apogee's tools could not${which === '' ? ' be registered' : which}: ${refused.reason}`, { timeoutMs: REFUSAL_TOAST_MS })
    }
  } catch (error) {
    $.ui.toast(`Apogee's tools could not be registered: ${reasonOf(error)}`, { timeoutMs: REFUSAL_TOAST_MS })
  }
  startStatusLine($, binary)
}

function reasonOf(error: unknown): string {
  return error instanceof Error ? error.message : String(error)
}
