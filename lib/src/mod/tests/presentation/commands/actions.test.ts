// The two commands that change the store: promote and rollback run only after
// the person confirms the exact command shown (the item's confirm pins). The
// CLI's promote and rollback take no --yes, so the confirmation is the one
// gate: nothing is run before the answer, and no --yes is ever passed. Each
// confirmed run goes on as a job, its end toasted and kept for apogee_job.
import { expect, test } from 'claude-code/testing'

import { apogee, callTool, scripted } from '../../support/apogee'
import { ROLLBACK_NO_HISTORY_HANDMADE, ROLLBACK_TUNED_HANDMADE } from '../../support/handmade'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'
import type { Machine } from '../../support/machine'
import { TRAIN_PROMOTE_ADAPTER_ERR, TRAIN_PROMOTE_ADAPTER_OUT } from '../../support/recorded'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }
const ROLLBACK = `${BIN}/apogee train rollback tuned`
const PROMOTE = `${BIN}/apogee train promote 20261026-065637 --as based`

function changes(made: Machine): string[][] {
  return made.runs.filter(run => run.argv[1] === 'train' && (run.argv[2] === 'promote' || run.argv[2] === 'rollback')).map(run => run.argv)
}

test('rollback asks with the exact command, and runs it only on the yes', async ($, on) => {
  let ranBeforeAnswer: string[][] = [[]]
  const made: Machine = machine(on, FOUND, undefined, {
    script: scripted({ 'train rollback tuned': { stdout: ROLLBACK_TUNED_HANDMADE } }),
    ask: () => {
      ranBeforeAnswer = changes(made)

      return 'Run it'
    },
  })
  await $.session.start(START)

  const { text } = await apogee($, 'rollback tuned')
  await made.clock.settle()

  expect(made.asks).toEqual([
    { question: `Roll back tuned to its previous promoted version by running ${ROLLBACK}?`, labels: ['Run it', 'Cancel'] },
  ])
  expect(ranBeforeAnswer).toEqual([])
  expect(changes(made)).toEqual([[`${BIN}/apogee`, 'train', 'rollback', 'tuned']])
  expect(made.runs.find(run => run.argv[2] === 'rollback')?.isSpawned).toBe(true)
  expect(text).toBe(`${ROLLBACK} is running as job rollback-1; a toast says when it ends.`)
  expect(made.toasts.map(toast => toast.text)).toEqual([`Apogee job rollback-1: Ran ${ROLLBACK}:\n${ROLLBACK_TUNED_HANDMADE.trimEnd()}`])
})

test('promote asks with the exact command, and runs it only on the yes', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: scripted({ 'train promote 20261026-065637 --as based': { stdout: TRAIN_PROMOTE_ADAPTER_OUT, stderr: TRAIN_PROMOTE_ADAPTER_ERR } }),
    ask: () => 'Run it',
  })
  await $.session.start(START)

  await apogee($, 'promote 20261026-065637 --as based')
  await made.clock.settle()

  expect(made.asks[0]?.question).toBe(`Promote run 20261026-065637 as based's next version by running ${PROMOTE}?`)
  expect(changes(made)).toEqual([[`${BIN}/apogee`, 'train', 'promote', '20261026-065637', '--as', 'based']])
  expect(made.toasts[0]?.text).toStartWith(`Apogee job promote-1: Ran ${PROMOTE}:\nupdated backend based -> v1 (adapter)`)
  expect(JSON.stringify(await callTool($, 'apogee_job', { id: 'promote-1' }))).toContain('updated backend based -> v1 (adapter)')
})

test('Cancel, a dismissed dialog or words typed instead run nothing, and say so', async ($, on) => {
  const answers = ['Cancel', undefined, 'yes please']
  const made = machine(on, FOUND, undefined, { script: scripted({}), ask: () => answers.shift() })
  await $.session.start(START)

  expect((await apogee($, 'rollback tuned')).text).toBe(`Not run: ${ROLLBACK} (you chose Cancel)`)
  expect((await apogee($, 'rollback tuned')).text).toBe(`Not run: ${ROLLBACK} (no answer)`)
  expect((await apogee($, 'promote 20261026-065637 --as based')).text).toBe(`Not run: ${PROMOTE} (you answered "yes please")`)
  expect(made.asks.length).toBe(3)
  expect(changes(made)).toEqual([])
})

test('no --yes, nor any word the person did not type, ever reaches the CLI', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: scripted({ 'train rollback tuned': { stdout: ROLLBACK_TUNED_HANDMADE }, 'train promote r1 --as q4': { stdout: '' } }),
    ask: () => 'Run it',
  })
  await $.session.start(START)

  await apogee($, 'rollback tuned')
  await apogee($, 'promote r1 --as q4')
  await apogee($, 'rollback --yes')
  await made.clock.settle()

  expect(changes(made)).toEqual([
    [`${BIN}/apogee`, 'train', 'rollback', 'tuned'],
    [`${BIN}/apogee`, 'train', 'promote', 'r1', '--as', 'q4'],
  ])
  expect(made.runs.some(run => run.argv.includes('--yes') || run.argv.includes('-y'))).toBe(false)
})

test('a command that fails after the yes says its status and what it said last', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: scripted({ 'train rollback nightly-model': { exitCode: 1, stderr: ROLLBACK_NO_HISTORY_HANDMADE } }),
    ask: () => 'Run it',
  })
  await $.session.start(START)

  await apogee($, 'rollback nightly-model')
  await made.clock.settle()

  expect(made.toasts.map(toast => toast.text)).toEqual([
    `Apogee job rollback-1: ${BIN}/apogee train rollback nightly-model failed (exit status 1): ${ROLLBACK_NO_HISTORY_HANDMADE.trim()}`,
  ])
  expect(await callTool($, 'apogee_job', { id: 'rollback-1' })).toEqual({
    result: `rollback-1 failed (${BIN}/apogee train rollback nightly-model): exit status 1: ${ROLLBACK_NO_HISTORY_HANDMADE.trim()}`,
  })
})

test('a command that cannot start after the yes says why', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: scripted({ 'train rollback tuned': { refuses: 'permission denied' } }), ask: () => 'Run it' })
  await $.session.start(START)

  await apogee($, 'rollback tuned')
  await made.clock.settle()

  expect(made.toasts.map(toast => toast.text)).toEqual([expect.stringMatching(new RegExp(`^Apogee job rollback-1: ${ROLLBACK} could not run: .*permission denied`))])
})
