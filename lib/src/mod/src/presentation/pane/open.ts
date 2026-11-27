// Opening the pane (`/apogee pane`, the person's command, so it is placed at
// any width) and where it stands with the person: what the training view's
// poll asks before each read, so the pane polls only while it is shown.
import type { EngineInterface } from 'claude-code'

import { sessionBinary } from '../../business/client/client'
import type { FoundBinary } from '../../business/client/client'
import { readPane, startPanePoll } from '../../business/pane/pane'
import type { PaneVisibility } from '../../business/pane/pane'

/** The pane's id: `$.ui.open`'s, and the `requestId` its render hook answers. */
export const PANE_ID = 'apogee'

/** Its tab's label while more than one pane is open. */
const PANE_TITLE = 'Apogee'

/** Opens the pane and, with a binary, reads its documents and starts its poll; the words say what became of it. */
export async function openPane($: EngineInterface): Promise<string> {
  let isPlaced: boolean
  let reason = ''
  try {
    const opened = await $.ui.open({ id: PANE_ID, title: PANE_TITLE })
    isPlaced = opened.isPlaced
    if (!opened.isPlaced) reason = opened.reason
  } catch (error) {
    return `Apogee's pane could not be opened: ${error instanceof Error ? error.message : String(error)}`
  }
  const binary = await sessionBinary($)
  if (binary?.kind === 'found') {
    startPanePoll($, binary, () => paneVisibility($))
  }

  return isPlaced ? "Apogee's pane is open." : `Apogee's pane is open, not drawn yet: ${reason}`
}

/**
 * At a session's start, the pane the engine kept open (a reload keeps a pane
 * up while its module's timers are gone): its documents read again and its
 * poll started. A pane that is not open is left alone; a look that fails is
 * nothing to say.
 */
export async function resumePane($: EngineInterface, binary: FoundBinary): Promise<void> {
  try {
    if ((await paneVisibility($)) !== 'closed') {
      startPanePoll($, binary, () => paneVisibility($))
    }
  } catch {
    // No pane to resume.
  }
}

/** Whether the pane is shown now (open, placed, the tab in front), open but hidden, or closed. */
export async function paneVisibility($: EngineInterface): Promise<PaneVisibility> {
  const pane = (await $.ui.panes()).find(one => one.id === PANE_ID)
  if (pane === undefined) return 'closed'

  return pane.isPlaced && pane.isShown ? 'shown' : 'hidden'
}

/**
 * The pane's read-back after a command that changed the store, however it
 * was confirmed (the pane's button or /apogee): its documents read again while
 * it is open. Not awaited; one that cannot finish has nobody to tell.
 */
export function readPaneBackLater($: EngineInterface, binary: FoundBinary): void {
  readPaneBack($, binary).catch(() => undefined)
}

async function readPaneBack($: EngineInterface, binary: FoundBinary): Promise<void> {
  if ((await paneVisibility($)) !== 'closed') {
    await readPane($, binary)
  }
}
