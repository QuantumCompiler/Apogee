// Apogee's MCP server, one exchange a run: the listing at the session's start
// and each call, the found binary's `mcp serve` fed the handshake and one
// request on its stdin, its replies read back (the CLI's own client's way).
import { expect, test } from 'claude-code/testing'

import { callTool, either, served, servedReply, SERVE, INITIALIZED } from '../../support/apogee'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'
import { SERVED_MODELS_STATUS_NO_CONFIG, TRAIN_STATUS_CYCLE } from '../../support/recorded'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }
const HANDSHAKE =
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"apogee-mod","version":"1"}}}\n' +
  '{"jsonrpc":"2.0","method":"notifications/initialized"}\n'

test('the session start lists the server once: mcp serve, the handshake and tools/list on its stdin', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: served() })

  await $.session.start(START)

  expect(made.runs.filter(run => run.argv.slice(1).join(' ') === SERVE)).toEqual([
    { argv: [`${BIN}/apogee`, 'mcp', 'serve'], init: { timeoutMs: 10_000, stdin: `${HANDSHAKE}{"jsonrpc":"2.0","id":2,"method":"tools/list"}\n` } },
  ])
})

test('a call runs mcp serve with the call on its stdin, the arguments as the model gave them, and answers the document', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: served({ train_status: { text: TRAIN_STATUS_CYCLE } }) })
  await $.session.start(START)

  const answered = await callTool($, 'train_status')

  expect(answered).toEqual({ result: TRAIN_STATUS_CYCLE })
  expect(made.runs.at(-1)).toEqual({
    argv: [`${BIN}/apogee`, 'mcp', 'serve'],
    init: {
      timeoutMs: 120_000,
      stdin: `${HANDSHAKE}{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"train_status","arguments":{}}}\n`,
    },
  })
})

test("the model's arguments reach the server whole, the call's own envelope left out", async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: served({ git_log: { text: 'abc123 first' } }) })
  await $.session.start(START)

  await callTool($, 'git_log', { n: 3, repo: '.', agentId: 'agent-1' })

  expect(made.runs.at(-1)?.init?.stdin).toContain('"params":{"name":"git_log","arguments":{"n":3,"repo":"."}}')
})

test("an error result is the call's error, in the server's own words", async ($, on) => {
  machine(on, FOUND, undefined, {
    script: either(served(), args => (args.join(' ') === SERVE ? { stdout: SERVED_MODELS_STATUS_NO_CONFIG } : undefined)),
  })
  await $.session.start(START)

  expect(await callTool($, 'models_status')).toEqual({
    deny: "Error: /Users/someone/.apogee/config/config.json: cannot open config file (run 'apogee config init' to create one)",
  })
})

test('a server that cannot run, or answers no reply, is the call\'s error with why', async ($, on) => {
  let call = 0
  machine(on, FOUND, undefined, {
    script: either(served(), args => {
      if (args.join(' ') !== SERVE) return undefined
      call += 1

      return call === 1 ? { refuses: 'timed out after 120000 ms' } : { exitCode: 1, stdout: INITIALIZED, stderr: 'error: config is broken\n' }
    }),
  })
  await $.session.start(START)

  expect(await callTool($, 'check')).toEqual({ deny: expect.stringMatching(/^Apogee's server \(.*apogee mcp serve\) could not run: .*timed out after 120000 ms/) })
  expect(await callTool($, 'check')).toEqual({
    deny: `Apogee's server (${BIN}/apogee mcp serve) answered no reply: the server answered nothing to the request (exit status 1: error: config is broken)`,
  })
})

test('a reply the server marks as no error is the result even when the run exits non-zero', async ($, on) => {
  machine(on, FOUND, undefined, {
    script: either(served(), args => (args.join(' ') === SERVE ? { exitCode: 1, stdout: INITIALIZED + servedReply('{"rows":[]}') } : undefined)),
  })
  await $.session.start(START)

  expect(await callTool($, 'check')).toEqual({ result: '{"rows":[]}' })
})
