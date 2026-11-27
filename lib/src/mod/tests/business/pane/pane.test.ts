// The pane's reads and decisions. Its training view is polled every 15 s only
// while the pane is shown (the item's acceptance: polling stops when the pane
// is hidden), a closed pane ends the poll, and one poll runs a copy; and what
// the pane offers is decided here: Promote on a complete run, into the
// configured llamacpp and mlx backends, and Roll back on a ledger that lists a
// version below its active one.
import { expect, test } from 'claude-code/testing'

import { canRollBack, isPromotable, promoteTargets } from '../../../src/business/pane/pane'
import { decodeModelsList } from '../../../src/data/documents/models'
import { decodeTrainVersions } from '../../../src/data/documents/versions'
import { apogee, either, served } from '../../support/apogee'
import { MODELS_LIST_MIXED_HANDMADE, ROLLBACK_TUNED_HANDMADE, TRAIN_VERSIONS_TWO_HANDMADE } from '../../support/handmade'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'
import type { Machine } from '../../support/machine'
import { MODELS_LIST, TRAIN_STATUS, TRAIN_VERSIONS, lines, mountPane, paneReads, ranCount } from '../../support/pane'
import { MODELS_LIST_ADAPTERS, MODELS_STATUS_CHAT, TRAIN_STATUS_NONE, TRAIN_STATUS_PIPELINE_RUNNING, TRAIN_VERSIONS_ADAPTERS } from '../../support/recorded'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }
const READS = paneReads({
  models: { stdout: MODELS_LIST_ADAPTERS },
  roles: { stdout: MODELS_STATUS_CHAT },
  training: { stdout: TRAIN_STATUS_PIPELINE_RUNNING },
  ledger: { stdout: TRAIN_VERSIONS_ADAPTERS },
})

/**
 * Opens the pane at the session's start and lets its first reads land. The
 * status line reads train status too, at the start and then each minute, so
 * every count below is taken within the first minute, where only the pane's
 * poll reads it.
 */
async function opened($: Parameters<typeof apogee>[0], made: Machine): Promise<number> {
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  return ranCount(made, TRAIN_STATUS)
}

test('the training view is read every 15 s while the pane is shown, and nothing else is polled', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: either(served(), READS) })
  const start = await opened($, made)
  const others = [ranCount(made, MODELS_LIST), ranCount(made, TRAIN_VERSIONS)]

  await made.clock.advance(14_999)
  expect(ranCount(made, TRAIN_STATUS)).toBe(start)
  await made.clock.advance(1)
  expect(ranCount(made, TRAIN_STATUS)).toBe(start + 1)
  await made.clock.advance(15_000)
  expect(ranCount(made, TRAIN_STATUS)).toBe(start + 2)
  expect([ranCount(made, MODELS_LIST), ranCount(made, TRAIN_VERSIONS)]).toEqual(others)
})

test('while the pane is hidden the poll reads nothing, and shown again it reads again', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: either(served(), READS) })
  const start = await opened($, made)

  for (const pane of made.panes) pane.isShown = false
  await made.clock.advance(30_000)
  expect(ranCount(made, TRAIN_STATUS)).toBe(start)

  for (const pane of made.panes) pane.isShown = true
  await made.clock.advance(15_000)
  expect(ranCount(made, TRAIN_STATUS)).toBe(start + 1)
})

test('a pane the surface does not draw is hidden, even as the tab in front: the poll reads nothing', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: either(served(), READS), paneWaits: 'opened unasked under 144 columns (100 now)' })
  const start = await opened($, made)
  for (const pane of made.panes) pane.isShown = true

  await made.clock.advance(45_000)

  expect(ranCount(made, TRAIN_STATUS)).toBe(start)
})

test("a command's read-back lands after a poll read begun before the command wrote", async ($, on) => {
  let isRolledBack = false
  let isSlow = false
  let made: Machine | undefined
  made = machine(on, FOUND, undefined, {
    ask: () => 'Run it',
    script: either(served(), async (args, init) => {
      const words = args.join(' ')
      if (words === 'train rollback tuned') {
        isRolledBack = true

        return { stdout: ROLLBACK_TUNED_HANDMADE }
      }
      if (words === TRAIN_STATUS && isSlow && !isRolledBack) {
        // The poll's read, begun before the rollback wrote: slow, and old by the time it lands.
        await (made as Machine).clock.sleep(10_000)

        return { stdout: TRAIN_STATUS_PIPELINE_RUNNING }
      }

      return words === TRAIN_STATUS ? { stdout: isRolledBack ? TRAIN_STATUS_NONE : TRAIN_STATUS_PIPELINE_RUNNING } : READS(args, init)
    }),
  })
  await opened($, made)
  isSlow = true
  await made.clock.advance(15_000)
  await apogee($, 'rollback tuned')
  // The poll's read lands at 25 s; the next tick, at 30 s, would read again.
  await made.clock.advance(11_000)

  const ui = await mountPane($, 'terminal')
  expect(await lines(ui)).toContain('Nothing trained yet: no runs, no pipeline, no cycle.')
  await ui.unmount()
})

