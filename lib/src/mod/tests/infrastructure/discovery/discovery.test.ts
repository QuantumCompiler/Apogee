// The discovery ladder (the macOS app's, mirrored: its BinaryDiscoveryTests),
// over scripted $.env, $.fs and $.process results.
import { expect, test } from 'claude-code/testing'

import { candidates, discoveryOptions } from '../../../src/infrastructure/discovery/discovery'
import { BIN, DEV_VERSION, HOME, RELEASE_VERSION, START, machine } from '../../support/machine'
import { keptState } from '../../support/state'

test('the places looked, in order: the override, then the release, dev and test channels', () => {
  const release = candidates(HOME, { override: '/opt/tools/apogee', channel: 'release' })

  expect(release.map(one => one.source)).toEqual(['override', 'release', 'dev', 'test'])
  expect(release.map(one => one.path)).toEqual([
    '/opt/tools/apogee',
    `${BIN}/apogee`,
    `${BIN}/apogee-dev`,
    `${BIN}/apogee-test`,
  ])
  expect(candidates(HOME, { channel: 'release' }).map(one => one.source)).toEqual(['release', 'dev', 'test'])
})

test('the dev channel looks at the override and apogee-dev only, never the release binary', () => {
  expect(candidates(HOME, { override: '/opt/tools/apogee-dev', channel: 'dev' })).toEqual([
    { path: '/opt/tools/apogee-dev', source: 'override' },
    { path: `${BIN}/apogee-dev`, source: 'dev' },
  ])
  expect(candidates(HOME, { channel: 'dev' })).toEqual([{ path: `${BIN}/apogee-dev`, source: 'dev' }])
})

test('a leading ~/ in the override is the home folder', () => {
  expect(candidates(HOME, { override: '~/tools/apogee', channel: 'release' })[0]).toEqual({
    path: `${HOME}/tools/apogee`,
    source: 'override',
  })
})

test('the options read: an empty override is none, and an unknown channel is release', () => {
  expect(discoveryOptions({ binary: '', channel: 'release' })).toEqual({ channel: 'release' })
  expect(discoveryOptions({ binary: ' ~/tools/apogee ', channel: 'dev' })).toEqual({
    override: '~/tools/apogee',
    channel: 'dev',
  })
  expect(discoveryOptions({})).toEqual({ channel: 'release' })
  expect(discoveryOptions({ channel: 'nightly' })).toEqual({ channel: 'release' })
})

test("the release channel's binary is found, verified by its version line", async ($, on) => {
  const kept = keptState(on)
  const made = machine(on, { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } })

  await $.session.start(START)

  expect(made.runs).toEqual([{ argv: [`${BIN}/apogee`, 'version'], init: { timeoutMs: 10_000 } }])
  expect(kept.binary()?.binary).toEqual({
    kind: 'found',
    path: `${BIN}/apogee`,
    source: 'release',
    version: '0.1.9',
    versionLine: 'apogee 0.1.9 (7072bdf, 2026-10-19, macos-arm64)',
    channel: 'release',
    skipped: [],
  })
})

test('the override comes first, ahead of an installed release', { options: { binary: '~/tools/apogee' } }, async ($, on) => {
  const kept = keptState(on)
  machine(on, {
    [`${BIN}/apogee`]: { stdout: RELEASE_VERSION },
    [`${HOME}/tools/apogee`]: { stdout: 'apogee 0.2.0 (dev)\n' },
  })

  await $.session.start(START)

  const binary = kept.binary()?.binary
  expect(binary?.kind === 'found' && [binary.source, binary.path, binary.version, binary.channel]).toEqual([
    'override',
    `${HOME}/tools/apogee`,
    '0.2.0',
    undefined,
  ])
})

test('a candidate that fails verification is skipped and said, and the next one found', { options: { binary: '/Users/someone/nowhere/apogee' } }, async ($, on) => {
  const kept = keptState(on)
  machine(on, {
    [`${BIN}/apogee`]: { stdout: 'hello\n' },
    [`${BIN}/apogee-dev`]: { exitCode: 1, stderr: 'warming up\nerror: broken install\n' },
    [`${BIN}/apogee-test`]: { stdout: RELEASE_VERSION },
  })

  await $.session.start(START)

  const binary = kept.binary()?.binary
  expect(binary?.kind === 'found' && binary.source).toBe('test')
  expect(binary?.skipped).toEqual([
    { path: '~/nowhere/apogee', source: 'override', reason: 'not installed' },
    { path: '~/.local/bin/apogee', source: 'release', reason: '`version` printed "hello", not Apogee\'s version line' },
    { path: '~/.local/bin/apogee-dev', source: 'dev', reason: '`version` exited with status 1: error: broken install' },
  ])
})

