// The binary's $.state value: kept under its shape tag, so what an earlier
// build of the mod stored under another shape is declined, never misread.
import { expect, test } from 'claude-code/testing'

import { START, machine } from '../../support/machine'
import { keptState } from '../../support/state'

test('the value is written under its shape tag, discovery and the absence together', async ($, on) => {
  const kept = keptState(on)
  machine(on, {})

  await $.session.start(START)

  expect(kept.binary()).toEqual({ binary: expect.objectContaining({ kind: 'not-found' }), isAbsenceSaid: true })
  expect(kept.writes()).toBe(2)
})

test('what an earlier shape of the value stored is declined, so the absence is still said', async ($, on) => {
  // A value without this version's shape tag, as an earlier build kept it.
  // It stands at the version the kit's store is at (0, nothing written yet),
  // so the write over it lands and the planted value is what the atom read:
  // at any other version the write misses, and the retry reads past the plant.
  let isFirstRead = true
  on('state.get', (_, e, next) => {
    if (isFirstRead && e.plugin === 'apogee') {
      isFirstRead = false

      return { value: { value: { isAbsenceSaid: true }, version: 0 } }
    }

    return next(e)
  })
  const made = machine(on, {})

  await $.session.start(START)

  expect(made.toasts.length).toBe(1)
})
