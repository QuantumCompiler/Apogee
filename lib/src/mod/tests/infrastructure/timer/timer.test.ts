// Time and timers, over the kit's mocked clock: the async window is thirty
// seconds of the engine's clock, to the millisecond, and a window that the
// work beats is cancelled.
import { expect, test } from 'claude-code/testing'

import { callTool, cliPath } from '../../support/apogee'
import { COMPLETE_ANSWER_HANDMADE } from '../../support/handmade'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'
import type { Machine } from '../../support/machine'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }

test('the window ends at thirty seconds of the clock, not a millisecond before', async ($, on) => {
  let made: Machine | undefined
  let isAnswered = false
  made = machine(on, FOUND, undefined, {
    script: cliPath(async args => {
      if (args[0] !== 'complete') return undefined
      await (made as Machine).clock.sleep(60_000)

      return { stdout: COMPLETE_ANSWER_HANDMADE }
    }),
  })
  await $.session.start(START)

  const answer = callTool($, 'apogee_complete', { prompt: 'Hi', backend: 'q4' }).then(done => {
    isAnswered = true

    return done
  })
  await made.clock.advance(29_999)
  expect(isAnswered).toBe(false)
  await made.clock.advance(1)
  expect(isAnswered).toBe(true)
  expect(JSON.stringify(await answer)).toContain('it goes on as job complete-1')
})
