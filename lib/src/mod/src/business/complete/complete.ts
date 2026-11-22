// apogee_complete: one of Apogee's local models asked one prompt. Offered only
// for a backend that runs on this machine and bills nothing per call (honest
// about spend), at most 1024 tokens a call, or the backend's own max_tokens
// when that is lower (the bound 54b's served complete keeps, tools.md
// "Calling a local model"). Its plumbing is the server's own complete where
// the person's config lists it under a backend's mcp_serve:, else the
// one-shot `apogee complete -m <backend> "<prompt>"`; the surface is the same
// either way (the item's call: the served tool replaces the plumbing, not the
// surface). The one-shot can call a model beside its backend, which the
// served complete never does: the utility model, asked whether to think, and
// an active suite's members, asked to check the answer. So the CLI path is
// offered only where no suite is active and the utility model is unset or
// local and unmetered too (`spendRefusal`).
import type { EngineInterface } from 'claude-code'

import type { ApogeeCompleteBackend, ApogeeCompletePlumbing } from '../../../types'
import { decodeWholeValue } from '../../data/documents/config'
import { isFields } from '../../data/documents/document'
import type { ModelRow, ModelsList, ModelsStatus } from '../../data/documents/models'
import type { ServedTool } from '../../data/documents/served'
import { LONGEST_RUN_MS, lastLine, runBinary } from '../../infrastructure/runner/runner'
import { readDocument } from '../client/client'
import type { FoundBinary } from '../client/client'
import { callServed } from '../served/served'

/** The most tokens one call generates: the served complete's bound, kept on the CLI path too; a backend's own max_tokens lowers it. */
export const COMPLETE_MAX_TOKENS = 1024

/**
 * The backend types the CLI path offers: the ones that run their model on
 * this machine and bill nothing per call (tools.md: "a llama.cpp or MLX
 * model, today"). Ollama is left out: an Ollama model may run in its cloud.
 */
export const LOCAL_UNMETERED_TYPES: readonly string[] = ['llamacpp', 'mlx']

/**
 * The MLX kinds a backend chats with (machine-mode.md; the macOS app's
 * RowsThatChat): a row of another kind, a word not known yet included, cannot
 * complete. A row with no kind (llama.cpp, or a binary before the CLI's 53e)
 * chats.
 */
export const CHAT_KINDS: readonly string[] = ['chat', 'vision']

/** How apogee_complete is offered: its plumbing and the backends it may name. */
export type CompleteOffer = { plumbing: ApogeeCompletePlumbing; backends: ApogeeCompleteBackend[] }

/**
 * A request checked against the offer, or why it is refused before anything
 * runs. `maxTokens` is absent on the served path when the model asked for no
 * bound: the server then holds the backend to its own.
 */
export type CompleteRequest = { kind: 'request'; prompt: string; backend: string; maxTokens?: number } | { kind: 'refused'; reason: string }

/** How a completion ended: its reply, or why there is none. */
export type CompleteOutcome = { kind: 'answered'; text: string } | { kind: 'failed'; reason: string }

/**
 * The offer the server's own complete makes: the backends its schema names,
 * each at the bound the server holds it to. The schema's `maximum` is only the
 * widest of them (mcp_serve.cpp, served_complete); each backend's own is said
 * in the description, `<name> (at most N tokens)` (complete_description).
 */
export function servedOffer(tool: ServedTool): CompleteOffer | undefined {
  const properties = isFields(tool.inputSchema.properties) ? tool.inputSchema.properties : {}
  const backend = isFields(properties.backend) ? properties.backend : {}
  const maximum = isFields(properties.max_tokens) ? properties.max_tokens.maximum : undefined
  const widest = isBound(maximum) ? maximum : COMPLETE_MAX_TOKENS
  const names = Array.isArray(backend.enum) ? backend.enum.filter((name): name is string => typeof name === 'string' && name !== '') : []

  return names.length === 0 ? undefined : { plumbing: 'served', backends: names.map(name => servedBackend(tool.description, name, widest)) }
}

/**
 * One served backend at the bound the server's description says for it. A
 * backend it does not say is never assumed to take the widest: it is offered
 * at the widest the schema takes, its own bound said as unsaid, and the server
 * holds it to its own (a bound left out is never sent there).
 */
