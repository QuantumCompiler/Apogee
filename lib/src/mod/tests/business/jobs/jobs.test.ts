// The async law on the kit's mocked clock: a completion that ends inside the
// window is answered there; one that outlasts it is answered at once as a
// job, goes on, and its end is said by a toast and kept for a poll. A
// confirmed promote or rollback is a job from its start, its run begun
// outside the command's dispatch: the API ends a spawn when the dispatch that
// started it is abandoned (the person interrupting), and a run that writes
// the store must never be cut off.
import { expect, test } from 'claude-code/testing'

import { apogee, callTool, cliPath, scripted } from '../../support/apogee'
import { COMPLETE_ANSWER_HANDMADE, ROLLBACK_TUNED_HANDMADE } from '../../support/handmade'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'
import type { Machine } from '../../support/machine'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }
const PROMOTE = `${BIN}/apogee train promote r1 --as q4`

/** A promote that takes `ms` of the mocked clock to end, then prints its line. */
function slowPromote(made: () => Machine, ms: number): Parameters<typeof machine>[3] {
  return {
    script: async args => {
      if (args.join(' ') !== 'train promote r1 --as q4') return undefined
      await made().clock.sleep(ms)

      return { stdout: 'updated backend q4 -> v2\n' }
    },
    ask: () => 'Run it',
  }
}

/** The supervised runs made: promote and rollback. */
function changes(made: Machine): string[][] {
  return made.runs.filter(run => run.isSpawned === true).map(run => run.argv.slice(1))
}

test('a confirmed promote is answered at once as a job, and its end, however late, is toasted', async ($, on) => {
  let made: Machine | undefined
  made = machine(on, FOUND, undefined, slowPromote(() => made as Machine, 45_000))
  await $.session.start(START)

  expect((await apogee($, 'promote r1 --as q4')).text).toBe(`${PROMOTE} is running as job promote-1; a toast says when it ends.`)
  await made.clock.advance(44_999)
  expect(made.toasts).toEqual([])

  await made.clock.advance(1)

  expect(made.toasts).toEqual([{ text: `Apogee job promote-1: Ran ${PROMOTE}:\nupdated backend q4 -> v2`, timeoutMs: 10_000 }])
})

test('a confirmed command that ends at once is a job all the same, its run begun only after the dispatch answered', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: scripted({ 'train rollback tuned': { stdout: ROLLBACK_TUNED_HANDMADE } }), ask: () => 'Run it' })
  await $.session.start(START)

  const { text } = await apogee($, 'rollback tuned')

  expect(text).toBe(`${BIN}/apogee train rollback tuned is running as job rollback-1; a toast says when it ends.`)
  expect(changes(made)).toEqual([])

  await made.clock.settle()

  expect(changes(made)).toEqual([['train', 'rollback', 'tuned']])
  expect(made.toasts.map(toast => toast.text)).toEqual([`Apogee job rollback-1: Ran ${BIN}/apogee train rollback tuned:\n${ROLLBACK_TUNED_HANDMADE.trimEnd()}`])
  expect(await callTool($, 'apogee_job', { id: 'rollback-1' })).toEqual({ result: ROLLBACK_TUNED_HANDMADE.trimEnd() })
})

test('a completion that ends inside the window is answered there, with no job and no toast', async ($, on) => {
  let made: Machine | undefined
  made = machine(on, FOUND, undefined, {
    script: cliPath(async args => {
      if (args[0] !== 'complete') return undefined
      await (made as Machine).clock.sleep(29_000)

      return { stdout: COMPLETE_ANSWER_HANDMADE }
    }),
  })
  await $.session.start(START)

  const answer = callTool($, 'apogee_complete', { prompt: 'Capital of France?', backend: 'q4' })
  await made.clock.advance(29_000)

  expect(await answer).toEqual({ result: 'The capital of France is Paris.' })
  await made.clock.advance(60_000)
  expect(made.toasts).toEqual([])
})

test("a job's failure is toasted with its status and last words", async ($, on) => {
  let made: Machine | undefined
  made = machine(on, FOUND, undefined, {
    script: async args => {
      if (args[1] !== 'rollback') return undefined
      await (made as Machine).clock.sleep(40_000)

      return { exitCode: 1, stderr: 'error: the ledger is unreadable\n' }
    },
    ask: () => 'Run it',
  })
  await $.session.start(START)

  expect((await apogee($, 'rollback tuned')).text).toContain('is running as job rollback-1')
  await made.clock.advance(40_000)

  expect(made.toasts.map(toast => toast.text)).toEqual([
    `Apogee job rollback-1: ${BIN}/apogee train rollback tuned failed (exit status 1): error: the ledger is unreadable`,
  ])
})

test('each job takes the next number of its kind', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: scripted({ 'train rollback tuned': { stdout: ROLLBACK_TUNED_HANDMADE } }), ask: () => 'Run it' })
  await $.session.start(START)

  const first = await apogee($, 'rollback tuned')
  await made.clock.settle()
  const second = await apogee($, 'rollback tuned')

  expect([first.text, second.text].map(text => /job (\S+);/.exec(text ?? '')?.[1])).toEqual(['rollback-1', 'rollback-2'])
})
