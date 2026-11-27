// The decision behind the guard: a missing binary is said once a session,
// then the mod stays quiet; a found binary is never announced.
import { expect, test } from 'claude-code/testing'

import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'
import { keptState } from '../../support/state'

test('a session with no binary says so once, however often the session starts', async ($, on) => {
  const kept = keptState(on)
  const made = machine(on, {})

  await $.session.start(START)
  await $.session.start(START)
  await $.session.start(START)

  expect(made.toasts.length).toBe(1)
  expect(kept.binary()?.isAbsenceSaid).toBe(true)
})

test('a found binary is kept and nothing is said', async ($, on) => {
  const kept = keptState(on)
  const made = machine(on, { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } })

  await $.session.start(START)

  expect(made.toasts).toEqual([])
  expect(kept.binary()).toEqual({
    isAbsenceSaid: false,
    binary: expect.objectContaining({ kind: 'found', source: 'release' }),
  })
})