test('a closed pane ends the poll: no read, and no look after the one that found it closed', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: either(served(), READS) })
  const start = await opened($, made)

  made.panes = []
  await made.clock.advance(15_000)
  const looks = made.paneLooks()
  await made.clock.advance(40_000)

  expect(ranCount(made, TRAIN_STATUS)).toBe(start)
  expect(looks).toBeGreaterThan(0)
  expect(made.paneLooks()).toBe(looks)
})

test('opening the pane again keeps one poll, and reads every document again', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: either(served(), READS) })
  const start = await opened($, made)
  const ledgers = ranCount(made, TRAIN_VERSIONS)

  await apogee($, 'pane')
  await made.clock.settle()
  expect(ranCount(made, TRAIN_VERSIONS)).toBe(ledgers + 1)
  const reopened = ranCount(made, TRAIN_STATUS)
  expect(reopened).toBe(start + 1)

  await made.clock.advance(15_000)
  expect(ranCount(made, TRAIN_STATUS)).toBe(reopened + 1)
})

test('a poll read still running when the next tick comes is not started twice', async ($, on) => {
  let isSlow = false
  let made: Machine | undefined
  made = machine(on, FOUND, undefined, {
    script: either(served(), async (args, init) => {
      if (isSlow && args.join(' ') === TRAIN_STATUS) await (made as Machine).clock.sleep(20_000)

      return READS(args, init)
    }),
  })
  const start = await opened($, made)
  isSlow = true

  await made.clock.advance(15_000)
  await made.clock.advance(15_000)
  expect(ranCount(made, TRAIN_STATUS)).toBe(start + 1)
  await made.clock.advance(15_000)
  expect(ranCount(made, TRAIN_STATUS)).toBe(start + 2)
  // The tick at 30 s was skipped, not queued: no read began when the first ended at 35 s.
  await made.clock.advance(11_000)
  expect(ranCount(made, TRAIN_STATUS)).toBe(start + 2)
})

test('with no binary, opening the pane reads nothing and polls nothing', async ($, on) => {
  const made = machine(on, {}, undefined, { script: READS })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.advance(45_000)

  expect(made.runs.filter(run => run.argv[1] !== 'version')).toEqual([])
  expect(made.paneLooks()).toBe(0)
})

test('Promote goes into the configured llamacpp and mlx backends, each once, in the order models list names them', () => {
  const recorded = decodeModelsList(MODELS_LIST_ADAPTERS)
  const mixed = decodeModelsList(MODELS_LIST_MIXED_HANDMADE)

  expect(recorded.kind === 'read' && promoteTargets(recorded.value)).toEqual(['based', 'q4', 'tuned'])
  expect(mixed.kind === 'read' && promoteTargets(mixed.value)).toEqual(['gemma-mlx'])
  expect(promoteTargets({ rows: [] })).toEqual([])
})

test('Promote is offered on a run that ran to its end, whatever its gate; never on one running, failed or cancelled', () => {
  expect(isPromotable({ id: 'r', status: 'complete', evalPassed: false })).toBe(true)
  expect(isPromotable({ id: 'r', status: 'complete' })).toBe(true)
  for (const status of ['running', 'failed', 'cancelled', '']) {
    expect([status, isPromotable({ id: 'r', status })]).toEqual([status, false])
  }
})

test('Roll back is offered on a ledger that lists a version below its active one, and on no other', () => {
  const two = decodeTrainVersions(TRAIN_VERSIONS_TWO_HANDMADE)
  const one = decodeTrainVersions(TRAIN_VERSIONS_ADAPTERS)

  expect(two.kind === 'read' && two.value.ledgers.map(canRollBack)).toEqual([true, true])
  expect(one.kind === 'read' && one.value.ledgers.map(canRollBack)).toEqual([false, false, false])
  expect(canRollBack({ backend: 'x', versions: [{ version: 1, runId: 'r', promotedAt: '' }] })).toBe(false)
  expect(canRollBack({ backend: 'x', activeVersion: 1, versions: [{ version: 2, runId: 'r', promotedAt: '' }, { runId: 's', promotedAt: '' }] })).toBe(false)
})

test('a session that starts with the pane already open (a reload kept it) reads it again and polls it', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: either(served(), READS) })
  made.panes = [{ id: 'apogee', title: 'Apogee', isShown: true, isFocused: false, isPlaced: true }]
  await $.session.start(START)
  await made.clock.settle()
  const start = ranCount(made, TRAIN_STATUS)

  expect(ranCount(made, TRAIN_VERSIONS)).toBe(1)
  await made.clock.advance(15_000)
  expect(ranCount(made, TRAIN_STATUS)).toBe(start + 1)
})

test('a session that starts with no pane open reads none of its documents', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: either(served(), READS) })
  await $.session.start(START)
  await made.clock.advance(45_000)

  expect(ranCount(made, TRAIN_VERSIONS)).toBe(0)
})