function servedBackend(description: string, name: string, widest: number): ApogeeCompleteBackend {
  const said = new RegExp(`(?:^|[\\s,])${escaped(name)} \\(at most (\\d+) tokens\\)`).exec(description)
  const bound = said === null ? undefined : Number(said[1])

  return isBound(bound) ? { name, maxTokens: Math.min(bound, widest) } : { name, maxTokens: widest, isBoundUnsaid: true }
}

/** Whether `value` is a bound: a whole number of tokens, one at least. */
function isBound(value: unknown): value is number {
  return typeof value === 'number' && Number.isInteger(value) && value >= 1
}

/** `text` with every character a pattern reads specially escaped. */
function escaped(text: string): string {
  return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
}

/** The offer of the CLI path: each configured backend of a local, unmetered type that chats, once. */
export function cliOffer(list: ModelsList): CompleteOffer | undefined {
  const names = list.rows
    .filter(row => isLocalUnmetered(row) && row.backend !== '' && !row.backend.startsWith('(') && (row.kind === undefined || CHAT_KINDS.includes(row.kind)))
    .map(row => row.backend)
  const backends = [...new Set(names)].map(name => ({ name, maxTokens: COMPLETE_MAX_TOKENS }))

  return backends.length === 0 ? undefined : { plumbing: 'cli', backends }
}

/**
 * A CLI backend's bound, by the served complete's rule (served_max_tokens,
 * lib/src/cli/source/data/contracts/mcp_serve.cpp): 1024, or the entry's own
 * max_tokens when that is lower. The one-shot's `-n` takes over the entry's
 * (run_settings.cpp, resolve_max_tokens), so passing 1024 to an entry of 200
 * would raise it.
 */
export function entryBound(entry: number | undefined): number {
  return entry !== undefined && Number.isInteger(entry) && entry >= 1 && entry < COMPLETE_MAX_TOKENS ? entry : COMPLETE_MAX_TOKENS
}

/** `config get`'s arguments for a backend entry's own max_tokens: a read, text out. */
export function entryMaxTokensArgs(backend: string): string[] {
  return ['config', 'get', `backends.${backend}.max_tokens`]
}

/**
 * The CLI path's offer with each backend at its own bound, its entry's
 * max_tokens read one backend at a time. An entry whose value cannot be read
 * keeps 1024, the most the mod ever passes.
 */
export async function withEntryBounds($: EngineInterface, binary: FoundBinary, offer: CompleteOffer): Promise<CompleteOffer> {
  const backends: ApogeeCompleteBackend[] = []
  for (const backend of offer.backends) {
    const read = await readDocument($, binary, entryMaxTokensArgs(backend.name), decodeWholeValue)
    backends.push({ name: backend.name, maxTokens: entryBound(read.kind === 'read' ? read.value : undefined) })
  }

  return { ...offer, backends }
}

/** Where the served complete runs, calling no model but its backend's: said with each refusal over spend. */
const SERVE_IT =
  "list complete under a local backend's mcp_serve (apogee config set-backend <backend> --mcp-serve complete) to serve it with no call but that backend's"

/**
 * Why the CLI path is withheld over spend, if it is: the one-shot can call a
 * model beside its backend, which the served complete never does
 * (mcp_serve.cpp: "the utility model off: no call but this backend's"), and no
 * flag of the CLI's stops either.
 * - An active suite (`models status`'s `suite`): the one-shot runs with its
 *   member calls (complete.cpp), where the suite's `validate:` may ask a
 *   member model to check the answer (27g), which may be billed.
 * - The utility model: the one-shot asks the model `models.utility` names
 *   whether to think, where a backend's thinking is `auto` (complete.cpp,
 *   one_shot: thinking_judge is the named utility). Offered only where it is
 *   unset (`(unset -- the chat's own backend)`, models.cpp) or local and
 *   unmetered by its row in `models list`; a status naming none cannot tell.
 */
export function spendRefusal(status: ModelsStatus, list: ModelsList): string | undefined {
  const suite = status.fields.find(field => field.field === 'suite')?.value.trim().split(/\s+/)[0] ?? ''
  if (suite !== '') {
    return `the suite ${suite} is active, and the one-shot apogee complete runs under it, where its validation may ask a member model that may be billed; ${SERVE_IT}`
  }
  const utility = status.fields.find(field => field.field === 'utility')?.value.trim() ?? ''
  if (utility === '') {
    return `models status names no utility model, so whether the one-shot apogee complete would ask a billed one cannot be told; ${SERVE_IT}`
  }
  if (utility.startsWith('(')) {
    return undefined
  }
  const name = utility.split(/\s+/)[0] ?? ''

  return list.rows.some(row => row.backend === name && isLocalUnmetered(row))
    ? undefined
    : `the utility model (${name}) is not local and unmetered, and the one-shot apogee complete asks it whether to think where a backend's thinking is auto, a call that may be billed; ${SERVE_IT}`
}

