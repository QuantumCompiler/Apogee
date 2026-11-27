// The tools Claude is given: Apogee's served tools registered into the
// session by their own names, descriptions and schemas, from the listing the
// found binary's server gave; apogee_job beside them. A tool of the same
// prefix the mod did not register is passed on, never answered.
import { expect, test } from 'claude-code/testing'

import { apogee, callTool, served } from '../../support/apogee'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'
import { SERVED_LIST } from '../../support/recorded'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }

/** The recorded listing's tools, as the server gave them. */
const LISTED = (JSON.parse(SERVED_LIST.split('\n')[1] ?? '{}') as { result: { tools: { name: string; description: string; inputSchema: Record<string, unknown> }[] } }).result.tools

test('every served tool is registered by its own name, description and schema', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: served() })

  await $.session.start(START)

  const registered = made.tools.filter(tool => tool.name !== 'apogee_job' && tool.name !== 'apogee_complete')
  expect(registered).toEqual(LISTED.map(tool => ({ name: tool.name, description: tool.description, inputSchema: tool.inputSchema })))
  expect(registered.length).toBe(22)
})

test('apogee_job is registered beside them, to read a job by its id', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: served() })

  await $.session.start(START)

  expect(made.tools.find(tool => tool.name === 'apogee_job')).toEqual({
    name: 'apogee_job',
    description: expect.stringContaining('A job Apogee runs for you that outlasted its call'),
    inputSchema: {
      type: 'object',
      properties: { id: { type: 'string', description: 'The job\'s id, as the call that launched it said it (complete-1, promote-2)' } },
      required: ['id'],
    },
  })
})

test('a server that will not list registers none of its tools, and /apogee says why', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: args => (args.join(' ') === 'mcp serve' ? { exitCode: 1, stderr: 'apogee: unknown command mcp\n' } : undefined) })

  await $.session.start(START)

  expect(made.tools.map(tool => tool.name)).toEqual(['apogee_job'])
  expect((await apogee($)).text).toContain(
    `Apogee's served tools: none registered: ${BIN}/apogee mcp serve answered no listing: the server answered nothing to the request (exit status 1: apogee: unknown command mcp)`,
  )
})

test('/apogee says how many served tools Claude was given', async ($, on) => {
  machine(on, FOUND, undefined, { script: served() })
  await $.session.start(START)

  expect((await apogee($)).text).toContain("Apogee's served tools: 22 registered (mcp__apogee__<name>, each call one run of apogee mcp serve)")
})

test('with no binary there are no tools to call', async ($, on) => {
  const made = machine(on, {})
  await $.session.start(START)

  expect(made.tools).toEqual([])
})

test('a tool of the prefix the mod did not register is passed on to what is beneath', async ($, on) => {
  on('tool.run', { tool: 'mcp__apogee__someone_elses' }, () => ({ result: 'beneath answered' }))
  machine(on, FOUND, undefined, { script: served() })
  await $.session.start(START)

  expect(await callTool($, 'someone_elses')).toEqual({ result: 'beneath answered' })
})

test('the served complete is never registered under its own name: apogee_complete is its one surface', async ($, on) => {
  const withComplete = SERVED_LIST.replace(
    '"name":"check"}',
    '"name":"check"},{"annotations":{"readOnlyHint":false},"description":"Hand a prompt to one of Apogee\'s local models","inputSchema":{"type":"object"},"name":"complete"}',
  )
  const made = machine(on, FOUND, undefined, { script: served({}, withComplete) })

  await $.session.start(START)

  expect(made.tools.some(tool => tool.name === 'complete')).toBe(false)
})
