// The process runner, the one door to the binary: a read runs within its
// time, a supervised run is read piece by piece to its end with no time of
// its own, and a run that cannot start is said, never thrown.
import { expect, test } from 'claude-code/testing'

import { apogee, scripted } from '../../support/apogee'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }

test('a read runs by $.process.run, bounded by its time, with nothing on its stdin', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: scripted({ 'check --output-format json': { stdout: '{"rows":[],"ok":true,"failures":0,"warnings":0}' } }) })
  await $.session.start(START)

  await apogee($, 'check')

  expect(made.runs.at(-1)).toEqual({ argv: [`${BIN}/apogee`, 'check', '--output-format', 'json'], init: { timeoutMs: 30_000 } })
})

test('a supervised run is spawned, its stdout and stderr gathered, and its exit status kept', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: scripted({ 'train rollback tuned': { exitCode: 3, stdout: 'partly\n', stderr: 'step one\nerror: stopped here\n' } }),
    ask: () => 'Run it',
  })
  await $.session.start(START)

  await apogee($, 'rollback tuned')
  await made.clock.settle()

  expect(made.runs.filter(run => run.isSpawned)).toEqual([{ argv: [`${BIN}/apogee`, 'train', 'rollback', 'tuned'], init: undefined, isSpawned: true }])
  expect(made.toasts.map(toast => toast.text)).toEqual([`Apogee job rollback-1: ${BIN}/apogee train rollback tuned failed (exit status 3): error: stopped here`])
})

test('a supervised run that cannot start is said with why', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: scripted({ 'train rollback tuned': { refuses: 'no such file' } }), ask: () => 'Run it' })
  await $.session.start(START)

  await apogee($, 'rollback tuned')
  await made.clock.settle()

  expect(made.toasts.map(toast => toast.text)).toEqual([expect.stringMatching(/^Apogee job rollback-1: .*could not run: .*no such file/)])
})
