// The jobs kept in $.state: a job launched by a copy of the mod that is gone
// (the engine replaced it mid-session) can never end, so the next session
// start says it lost rather than running forever; a job of this copy is left
// to end.
import { expect, test } from 'claude-code/testing'

import { callTool, either, scripted, served } from '../../support/apogee'
import { COMPLETE_ANSWER_HANDMADE } from '../../support/handmade'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'
import type { Machine } from '../../support/machine'
import { MODELS_LIST_ADAPTERS, MODELS_STATUS_CHAT } from '../../support/recorded'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }

test("a job an earlier copy left running is said lost at the session's start", async ($, on) => {
  // What an earlier copy of the mod kept, as the store holds it (version 0).
  let isFirstRead = true
  on('state.get', (_, e, next) => {
    if (isFirstRead && e.plugin === 'apogee' && e.key === 'jobs') {
      isFirstRead = false

      return {
        value: {
          value: { shape: 'jobs-1', value: { jobs: [{ id: 'complete-1', kind: 'complete', title: 'apogee_complete on q4', state: 'running', startedAt: 0 }], count: 1 } },
          version: 0,
        },
      }
    }

    return next(e)
  })
  machine(on, FOUND, undefined, { script: served() })
  await $.session.start(START)

  expect(await callTool($, 'apogee_job', { id: 'complete-1' })).toEqual({
    result: 'complete-1 was lost (apogee_complete on q4): the mod was loaded again while it ran, so its end went unheard; run it again',
  })
})

test("this copy's own running job is left running when the session starts again, and ends as it would", async ($, on) => {
  let made: Machine | undefined
  made = machine(on, FOUND, undefined, {
    script: either(
      served(),
      scripted({ 'models list --output-format json': { stdout: MODELS_LIST_ADAPTERS }, 'models status --output-format json': { stdout: MODELS_STATUS_CHAT } }),
      async args => {
        if (args[0] !== 'complete') return undefined
        await (made as Machine).clock.sleep(40_000)

        return { stdout: COMPLETE_ANSWER_HANDMADE }
      },
    ),
  })
  await $.session.start(START)
  const call = callTool($, 'apogee_complete', { prompt: 'Capital of France?', backend: 'q4' })
  await made.clock.advance(30_000)
  await call

  await $.session.start(START)
  await made.clock.advance(10_000)

  expect(await callTool($, 'apogee_job', { id: 'complete-1' })).toEqual({ result: 'The capital of France is Paris.' })
})
