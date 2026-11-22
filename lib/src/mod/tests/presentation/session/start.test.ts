// The session's start: the binary found first, its absence said once, then
// the surfaces registered, awaited before the first turn; the start is
// answered as the engine answered it.
import { expect, test } from 'claude-code/testing'

import { apogee, served, servedListWith } from '../../support/apogee'
import { SERVED_COMPLETE_TOOL_HANDMADE } from '../../support/handmade'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'

test('with no binary, /apogee is registered all the same, the absence said once and no tool registered', async ($, on) => {
  const made = machine(on, {})

  await $.session.start(START)

  expect(made.toasts.length).toBe(1)
  expect(made.commands.map(command => command.name)).toEqual(['apogee'])
  expect(made.tools).toEqual([])
})

test('with a binary, the start is answered as the engine answered it, the surfaces set up before it returns', async ($, on) => {
  const made = machine(on, { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } })

  expect(await $.session.start({ ...START, cwd: '/work' })).toEqual({ cwd: '/work' })
  expect(made.commands.map(command => command.name)).toEqual(['apogee'])
})

test('a registration the engine refuses is said once, and the session starts all the same', async ($, on) => {
  const made = machine(on, { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }, undefined, { script: served(), toolRefusal: 'tools are off here' })

  expect(await $.session.start(START)).toEqual({ cwd: '/' })
  expect(made.toasts.map(toast => toast.text)).toEqual([expect.stringMatching(/^Apogee's tools could not be registered: .*tools are off here/)])
  expect(made.commands.map(command => command.name)).toEqual(['apogee'])
})

test('tools the engine refused are never said as given: /apogee says none registered, and why', async ($, on) => {
  machine(on, { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }, undefined, {
    script: served({}, servedListWith(SERVED_COMPLETE_TOOL_HANDMADE)),
    toolRefusal: 'tools are off here',
  })
  await $.session.start(START)

  const { text } = await apogee($)

  expect(text).toMatch(/Apogee's served tools: none registered: the engine refused their registration: .*tools are off here/)
  expect(text).not.toContain('registered (mcp__apogee__')
  expect(text).toMatch(/apogee_complete: not offered: the engine refused its registration: .*tools are off here/)
})

test('a tool the engine refuses by name is said, and every tool after it is registered all the same', async ($, on) => {
  const made = machine(on, { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }, undefined, {
    script: served({}, servedListWith(SERVED_COMPLETE_TOOL_HANDMADE)),
    toolRefusals: { train_status: 'not this one', apogee_complete: 'nor this' },
  })
  await $.session.start(START)

  expect(made.tools.map(tool => tool.name)).not.toContain('train_status')
  expect(made.tools.map(tool => tool.name)).not.toContain('apogee_complete')
  expect(made.tools.map(tool => tool.name)).toContain('apogee_job')
  const given = made.tools.filter(tool => tool.name !== 'apogee_job')
  expect(given.length).toBe(21)
  const { text } = await apogee($)
  expect(text).toMatch(
    new RegExp(`Apogee's served tools: ${given.length} registered \\(mcp__apogee__<name>, each call one run of apogee mcp serve\\); the engine refused train_status: .*not this one`),
  )
  expect(text).toMatch(/apogee_complete: not offered: the engine refused its registration: .*nor this/)
  expect(made.toasts.map(toast => toast.text)).toEqual([expect.stringMatching(/^Apogee's tools could not all be registered \(2 of 24: train_status, apogee_complete\): .*not this one/)])
})

test('a session started again in the same copy polls the status line once, not twice', async ($, on) => {
  const made = machine(on, { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }, undefined, { script: served() })
  await $.session.start(START)
  await $.session.start(START)
  await made.clock.settle()
  const settled = made.runs.length

  await made.clock.advance(60_000)

  expect(made.runs.slice(settled).map(run => run.argv.slice(1, 3).join(' '))).toEqual(['models status'])
})