test('a binary that cannot be run, or does not answer in time, is skipped with what the run said', async ($, on) => {
  const kept = keptState(on)
  machine(on, {
    [`${BIN}/apogee`]: { refuses: 'permission denied' },
    [`${BIN}/apogee-dev`]: { refuses: 'timed out after 10000 ms' },
    [`${BIN}/apogee-test`]: { exitCode: 2 },
  })

  await $.session.start(START)

  const binary = kept.binary()?.binary
  expect(binary?.kind).toBe('not-found')
  expect(binary?.skipped.map(one => one.reason)).toEqual([
    expect.stringMatching(/^could not run `version`: .*permission denied/),
    expect.stringMatching(/^could not run `version`: .*timed out after 10000 ms/),
    '`version` exited with status 2',
  ])
})

test('a place that cannot be looked at is skipped with why, and the ladder goes on', { options: { binary: '/net/share/apogee' } }, async ($, on) => {
  const kept = keptState(on)
  const made = machine(on, {
    '/net/share/apogee': { unreadable: 'network location' },
    [`${BIN}/apogee`]: { stdout: RELEASE_VERSION },
  })

  await $.session.start(START)

  expect(made.runs.map(run => run.argv[0])).toEqual([`${BIN}/apogee`])
  const binary = kept.binary()?.binary
  expect(binary?.kind === 'found' && [binary.source, binary.path]).toEqual(['release', `${BIN}/apogee`])
  expect(binary?.skipped).toEqual([
    { path: '/net/share/apogee', source: 'override', reason: expect.stringMatching(/^could not be looked at: .*network location/) },
  ])
})

test('the dev channel drives apogee-dev, and says the channel it reported', { options: { channel: 'dev' } }, async ($, on) => {
  const kept = keptState(on)
  const made = machine(on, {
    [`${BIN}/apogee`]: { stdout: RELEASE_VERSION },
    [`${BIN}/apogee-dev`]: { stdout: DEV_VERSION },
  })

  await $.session.start(START)

  expect(made.runs.map(run => run.argv[0])).toEqual([`${BIN}/apogee-dev`])
  const binary = kept.binary()?.binary
  expect(binary?.kind === 'found' && [binary.source, binary.version, binary.channel]).toEqual(['dev', '0.2.0', 'dev'])
})

test('the dev channel never falls back to the release binary', { options: { channel: 'dev' } }, async ($, on) => {
  const kept = keptState(on)
  const made = machine(on, { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } })

  await $.session.start(START)

  expect(made.runs).toEqual([])
  expect(kept.binary()?.binary).toEqual({
    kind: 'not-found',
    skipped: [{ path: '~/.local/bin/apogee-dev', source: 'dev', reason: 'not installed' }],
  })
})

test('with HOME unset, the override is still tried', { options: { binary: '/opt/tools/apogee' } }, async ($, on) => {
  const kept = keptState(on)
  machine(on, { '/opt/tools/apogee': { stdout: RELEASE_VERSION } }, null)

  await $.session.start(START)

  const binary = kept.binary()?.binary
  expect(binary?.kind === 'found' && [binary.path, binary.skipped]).toEqual(['/opt/tools/apogee', []])
})

test('with HOME unset and no override, every channel place is passed over saying why', async ($, on) => {
  const kept = keptState(on)
  const made = machine(on, { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }, null)

  await $.session.start(START)

  expect(made.runs).toEqual([])
  expect(kept.binary()?.binary?.skipped).toEqual([
    { path: '~/.local/bin/apogee', source: 'release', reason: 'HOME is not set' },
    { path: '~/.local/bin/apogee-dev', source: 'dev', reason: 'HOME is not set' },
    { path: '~/.local/bin/apogee-test', source: 'test', reason: 'HOME is not set' },
  ])
})

test('none found is the typed condition, every place said', async ($, on) => {
  const kept = keptState(on)
  const made = machine(on, {})

  await $.session.start(START)

  expect(made.runs).toEqual([])
  expect(kept.binary()?.binary).toEqual({
    kind: 'not-found',
    skipped: [
      { path: '~/.local/bin/apogee', source: 'release', reason: 'not installed' },
      { path: '~/.local/bin/apogee-dev', source: 'dev', reason: 'not installed' },
      { path: '~/.local/bin/apogee-test', source: 'test', reason: 'not installed' },
    ],
  })
})

test('an empty HOME counts as unset, never as the root folder', async ($, on) => {
  const kept = keptState(on)
  const made = machine(on, { '/.local/bin/apogee': { stdout: RELEASE_VERSION } }, '')

  await $.session.start(START)

  expect(made.runs).toEqual([])
  expect(kept.binary()?.binary?.skipped.map(one => one.reason)).toEqual(['HOME is not set', 'HOME is not set', 'HOME is not set'])
})
