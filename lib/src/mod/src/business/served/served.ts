// Apogee's MCP server, one exchange a run (tools.md, "Apogee as an MCP
// server"): `apogee mcp serve` of the binary discovery found, the handshake
// and one request on its stdin, its replies read back. Each read the server
// serves runs at its call, so a call after a promotion sees the promotion.
import type { EngineInterface } from 'claude-code'

import { decodeServedCall, decodeServedList, servedCallInput, servedListInput } from '../../data/documents/served'
import type { ServedTool } from '../../data/documents/served'
import { lastLine, runBinary } from '../../infrastructure/runner/runner'
import type { Ran } from '../../infrastructure/runner/runner'
import type { FoundBinary } from '../client/client'

/** The server's command: the public spelling since the CLI's 54a. */
export const SERVE_ARGS: readonly string[] = ['mcp', 'serve']

/**
 * How long the listing may take: it is awaited at the session's start, before
 * the first prompt, and reads no store, so discovery's own ten seconds.
 */
export const SERVED_LIST_TIMEOUT_MS = 10_000

/**
 * How long one read tool's call may take: the CLI's own client's bound on an
 * exchange (mcp_serve_client.py), since a served search may embed its query.
 * A served completion is given the one-shot's own time instead (complete.ts).
 */
export const SERVED_CALL_TIMEOUT_MS = 120_000

/** What the listing came to: the tools, or why there are none. */
export type Listed = { kind: 'listed'; tools: ServedTool[]; missing: string[] } | { kind: 'failed'; stage: 'run' | 'reply'; reason: string }

/** What a call came to: the server's answer, an error result among them, or why there is none. */
export type Called = { kind: 'answered'; text: string; isError: boolean } | { kind: 'failed'; stage: 'run' | 'reply'; reason: string }

/** Lists the server's tools. */
export async function listServed($: EngineInterface, binary: FoundBinary): Promise<Listed> {
  const ran = await runBinary($, binary.path, SERVE_ARGS, { timeoutMs: SERVED_LIST_TIMEOUT_MS, stdin: servedListInput() })
  if (ran.kind === 'refused') {
    return { kind: 'failed', stage: 'run', reason: ran.reason }
  }
  const listed = decodeServedList(ran.stdout)

  return listed.kind === 'read' ? { kind: 'listed', tools: listed.value, missing: listed.missing } : { kind: 'failed', stage: 'reply', reason: `${listed.reason}${statusOf(ran)}` }
}

/** Calls one of the server's tools with the arguments as the model gave them, within `timeoutMs`. */
export async function callServed(
  $: EngineInterface,
  binary: FoundBinary,
  name: string,
  args: Readonly<Record<string, unknown>>,
  timeoutMs: number = SERVED_CALL_TIMEOUT_MS,
): Promise<Called> {
  const ran = await runBinary($, binary.path, SERVE_ARGS, { timeoutMs, stdin: servedCallInput(name, args) })
  if (ran.kind === 'refused') {
    return { kind: 'failed', stage: 'run', reason: ran.reason }
  }
  const reply = decodeServedCall(ran.stdout)

  return reply.kind === 'read' ? { kind: 'answered', ...reply.value } : { kind: 'failed', stage: 'reply', reason: `${reply.reason}${statusOf(ran)}` }
}

/** A run's exit status and last stderr line, said where it was not 0. */
function statusOf(ran: Extract<Ran, { kind: 'ran' }>): string {
  if (ran.exitCode === 0) return ''
  const said = lastLine(ran.stderr)

  return ` (exit status ${ran.exitCode}${said === undefined ? '' : `: ${said}`})`
}
