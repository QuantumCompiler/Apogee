// The pane's two commands that change the store, as buttons: Roll back on a
// ledger, Promote on a complete run (its backend picked first). Each shows
// the exact command before it runs (the confirm pins: the CLI's promote and
// rollback take no --yes, so the confirmation is the one gate), runs only on
// the yes, as a job from its start, and the pane re-reads its documents when
// the job ends (read back, never mirrored). Each test runs on each surface.
import { expect, test } from 'claude-code/testing'

import type { FoundBinary } from '../../../src/business/client/client'
import { decodeTrainStatus } from '../../../src/data/documents/training'
import { paneLines } from '../../../src/presentation/pane/words'
import { either, apogee, scripted, served } from '../../support/apogee'
import { ROLLBACK_TUNED_HANDMADE, TRAIN_VERSIONS_TWO_HANDMADE } from '../../support/handmade'
import { BIN, RELEASE_VERSION, START, machine } from '../../support/machine'
import type { Asker, Machine } from '../../support/machine'
import { MODELS_LIST_ADAPTERS, MODELS_STATUS_CHAT, TRAIN_STATUS_PIPELINE_RUNNING, TRAIN_VERSIONS_ADAPTERS } from '../../support/recorded'
import { MODELS_LIST, SURFACES, TRAIN_VERSIONS, buttons, lines, mountPane, paneReads, ranCount } from '../../support/pane'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }
const ROLLBACK = `${BIN}/apogee train rollback tuned`
const RUN = 'pipe-20261026-074116-s0'
const PROMOTE = `${BIN}/apogee train promote ${RUN} --as q4`

/** Every command run that changes the store. */
function changes(made: Machine): { argv: string[]; isSpawned: boolean }[] {
  return made.runs
    .filter(run => run.argv[1] === 'train' && (run.argv[2] === 'promote' || run.argv[2] === 'rollback'))
    .map(run => ({ argv: run.argv, isSpawned: run.isSpawned === true }))
}

/**
 * The machine the pane's actions meet: the recorded documents, tuned's ledger
 * at v2 until a rollback ran (then the recorded one, tuned at v1), and each
 * confirmed command ending a second after it starts.
 */
function world(on: Parameters<typeof machine>[0], ask: Asker): { made: Machine; ranBeforeAnswer: () => unknown[] } {
  let isRolledBack = false
  let before: unknown[] = ['not asked']
  const made: Machine = machine(on, FOUND, undefined, {
    ask: (question, labels) => {
      before = changes(made)

      return ask(question, labels)
    },
    script: either(
      served(),
      async args => {
        const words = args.join(' ')
        if (words !== 'train rollback tuned' && !words.startsWith('train promote ')) return undefined
        await made.clock.sleep(1_000)
        if (words === 'train rollback tuned') isRolledBack = true

        return { stdout: words === 'train rollback tuned' ? ROLLBACK_TUNED_HANDMADE : 'updated backend q4 -> v2\n' }
      },
      (args, init) =>
        paneReads({
          models: { stdout: MODELS_LIST_ADAPTERS },
          roles: { stdout: MODELS_STATUS_CHAT },
          training: { stdout: TRAIN_STATUS_PIPELINE_RUNNING },
          ledger: { stdout: isRolledBack ? TRAIN_VERSIONS_ADAPTERS : TRAIN_VERSIONS_TWO_HANDMADE },
        })(args, init),
      scripted({}),
    ),
  })

  return { made, ranBeforeAnswer: () => before }
}

