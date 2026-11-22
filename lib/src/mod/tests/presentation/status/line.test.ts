// The status line: the active backend, its promoted version and the run
// state, tracking the documents as they change, and saying staleness when the
// reads stop answering. This stands in for the person watching it in a live
// session, which is theirs.
import { expect, test } from 'claude-code/testing'

import { either, served } from '../../support/apogee'
import { modelsStatusHandmade, trainStatusHandmade } from '../../support/handmade'
import { BIN, DEV_VERSION, RELEASE_VERSION, START, machine } from '../../support/machine'
import type { Answer } from '../../support/machine'
import { MODELS_STATUS_UNSET, TRAIN_STATUS_CYCLE_ACTIVE, TRAIN_STATUS_NONE } from '../../support/recorded'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }
const STATUS = 'models status --output-format json'
const TRAINING = 'train status --output-format json'

/** The machine's two documents, as `now` holds them when a read comes. */
function documents(now: { models: Answer; training: Answer }) {
  return either(served(), args => {
    const words = args.join(' ')

    return words === STATUS ? now.models : words === TRAINING ? now.training : undefined
  })
}

test('the line says the backend, its version and the run state, and tracks each as it changes', async ($, on) => {
  const now = { models: { stdout: modelsStatusHandmade('q4') } as Answer, training: { stdout: trainStatusHandmade('q4', 1) } as Answer }
  const made = machine(on, FOUND, undefined, { script: documents(now) })
  await $.session.start(START)

  await made.clock.settle()
  expect(made.statuses.at(-1)).toBe('Apogee: q4 v1 · idle')

  now.training = { stdout: trainStatusHandmade('q4', 2, ['20261121-090000']) }
  await made.clock.advance(60_000)
  expect(made.statuses.at(-1)).toBe('Apogee: q4 v2 · training: 1 run')

  now.models = { stdout: modelsStatusHandmade('tuned') }
  await made.clock.advance(60_000)
  expect(made.statuses.at(-1)).toBe('Apogee: tuned (no promoted version) · training: 1 run')
})

test('no chat backend, nothing trained, and a cycle pass with its pipeline each say so', async ($, on) => {
  const now = { models: { stdout: MODELS_STATUS_UNSET } as Answer, training: { stdout: TRAIN_STATUS_NONE } as Answer }
  const made = machine(on, FOUND, undefined, { script: documents(now) })
  await $.session.start(START)

  await made.clock.settle()
  expect(made.statuses.at(-1)).toBe('Apogee: no chat backend · idle')

  now.models = { stdout: modelsStatusHandmade('nightly-model') }
  now.training = { stdout: TRAIN_STATUS_CYCLE_ACTIVE }
  await made.clock.advance(60_000)
  expect(made.statuses.at(-1)).toBe('Apogee: nightly-model v1 · training: 1 run, pipeline nightly, cycle pass')
})

test('reads that stop answering keep the last line and say it is stale, how old and why; an answer clears it', async ($, on) => {
  const now = { models: { stdout: modelsStatusHandmade('q4') } as Answer, training: { stdout: trainStatusHandmade('q4', 1) } as Answer }
  const made = machine(on, FOUND, undefined, { script: documents(now) })
  await $.session.start(START)
  await made.clock.settle()

  now.training = { refuses: 'timed out after 30000 ms' }
  await made.clock.advance(60_000)
  expect(made.statuses.at(-1)).toMatch(
    new RegExp(`^Apogee: q4 v1 · idle · stale: read 1 min ago; ${BIN}/apogee train status --output-format json could not run: .*timed out after 30000 ms$`),
  )
  await made.clock.advance(120_000)
  expect(made.statuses.at(-1)).toMatch(/^Apogee: q4 v1 · idle · stale: read 3 min ago; /)

  now.training = { stdout: trainStatusHandmade('q4', 1) }
  await made.clock.advance(60_000)
  expect(made.statuses.at(-1)).toBe('Apogee: q4 v1 · idle')
})

test('a first read that fails says the status is unread, and why', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: documents({ models: { exitCode: 1, stderr: 'error: config.json: cannot open config file\n' }, training: { stdout: TRAIN_STATUS_NONE } }),
  })
  await $.session.start(START)

  await made.clock.settle()

  expect(made.statuses.at(-1)).toBe(
    `Apogee: status unread: ${BIN}/apogee models status --output-format json answered no document: exited with status 1: error: config.json: cannot open config file`,
  )
})

test('fields the documents lack are said on the line, never guessed', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: documents({ models: { stdout: '{"fields":[{"field":"embedding","value":"q4"}]}' }, training: { stdout: '{"versions":[]}' } }),
  })
  await $.session.start(START)

  await made.clock.settle()

  expect(made.statuses.at(-1)).toBe('Apogee: chat backend unread · run state unread · lacked: models status chat, train status running, recent_runs, active_pipeline, cycle_active')
})

test('on the dev channel the line says it drives the dev channel', { options: { channel: 'dev' } }, async ($, on) => {
  const made = machine(on, { [`${BIN}/apogee-dev`]: { stdout: DEV_VERSION } }, undefined, {
    script: documents({ models: { stdout: modelsStatusHandmade('q4') }, training: { stdout: trainStatusHandmade('q4', 3) } }),
  })
  await $.session.start(START)

  await made.clock.settle()

  expect(made.statuses.at(-1)).toBe('Apogee (dev): q4 v3 · idle')
})
