// The `/apogee` set: registered at the session's start, each word the real
// command with its document formatted, over the binary discovery found. This
// stands in for the person typing it in a live session, which is theirs.
import { expect, test } from 'claude-code/testing'

import { apogee, scripted } from '../../support/apogee'
import { BIN, DEV_VERSION, RELEASE_VERSION, START, machine } from '../../support/machine'
import {
  CHECK_ADAPTERS,
  MODELS_LIST_ADAPTERS,
  MODELS_LIST_SERVER_TOOLS,
  TRAIN_STATUS_CYCLE_ACTIVE,
  TRAIN_STATUS_CYCLE_STALE_LOCK,
  TRAIN_STATUS_CYCLE_UNREADABLE,
  TRAIN_STATUS_STALE_LOCK_NO_HISTORY,
} from '../../support/recorded'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }

test('the session start registers /apogee with its set in the hint', async ($, on) => {
  const made = machine(on, FOUND)

  await $.session.start(START)

  expect(made.commands).toEqual([
    {
      name: 'apogee',
      description: "Apogee on this machine: the install check, the models, training's status, the pane; promote and rollback ask first",
      argumentHint: 'check | models | train status | promote <run> --as <backend> | rollback <backend> | pane',
    },
  ])
})

test('/apogee with nothing after it says which binary it drives, its channel, and the set', async ($, on) => {
  machine(on, FOUND)
  await $.session.start(START)

  const { text } = await apogee($)

  expect(text).toStartWith(`Apogee 0.1.9 at ${BIN}/apogee (release channel)`)
  expect(text).toContain('/apogee check')
  expect(text).toContain('/apogee promote <run> --as <backend>')
  expect(text).toContain('asks first')
})

test('/apogee on the dev channel says it drives apogee-dev', { options: { channel: 'dev' } }, async ($, on) => {
  machine(on, { [`${BIN}/apogee-dev`]: { stdout: DEV_VERSION } })
  await $.session.start(START)

  expect((await apogee($)).text).toStartWith(`Apogee 0.2.0 at ${BIN}/apogee-dev (dev channel)`)
})

test('/apogee says the places passed over on the way to the binary', { options: { binary: '/opt/none/apogee' } }, async ($, on) => {
  machine(on, FOUND)
  await $.session.start(START)

  expect((await apogee($)).text).toContain('Passed over: /opt/none/apogee (not installed)')
})

test('with no binary, every word says Apogee is not installed, with the install line, and runs nothing', async ($, on) => {
  const made = machine(on, {})
  await $.session.start(START)

  for (const words of ['', 'check', 'rollback tuned']) {
    const { text } = await apogee($, words)
    expect(text).toStartWith('Apogee is not installed where the mod looks: ~/.local/bin/apogee (not installed)')
    expect(text).toContain('Install Apogee with: curl -fsSL')
  }
  expect(made.runs).toEqual([])
  expect(made.asks).toEqual([])
})

test('/apogee check runs the check as a document and shows every section, a failure with its remedy', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: scripted({ 'check --output-format json': { exitCode: 1, stdout: CHECK_ADAPTERS } }),
  })
  await $.session.start(START)

  const { text } = await apogee($, 'check')

  expect(made.runs.at(-1)).toEqual({ argv: [`${BIN}/apogee`, 'check', '--output-format', 'json'], init: { timeoutMs: 30_000 } })
  expect(text).toStartWith('apogee check: 3 failures, 0 warnings')
  expect(text).toContain('\nVersion\n')
  expect(text).toContain('  ok    apogee: 0.2.0')
  expect(text).toContain('  FAIL  backend: based: unreadable GGUF -- header runs past the end of the file (truncated or corrupt)')
  expect(text).toContain('        fix: re-download the model to /Users/someone/.apogee/models/tiny/gguf/bbbbbbbbbbbb/tiny-F16.gguf')
  expect(text).toContain('  skip  code-exec:')
})

test('/apogee models lists every backend and stored model by section, with its state and note', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: scripted({ 'models list --output-format json': { stdout: MODELS_LIST_ADAPTERS } }),
  })
  await $.session.start(START)

  const { text } = await apogee($, 'models')

  expect(made.runs.at(-1)?.argv).toEqual([`${BIN}/apogee`, 'models', 'list', '--output-format', 'json'])
  expect(text).toStartWith('apogee models list: 4 rows')
  expect(text).toContain('\nLocal\n')
  expect(text).toMatch(/\n {2}q4 +llamacpp +tiny-Q4_K_M\.gguf +ok\n/)
  expect(text).toContain('header runs past the end of the file (truncated or corrupt)')
})