for (const surface of SURFACES) {
  test(`Roll back shows the exact command, runs it only on the yes as a job, and the pane reads back at its end (${surface})`, async ($, on) => {
    const { made, ranBeforeAnswer } = world(on, () => 'Run it')
    await $.session.start(START)
    await apogee($, 'pane')
    await made.clock.settle()
    const ui = await mountPane($, surface)
    const reads = [ranCount(made, MODELS_LIST), ranCount(made, TRAIN_VERSIONS)]

    await ui.press({ key: 'rollback:tuned' })

    expect(made.asks).toEqual([{ question: `Roll back tuned to its previous promoted version by running ${ROLLBACK}?`, labels: ['Run it', 'Cancel'] }])
    expect(ranBeforeAnswer()).toEqual([])
    expect(made.toasts.map(toast => toast.text)).toEqual([`${ROLLBACK} is running as job rollback-1; a toast says when it ends.`])
    expect(await lines(ui)).toContain(`Running rollback-1: ${ROLLBACK}`)
    expect([ranCount(made, MODELS_LIST), ranCount(made, TRAIN_VERSIONS)]).toEqual(reads)

    await made.clock.advance(1_000)
    await made.clock.settle()

    expect(changes(made)).toEqual([{ argv: [`${BIN}/apogee`, 'train', 'rollback', 'tuned'], isSpawned: true }])
    expect(made.runs.some(run => run.argv.includes('--yes') || run.argv.includes('-y'))).toBe(false)
    expect(made.toasts.map(toast => toast.text)[1]).toBe(`Apogee job rollback-1: Ran ${ROLLBACK}:\n${ROLLBACK_TUNED_HANDMADE.trimEnd()}`)
    expect([ranCount(made, MODELS_LIST), ranCount(made, TRAIN_VERSIONS)]).toEqual([(reads[0] ?? 0) + 1, (reads[1] ?? 0) + 1])
    const drawn = await lines(ui)
    expect(drawn).toContain('tuned · active v1')
    expect(drawn.some(line => line.startsWith('Running rollback-1'))).toBe(false)
    expect(made.statuses.at(-1)).toMatch(/^Apogee: /)
    await ui.unmount()
  })

  test(`Cancel, a dismissed dialog or other words run nothing, and read nothing again (${surface})`, async ($, on) => {
    const answers = ['Cancel', undefined, 'yes please']
    const { made } = world(on, () => answers.shift())
    await $.session.start(START)
    await apogee($, 'pane')
    await made.clock.settle()
    const ui = await mountPane($, surface)
    const reads = ranCount(made, TRAIN_VERSIONS)

    for (let press = 0; press < 3; press += 1) await ui.press({ key: 'rollback:tuned' })
    await made.clock.advance(5_000)

    expect(made.asks.length).toBe(3)
    expect(made.toasts.map(toast => toast.text)).toEqual([
      `Not run: ${ROLLBACK} (you chose Cancel)`,
      `Not run: ${ROLLBACK} (no answer)`,
      `Not run: ${ROLLBACK} (you answered "yes please")`,
    ])
    expect(changes(made)).toEqual([])
    expect(ranCount(made, TRAIN_VERSIONS)).toBe(reads)
    await ui.unmount()
  })

  test(`Promote opens the pick of the configured backends; a pick shows the exact command and runs only on the yes (${surface})`, async ($, on) => {
    const { made, ranBeforeAnswer } = world(on, () => 'Run it')
    await $.session.start(START)
    await apogee($, 'pane')
    await made.clock.settle()
    const ui = await mountPane($, surface)

    await ui.press({ key: `promote:${RUN}` })

    expect(made.asks).toEqual([])
    const drawn = await lines(ui)
    expect(drawn.slice(drawn.indexOf(`${RUN} · complete · lora · started 2026-10-26T07:41:16Z · gate passed (1.00)`) + 1).slice(0, 2)).toEqual([
      `Promote ${RUN} as:`,
      `Or into a backend not listed: /apogee promote ${RUN} --as <backend>`,
    ])
    expect((await buttons(ui)).filter(button => button.key?.startsWith('promote-'))).toEqual([
      { key: 'promote-as:based', label: 'based' },
      { key: 'promote-as:q4', label: 'q4' },
      { key: 'promote-as:tuned', label: 'tuned' },
      { key: 'promote-cancel', label: 'Cancel' },
    ])

    await ui.press({ key: 'promote-as:q4' })

    expect(made.asks).toEqual([{ question: `Promote run ${RUN} as q4's next version by running ${PROMOTE}?`, labels: ['Run it', 'Cancel'] }])
    expect(ranBeforeAnswer()).toEqual([])
    expect((await lines(ui)).some(line => line.startsWith(`Promote ${RUN}`))).toBe(false)
    const reads = ranCount(made, TRAIN_VERSIONS)
    await made.clock.advance(1_000)
    await made.clock.settle()

    expect(changes(made)).toEqual([{ argv: [`${BIN}/apogee`, 'train', 'promote', RUN, '--as', 'q4'], isSpawned: true }])
    expect(made.toasts.map(toast => toast.text).at(-1)).toBe(`Apogee job promote-1: Ran ${PROMOTE}:\nupdated backend q4 -> v2`)
    expect(ranCount(made, TRAIN_VERSIONS)).toBe(reads + 1)
    await ui.unmount()
  })

  test(`Cancel at the pick asks nothing and runs nothing; a declined promotion runs nothing (${surface})`, async ($, on) => {
    const { made } = world(on, () => 'Cancel')
    await $.session.start(START)
    await apogee($, 'pane')
    await made.clock.settle()
    const ui = await mountPane($, surface)

    await ui.press({ key: `promote:${RUN}` })
    await ui.press({ key: 'promote-cancel' })

    expect((await buttons(ui)).filter(button => button.key?.startsWith('promote-'))).toEqual([])
    expect(made.asks).toEqual([])

    await ui.press({ key: `promote:${RUN}` })
    await ui.press({ key: 'promote-as:tuned' })
    await made.clock.advance(5_000)

    expect(made.asks.length).toBe(1)
    expect(made.toasts.map(toast => toast.text)).toEqual([`Not run: ${BIN}/apogee train promote ${RUN} --as tuned (you chose Cancel)`])
    expect(changes(made)).toEqual([])
    await ui.unmount()
  })
}

