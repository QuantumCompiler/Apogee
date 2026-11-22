// The MCP server's frames (tools.md, "Apogee as an MCP server"): what the mod
// writes to `apogee mcp serve`'s stdin, and the replies read back from its
// stdout -- the recorded capture, read tolerantly.
import { expect, test } from 'claude-code/testing'

import { decodeServedCall, decodeServedList, servedCallInput, servedListInput } from '../../../src/data/documents/served'
import { SERVED_COMPLETE_ABSENT, SERVED_LIST, SERVED_MODELS_STATUS_NO_CONFIG, SERVED_TRAIN_STATUS } from '../../support/recorded'

const INITIALIZE =
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"apogee-mod","version":"1"}}}\n' +
  '{"jsonrpc":"2.0","method":"notifications/initialized"}\n'

test('the stdin of a listing: the handshake, then tools/list, one frame a line', () => {
  expect(servedListInput()).toBe(INITIALIZE + '{"jsonrpc":"2.0","id":2,"method":"tools/list"}\n')
})

test('the stdin of a call: the handshake, then tools/call with the arguments as given', () => {
  expect(servedCallInput('git_log', { n: 3, repo: '.' })).toBe(
    INITIALIZE + '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"git_log","arguments":{"n":3,"repo":"."}}}\n',
  )
})

test('the recorded listing reads as 22 tools, each with its description, schema and read-only mark', () => {
  const listed = decodeServedList(SERVED_LIST)

  expect(listed.kind).toBe('read')
  if (listed.kind !== 'read') return
  expect(listed.value.map(tool => tool.name)).toEqual([
    'collection_info',
    'git_diff',
    'git_log',
    'git_show',
    'git_status',
    'graph_explain',
    'graph_neighbors',
    'graph_path',
    'graph_query',
    'grep_files',
    'list_collections',
    'list_directory',
    'list_notes',
    'read_file',
    'read_note',
    'search_documents',
    'search_files',
    'models_list',
    'models_status',
    'train_status',
    'train_versions',
    'check',
  ])
  expect(listed.value.every(tool => tool.isReadOnly)).toBe(true)
  const status = listed.value.find(tool => tool.name === 'train_status')
  expect(status?.description).toStartWith("Apogee's training at a glance")
  expect(status?.inputSchema).toEqual({ type: 'object', properties: {} })
  expect(listed.value.find(tool => tool.name === 'read_file')?.inputSchema).toEqual(
    expect.objectContaining({ type: 'object', required: ['path'] }),
  )
  expect(listed.missing).toEqual([])
})

test('a listing tool missing its fields is kept with what it has; one with no name is left out', () => {
  const listed = decodeServedList('{"jsonrpc":"2.0","id":2,"result":{"tools":[{"name":"newer","extra":1},{"description":"nameless"}]}}\n')

  expect(listed).toEqual({
    kind: 'read',
    value: [{ name: 'newer', description: '', inputSchema: { type: 'object' }, isReadOnly: false }],
    missing: ['tools[0].description', 'tools[1].name'],
  })
})

test('a recorded call reply reads as its document, and an error result as an error', () => {
  expect(decodeServedCall(SERVED_TRAIN_STATUS)).toEqual({
    kind: 'read',
    value: {
      text: '{"active_pipeline":null,"cycle":null,"cycle_active":false,"cycle_error":null,"latest_pipeline":null,"pipelines":0,"recent_runs":[],"running":[],"runs":0,"stale_lock":null,"versions":[]}',
      isError: false,
    },
    missing: [],
  })
  expect(decodeServedCall(SERVED_COMPLETE_ABSENT)).toEqual({
    kind: 'read',
    value: { text: "Error: no such tool 'complete'", isError: true },
    missing: [],
  })
  expect(decodeServedCall(SERVED_MODELS_STATUS_NO_CONFIG)).toEqual(
    expect.objectContaining({ value: expect.objectContaining({ isError: true }) }),
  )
})

test('a server that answers no reply to the request, or an error frame, is unreadable with why', () => {
  expect(decodeServedCall('')).toEqual({ kind: 'unreadable', reason: 'the server answered nothing to the request' })
  expect(decodeServedList(SERVED_LIST.split('\n')[0] ?? '')).toEqual({ kind: 'unreadable', reason: 'the server answered nothing to the request' })
  expect(decodeServedList('{"jsonrpc":"2.0","id":2,"error":{"code":-32601,"message":"method not found: tools/list"}}\n')).toEqual({
    kind: 'unreadable',
    reason: 'the server refused the request: method not found: tools/list',
  })
  expect(decodeServedList('apogee: unknown command mcp\n{"id":2,"result":{"tools":[]}}\n')).toEqual({ kind: 'read', value: [], missing: [] })
})
