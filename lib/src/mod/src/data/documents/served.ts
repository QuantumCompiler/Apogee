// Apogee's MCP server as documents (tools.md, "Apogee as an MCP server"):
// `apogee mcp serve` speaks JSON-RPC, one frame a line, over its stdin and
// stdout, and exits at its stdin's end. So one exchange is one run: the
// handshake and one request written whole, the pipe closed, the replies read
// back -- as the CLI's own client does (lib/src/cli/tests/scripts/py/
// mcp_serve_client.py). stdout carries the protocol and nothing else; a line
// that is no frame is skipped, never fatal.
import { isFields, readerOf } from './document'
import type { Decoded, Fields } from './document'

/** The protocol version the server speaks (lib/src/cli/source/business/mcp/types.h). */
export const PROTOCOL_VERSION = '2025-03-26'

/** The request's id: the handshake is 1. */
const REQUEST_ID = 2

/** One tool the server lists: its own name, description and input schema. */
export type ServedTool = {
  name: string
  description: string
  /** The JSON schema of its arguments, as the server gave it; `{ type: "object" }` when it gave none. */
  inputSchema: Record<string, unknown>
  /** The server's `readOnlyHint`: every tool but `complete` is read-only. */
  isReadOnly: boolean
}

/** What a call answered: its text blocks joined, and whether it is an error result. */
export type ServedReply = { text: string; isError: boolean }

/** The stdin of one listing: the handshake, then `tools/list`. */
export function servedListInput(): string {
  return frames({ jsonrpc: '2.0', id: REQUEST_ID, method: 'tools/list' })
}

/** The stdin of one call: the handshake, then `tools/call` of `name` with `args`. */
export function servedCallInput(name: string, args: Readonly<Record<string, unknown>>): string {
  return frames({ jsonrpc: '2.0', id: REQUEST_ID, method: 'tools/call', params: { name, arguments: args } })
}

/** The tools a listing's stdout names, read tolerantly; a tool with no name is left out, said. */
export function decodeServedList(stdout: string): Decoded<ServedTool[]> {
  const result = resultOf(stdout)
  if (result.kind === 'unreadable') {
    return result
  }
  const missing: string[] = []
  const tools = readerOf(result.fields, '', missing).list('tools', tool => {
    const name = tool.text('name')
    if (name === '') {
      return undefined
    }
    const description = tool.text('description')

    return { name, description, inputSchema: schemaOf(tool.raw('inputSchema')), isReadOnly: readOnlyOf(tool.raw('annotations')) }
  })

  return { kind: 'read', value: tools.filter(tool => tool !== undefined), missing }
}

/** What a call's stdout answered, read tolerantly. */
export function decodeServedCall(stdout: string): Decoded<ServedReply> {
  const result = resultOf(stdout)
  if (result.kind === 'unreadable') {
    return result
  }
  const missing: string[] = []
  const reader = readerOf(result.fields, '', missing)
  const text = reader
    .list('content', block => (block.optionalText('type') === 'text' ? block.text('text') : ''))
    .filter(one => one !== '')
    .join('\n')
  const isError = reader.optionalFlag('isError') ?? false

  return { kind: 'read', value: { text, isError }, missing }
}

function frames(request: Readonly<Record<string, unknown>>): string {
  const handshake = [
    {
      jsonrpc: '2.0',
      id: 1,
      method: 'initialize',
      params: { protocolVersion: PROTOCOL_VERSION, capabilities: {}, clientInfo: { name: 'apogee-mod', version: '1' } },
    },
    { jsonrpc: '2.0', method: 'notifications/initialized' },
  ]

  return [...handshake, request].map(frame => `${JSON.stringify(frame)}\n`).join('')
}

/** The `result` of the reply to the request, or why there is none. */
function resultOf(stdout: string): { kind: 'result'; fields: Fields } | { kind: 'unreadable'; reason: string } {
  for (const line of stdout.split('\n')) {
    let frame: unknown
    try {
      frame = JSON.parse(line)
    } catch {
      continue
    }
    if (!isFields(frame) || frame.id !== REQUEST_ID) {
      continue
    }
    if (isFields(frame.error)) {
      const message = typeof frame.error.message === 'string' ? frame.error.message : 'no message'

      return { kind: 'unreadable', reason: `the server refused the request: ${message}` }
    }
    if (isFields(frame.result)) {
      return { kind: 'result', fields: frame.result }
    }
  }

  return { kind: 'unreadable', reason: 'the server answered nothing to the request' }
}

function schemaOf(value: unknown): Record<string, unknown> {
  return isFields(value) ? { ...value } : { type: 'object' }
}

function readOnlyOf(value: unknown): boolean {
  return isFields(value) && value.readOnlyHint === true
}