/** Why the CLI path is withheld where `models status` gave no document: `why` says what it did. */
export function unreadSpendRefusal(why: string): string {
  return `${why}, so whether the one-shot apogee complete would call a billed model beside its backend cannot be told; ${SERVE_IT}`
}

/** Whether a row's backend runs its model on this machine and bills nothing per call, by its type. */
function isLocalUnmetered(row: ModelRow): boolean {
  return row.section === 'local' && LOCAL_UNMETERED_TYPES.includes(row.backendType)
}

/**
 * The one-shot's arguments (lib/src/cli/source/presentation/cli/complete.cpp):
 * the backend, the bound, retrieval switched off as the served complete has it
 * (`--rag ""`), and the prompt after `--`, so a prompt is never read as a flag.
 */
export function completeArgs(backend: string, maxTokens: number, prompt: string): string[] {
  return ['complete', '-m', backend, '-n', String(maxTokens), '--rag', '', '--', prompt]
}

/** The model's arguments checked against the offer: nothing runs on a refusal. */
export function checkRequest(offer: CompleteOffer, args: Readonly<Record<string, unknown>>): CompleteRequest {
  const prompt = typeof args.prompt === 'string' ? args.prompt : ''
  if (prompt.trim() === '') {
    return { kind: 'refused', reason: 'prompt is required: what the model is asked' }
  }
  const named = typeof args.backend === 'string' ? args.backend : ''
  const backend = offer.backends.find(one => one.name === named)
  if (backend === undefined) {
    return { kind: 'refused', reason: `'${named}' is not a backend apogee_complete runs on here: it may name ${offer.backends.map(one => one.name).join(', ')}` }
  }
  const asked = args.max_tokens
  if (asked === undefined) {
    // The served complete holds the backend to its own bound when none is
    // sent; the one-shot is told the bound.
    return offer.plumbing === 'served' ? { kind: 'request', prompt, backend: backend.name } : { kind: 'request', prompt, backend: backend.name, maxTokens: backend.maxTokens }
  }
  if (typeof asked !== 'number' || !Number.isInteger(asked) || asked < 1 || asked > backend.maxTokens) {
    return { kind: 'refused', reason: `max_tokens must be a whole number from 1 to ${backend.maxTokens} for ${backend.name}` }
  }

  return { kind: 'request', prompt, backend: backend.name, maxTokens: asked }
}

/** Runs one completion by the offer's plumbing. */
export async function runComplete(
  $: EngineInterface,
  binary: FoundBinary,
  offer: CompleteOffer,
  request: Extract<CompleteRequest, { kind: 'request' }>,
): Promise<CompleteOutcome> {
  if (offer.plumbing === 'served') {
    // The served complete loads the backend's model at the call, then
    // generates: the one-shot's ten minutes, not a read tool's 120 s.
    const asked = request.maxTokens === undefined ? {} : { max_tokens: request.maxTokens }
    const called = await callServed($, binary, 'complete', { prompt: request.prompt, backend: request.backend, ...asked }, LONGEST_RUN_MS)
    if (called.kind === 'failed') {
      return { kind: 'failed', reason: `Apogee's server ${called.stage === 'run' ? 'could not run' : 'answered no reply'}: ${called.reason}` }
    }

    return called.isError ? { kind: 'failed', reason: called.text } : { kind: 'answered', text: called.text }
  }
  const bound = request.maxTokens ?? offer.backends.find(one => one.name === request.backend)?.maxTokens ?? COMPLETE_MAX_TOKENS
  const ran = await runBinary($, binary.path, completeArgs(request.backend, bound, request.prompt), { timeoutMs: LONGEST_RUN_MS })
  if (ran.kind === 'refused') {
    return { kind: 'failed', reason: `apogee complete on ${request.backend} could not run: ${ran.reason}` }
  }
  if (ran.exitCode !== 0) {
    return { kind: 'failed', reason: `apogee complete on ${request.backend} failed (exit status ${ran.exitCode}): ${lastLine(ran.stderr) ?? 'it said nothing'}` }
  }

  return { kind: 'answered', text: ran.stdout.trim() }
}