test('a promotion with no configured llamacpp or mlx backend to take it says so, and offers only Cancel', async ($, on) => {
  const cloud = JSON.stringify({ data: [{ backend: 'claude', backend_type: 'claude-cli', model: '', section: 'cloud', state: 'installed', type: 'model' }], object: 'list' })
  const made = machine(on, FOUND, undefined, {
    script: either(
      served(),
      paneReads({ models: { stdout: cloud }, roles: { stdout: MODELS_STATUS_CHAT }, training: { stdout: TRAIN_STATUS_PIPELINE_RUNNING }, ledger: { stdout: TRAIN_VERSIONS_ADAPTERS } }),
    ),
  })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)
    await ui.press({ key: `promote:${RUN}` })

    expect(await lines(ui)).toContain(`Promote ${RUN}: models list names no configured llamacpp or mlx backend to promote into.`)
    expect((await buttons(ui)).filter(button => button.key?.startsWith('promote-'))).toEqual([{ key: 'promote-cancel', label: 'Cancel' }])
    await ui.press({ key: 'promote-cancel' })
    await ui.unmount()
  }
})

test('a promotion when models list gave no document says so, never that it names no backend, and offers only Cancel', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: either(
      served(),
      paneReads({ models: { exitCode: 1, stderr: 'error: config unreadable\n' }, roles: { stdout: MODELS_STATUS_CHAT }, training: { stdout: TRAIN_STATUS_PIPELINE_RUNNING }, ledger: { stdout: TRAIN_VERSIONS_ADAPTERS } }),
    ),
  })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)
    await ui.press({ key: `promote:${RUN}` })
    const drawn = await lines(ui)

    expect(drawn).toContain(`Promote ${RUN}: models list gave no document, so no backend can be picked.`)
    expect(drawn.some(line => line.includes('names no configured'))).toBe(false)
    expect((await buttons(ui)).filter(button => button.key?.startsWith('promote-'))).toEqual([{ key: 'promote-cancel', label: 'Cancel' }])
    await ui.press({ key: 'promote-cancel' })
    await ui.unmount()
  }
})

test('a pick before models list has been read says it is still being read, and offers only Cancel', () => {
  // The pane reads models list before train status, so its flow never draws
  // this: the words are pinned over the state itself.
  const training = decodeTrainStatus(TRAIN_STATUS_PIPELINE_RUNNING)
  if (training.kind !== 'read') throw new Error('the recorded train status did not decode')
  const binary: FoundBinary = { kind: 'found', path: `${BIN}/apogee`, source: 'release', version: '0.1.9', versionLine: 'apogee 0.1.9', skipped: [] }
  const drawn = paneLines(binary, { state: { training: { value: training.value, missing: [], readAt: 0 }, promoting: RUN }, actions: [], at: 0 })
  const pick = drawn.find(line => line.text.startsWith(`Promote ${RUN}`))

  expect(pick?.text).toBe(`Promote ${RUN}: models list is still being read, so no backend can be picked yet.`)
  expect(pick?.buttons?.map(button => button.key)).toEqual(['promote-cancel'])
  expect(drawn.some(line => line.text.includes('names no configured'))).toBe(false)
})

test('Read again reads every document of the pane again', async ($, on) => {
  const { made } = world(on, () => undefined)
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)
    const reads = [ranCount(made, MODELS_LIST), ranCount(made, TRAIN_VERSIONS)]

    await ui.press({ key: 'reread' })

    expect([ranCount(made, MODELS_LIST), ranCount(made, TRAIN_VERSIONS)]).toEqual([(reads[0] ?? 0) + 1, (reads[1] ?? 0) + 1])
    await ui.unmount()
  }
})

test('a command confirmed through /apogee while the pane is open re-reads the pane when it ends', async ($, on) => {
  const { made } = world(on, () => 'Run it')
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()
  const reads = ranCount(made, TRAIN_VERSIONS)

  await apogee($, 'rollback tuned')
  await made.clock.advance(1_000)
  await made.clock.settle()

  expect(ranCount(made, TRAIN_VERSIONS)).toBe(reads + 1)
  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)

    expect(await lines(ui)).toContain('tuned · active v1')
    await ui.unmount()
  }
})

test('with no pane open, a confirmed command reads none of its documents', async ($, on) => {
  const { made } = world(on, () => 'Run it')
  await $.session.start(START)
  await made.clock.settle()

  await apogee($, 'rollback tuned')
  await made.clock.advance(1_000)
  await made.clock.settle()

  expect(made.toasts.map(toast => toast.text).at(-1)).toStartWith('Apogee job rollback-1: Ran')
  expect(ranCount(made, TRAIN_VERSIONS)).toBe(0)
})
