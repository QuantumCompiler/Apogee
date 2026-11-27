// What the session is offered, decided once at its start over the found
// binary: the server's tools (all but `complete`, whose one surface is
// apogee_complete), and apogee_complete itself, by the server's complete where
// the person's config lists it, else on the CLI path for the local, unmetered
// backends that chat `models list` shows, where `models status` names no
// active suite and a utility model that bills nothing, each at its entry's
// own bound. What was registered
// is kept in $.state once the engine took it, so each later call knows what
// is the mod's to answer, and /apogee says what Claude was given.
import type { EngineInterface } from 'claude-code'

import type { ApogeeToolsState } from '../../../types'
import { decodeModelsList, decodeModelsStatus } from '../../data/documents/models'
import type { ServedTool } from '../../data/documents/served'
import { readTools, recordTools } from '../../data/state/tools'
import { readDocument } from '../client/client'
import type { FoundBinary } from '../client/client'
import { READ_ARGS, shownCommand } from '../commands/plan'
import { cliOffer, servedOffer, spendRefusal, unreadSpendRefusal, withEntryBounds } from '../complete/complete'
import type { CompleteOffer } from '../complete/complete'
import { SERVE_ARGS, listServed } from '../served/served'
import { MODELS_STATUS_ARGS } from '../status/status'

/** The served tool `apogee_complete` stands for: never registered under its own name. */
export const SERVED_COMPLETE = 'complete'

/** What the session start offers: the served tools to register, apogee_complete's offer, and the state to keep of them as offered. */
export type Offered = { served: ServedTool[]; complete?: CompleteOffer; state: ApogeeToolsState }

/** What registering came to: the served tools the engine took, and its first refusal of the served tools and of apogee_complete. */
export type Registered = { served: string[]; servedRefusal?: string; completeRefusal?: string }

/** Decides what the session is offered over `binary`; Presentation registers it, then keeps what took (`keepRegistered`). */
export async function offerSurfaces($: EngineInterface, binary: FoundBinary): Promise<Offered> {
  const listed = await listServed($, binary)
  const shown = shownCommand(binary.path, SERVE_ARGS)
  const tools = listed.kind === 'listed' ? listed.tools : []
  const served = tools.filter(tool => tool.name !== SERVED_COMPLETE)
  const offered = await offerComplete($, binary, tools.find(tool => tool.name === SERVED_COMPLETE))
  const state: ApogeeToolsState = {
    ...(listed.kind === 'listed'
      ? { served: served.map(tool => tool.name) }
      : { served: [], servedRefusal: `${shown} ${listed.stage === 'run' ? 'could not run' : 'answered no listing'}: ${listed.reason}` }),
    ...('refusal' in offered ? { completeRefusal: offered.refusal } : { complete: offered }),
  }

  return 'refusal' in offered ? { served, state } : { served, complete: offered, state }
}

/**
 * Keeps what the engine took of the offer, once registering is done: the
 * served tools it registered, and apogee_complete only where its registration
 * took, each refusal said in the engine's words, so /apogee never says a tool
 * Claude was not given.
 */
export async function keepRegistered($: EngineInterface, offered: Offered, registered: Registered): Promise<ApogeeToolsState> {
  const refused = offered.served.map(tool => tool.name).filter(name => !registered.served.includes(name))
  const servedRefusal =
    offered.state.servedRefusal ??
    (refused.length === 0
      ? undefined
      : `the engine refused ${registered.served.length === 0 ? 'their registration' : refused.join(', ')}: ${registered.servedRefusal ?? 'it gave no reason'}`)
  const complete =
    offered.complete === undefined
      ? { completeRefusal: offered.state.completeRefusal ?? 'not looked at yet' }
      : registered.completeRefusal === undefined
        ? { complete: offered.complete }
        : { completeRefusal: `the engine refused its registration: ${registered.completeRefusal}` }
  const state: ApogeeToolsState = { served: [...registered.served], ...(servedRefusal === undefined ? {} : { servedRefusal }), ...complete }
  await recordTools($, state)

  return state
}

/** apogee_complete's offer: the served complete's, where it is listed; else the CLI path's. */
async function offerComplete($: EngineInterface, binary: FoundBinary, served: ServedTool | undefined): Promise<CompleteOffer | { refusal: string }> {
  if (served !== undefined) {
    return servedOffer(served) ?? { refusal: "the server's complete names no backend it may run" }
  }
  const read = await readDocument($, binary, READ_ARGS.models, decodeModelsList)
  if (read.kind === 'failed') {
    return { refusal: `${shownCommand(binary.path, READ_ARGS.models)} ${read.stage === 'run' ? 'could not run' : 'answered no document'}: ${read.reason}` }
  }
  const offer = cliOffer(read.value)
  if (offer === undefined) {
    return {
      refusal: 'no backend here is local and unmetered and chats (a configured llama.cpp backend, or an MLX one whose kind is chat or vision, by apogee models list)',
    }
  }
  // The one-shot may call a model beside its backend: offered only where none of those bills.
  const status = await readDocument($, binary, MODELS_STATUS_ARGS, decodeModelsStatus)
  if (status.kind === 'failed') {
    return { refusal: unreadSpendRefusal(`${shownCommand(binary.path, MODELS_STATUS_ARGS)} ${status.stage === 'run' ? 'could not run' : 'answered no document'}: ${status.reason}`) }
  }
  const withheld = spendRefusal(status.value, read.value)

  return withheld === undefined ? withEntryBounds($, binary, offer) : { refusal: withheld }
}

/** What the session was offered, as kept. */
export async function offeredTools($: EngineInterface): Promise<ApogeeToolsState> {
  return readTools($)
}
