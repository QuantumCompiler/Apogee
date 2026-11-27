// The tools the session was given at its start: which of the server's tools
// were registered, and how apogee_complete is offered, or why not. One
// $.state value, declared in the contract (types/index.d.ts).
import { atom, read, update } from 'claude-code'
import type { EngineInterface } from 'claude-code'

import type { ApogeeToolsState } from '../../../types'

// Change the tag when the value's fields change.
const TOOLS = atom({ plugin: 'apogee', key: 'tools' } as const, { served: [] }, { shape: 'tools-2' })

/** Keeps what the session start offered. */
export async function recordTools($: EngineInterface, tools: ApogeeToolsState): Promise<void> {
  await update($, TOOLS, () => tools)
}

/** What the session start offered; nothing served before it has. */
export async function readTools($: EngineInterface): Promise<ApogeeToolsState> {
  return read($, TOOLS)
}