test('/apogee models shows cloud backends under their own heading', async ($, on) => {
  machine(on, FOUND, undefined, { script: scripted({ 'models list --output-format json': { stdout: MODELS_LIST_SERVER_TOOLS } }) })
  await $.session.start(START)

  const { text } = await apogee($, 'models')

  expect(text).toContain('\nCloud\n')
  expect(text).toMatch(/\n {2}claude-api +anthropic +claude-sonnet-4-5 +no key\n/)
})

test('/apogee train status shows what runs, the pipeline, the cycle and each active version', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: scripted({ 'train status --output-format json': { stdout: TRAIN_STATUS_CYCLE_ACTIVE } }),
  })
  await $.session.start(START)

  const { text } = await apogee($, 'train status')

  expect(made.runs.at(-1)?.argv).toEqual([`${BIN}/apogee`, 'train', 'status', '--output-format', 'json'])
  expect(text).toStartWith('apogee train status')
  expect(text).toContain('Running: 1 run (cycle-20261026-074121-s0)')
  expect(text).toContain('Pipeline: cycle-20261026-074121 running (nightly, 0 of 1 stages passed)')
  expect(text).toContain('Cycle: nightly-model, a pass running')
  expect(text).toContain('Versions: nightly-model v1 (1 kept of 1)')
  expect(text).toContain('  pipe-20261026-074117-s1  cancelled  lora')
})

test("/apogee train status says a cycle history it could not read in the CLI's words, and a dead pass's lock as the cycle's state", async ($, on) => {
  let document = TRAIN_STATUS_CYCLE_UNREADABLE
  machine(on, FOUND, undefined, { script: args => (args.join(' ') === 'train status --output-format json' ? { stdout: document } : undefined) })
  await $.session.start(START)
  const cycle = async (): Promise<string[]> => ((await apogee($, 'train status')).text ?? '').split('\n').filter(line => line.startsWith('Cycle'))

  expect(await cycle()).toEqual(['Cycle: history unreadable -- /Users/someone/.apogee/training/cycle/history.json: cycle history: runs must be an array'])
  document = TRAIN_STATUS_CYCLE_STALE_LOCK
  expect(await cycle()).toEqual(['Cycle: nightly-model, stopped -- cycle.lock is left; remove /Users/someone/.apogee/training/cycle/cycle.lock to run again'])
  document = TRAIN_STATUS_STALE_LOCK_NO_HISTORY
  expect(await cycle()).toEqual(['Cycle: stopped -- cycle.lock is left; remove /Users/someone/.apogee/training/cycle/cycle.lock to run again'])
  // Halted, the lock is said on a line of its own, as the CLI says it (train.cpp).
  document = TRAIN_STATUS_CYCLE_STALE_LOCK.replace('"halt_reason":"","halted":false', '"halt_reason":"2 consecutive failures","halted":true')
  expect(document).not.toBe(TRAIN_STATUS_CYCLE_STALE_LOCK)
  expect(await cycle()).toEqual([
    'Cycle: nightly-model, halted: 2 consecutive failures',
    'Cycle lock: stopped -- cycle.lock is left; remove /Users/someone/.apogee/training/cycle/cycle.lock to run again',
  ])
})

test('a read the binary refuses, or answers with no document, is said with the command and why', async ($, on) => {
  machine(on, FOUND, undefined, {
    script: scripted({
      'check --output-format json': { refuses: 'timed out after 30000 ms' },
      'models list --output-format json': { exitCode: 1, stderr: 'loading\nerror: config.json: cannot open config file\n' },
    }),
  })
  await $.session.start(START)

  expect((await apogee($, 'check')).text).toMatch(
    new RegExp(`^${BIN}/apogee check --output-format json could not run: .*timed out after 30000 ms`),
  )
  expect((await apogee($, 'models')).text).toBe(
    `${BIN}/apogee models list --output-format json answered no document: exited with status 1: error: config.json: cannot open config file`,
  )
})

test('a document missing fields is shown with what it has, the missing fields said', async ($, on) => {
  machine(on, FOUND, undefined, {
    script: scripted({ 'check --output-format json': { stdout: '{"rows":[{"section":"Config","name":"config.json","status":"ok"}]}' } }),
  })
  await $.session.start(START)

  const { text } = await apogee($, 'check')

  expect(text).toContain('  ok    config.json')
  expect(text).toContain('The document lacked: rows[0].detail, ok, failures, warnings')
})

test('words outside the set are refused with the usage, and nothing runs', async ($, on) => {
  const made = machine(on, FOUND)
  await $.session.start(START)
  const before = made.runs.length

  const { text } = await apogee($, 'models pull x')

  expect(text).toStartWith("'models pull x' is not one of /apogee's commands.")
  expect(text).toContain('/apogee check')
  expect(made.runs.length).toBe(before)
})
