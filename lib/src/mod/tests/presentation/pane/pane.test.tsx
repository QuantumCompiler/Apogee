// The pane beside the chat (1c): backends and their models, training, and the
// version ledger, every datum from a document (`models list`, `models status`,
// `train status`, `train versions`), drawn on every surface the tests loop
// over. Absence is said in plain words: no binary, no backends, nothing
// trained, an empty ledger, a document that failed or lacked a field.
import { expect, test } from 'claude-code/testing'

import { apogee, either, served } from '../../support/apogee'
import { MODELS_LIST_EMPTY_HANDMADE, TRAIN_VERSIONS_EMPTY_HANDMADE, TRAIN_VERSIONS_TWO_HANDMADE } from '../../support/handmade'
import { INSTALL_HINT } from '../../../src/infrastructure/discovery/discovery'
import { BIN, DEV_VERSION, RELEASE_VERSION, START, machine } from '../../support/machine'
import {
  MODELS_LIST_ADAPTERS,
  MODELS_STATUS_CHAT,
  TRAIN_STATUS_CYCLE_STALE_LOCK,
  TRAIN_STATUS_CYCLE_UNREADABLE,
  TRAIN_STATUS_NONE,
  TRAIN_STATUS_PIPELINE_RUNNING,
  TRAIN_VERSIONS_ADAPTERS,
} from '../../support/recorded'
import { MODELS_STATUS, SURFACES, TRAIN_STATUS, TRAIN_VERSIONS, buttons, lines, mountPane, paneReads, ranCount } from '../../support/pane'

const FOUND = { [`${BIN}/apogee`]: { stdout: RELEASE_VERSION } }

const RECORDED = paneReads({
  models: { stdout: MODELS_LIST_ADAPTERS },
  roles: { stdout: MODELS_STATUS_CHAT },
  training: { stdout: TRAIN_STATUS_PIPELINE_RUNNING },
  ledger: { stdout: TRAIN_VERSIONS_ADAPTERS },
})

test('/apogee pane opens the pane, and reads its four documents at once', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: either(served(), RECORDED) })
  await $.session.start(START)
  await made.clock.settle()
  const before = ranCount(made, TRAIN_VERSIONS)

  const { text } = await apogee($, 'pane')
  await made.clock.settle()

  expect(text).toBe("Apogee's pane is open.")
  expect(made.opens).toEqual([{ id: 'apogee', title: 'Apogee' }])
  expect([before, ranCount(made, TRAIN_VERSIONS)]).toEqual([0, 1])
  expect(made.runs.find(run => run.argv.slice(1).join(' ') === TRAIN_VERSIONS)?.init).toEqual({ timeoutMs: 30_000 })
})

test('the pane shows backends and models, training and the ledger from the documents, on every surface', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: either(served(), RECORDED) })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)

    expect(await lines(ui)).toEqual([
      'Apogee 0.1.9 · release channel',
      'Backends and models',
      'Roles',
      'chat: EmbeddingGemma-300M-bf16',
      'embedding: EmbeddingGemma-300M-bf16 (via models.default)',
      'extraction: EmbeddingGemma-300M-bf16 (via models.default)',
      "vision: (unset -- the chat's own backend)",
      "transcription: (unset -- the chat's own backend)",
      "utility: (unset -- the chat's own backend)",
      'Local',
      '(not configured) · - · tiny/safetensors/aaaaaaaaaaaa · safetensors',
      'based · llamacpp · tiny-F16.gguf · unreadable',
      'header runs past the end of the file (truncated or corrupt)',
      'q4 · llamacpp · tiny-Q4_K_M.gguf · ok',
      'tuned · llamacpp · tuned-v1.gguf · ok',
      'Training',
      'Running: 1 run (pipe-20261026-074116-s1)',
      'Pipeline: pipe-20261026-074116 running (skills, 1 of 2 stages passed)',
      'Recent runs',
      'pipe-20261026-074116-s1 · running · lora · started 2026-10-26T07:41:16Z',
      'pipe-20261026-074116-s0 · complete · lora · started 2026-10-26T07:41:16Z · gate passed (1.00)',
      'pipe-20261026-074115-s0 · complete · lora · started 2026-10-26T07:41:15Z · gate passed (1.00)',
      'pipe-20261026-074112-s1 · complete · lora · started 2026-10-26T07:41:13Z · gate passed (1.00)',
      'pipe-20261026-074112-s0 · complete · lora · started 2026-10-26T07:41:12Z · gate passed (1.00)',
      'Version ledger',
      'based · active v1',
      'v1 (active) · run 20261026-065637 · promoted 2026-10-26T06:56:37Z · eval 1.00, passed',
      'adapter /Users/someone/.apogee/training/versions/based/v1.lora.gguf · base sha256 ce5065b7f3dc1bcd98592fa57a11f0e9670f16f33f21abf043dd7c903ad0c98c',
      'q4 · active v1',
      'v1 (active) · run 20261026-065638 · promoted 2026-10-26T06:56:39Z · eval 1.00, passed, scored on tiny-Q4_K_M.gguf (Q4_K_M)',
      'adapter /Users/someone/.apogee/training/versions/q4/v1.lora.gguf · base sha256 a81cdd0551b6e79be2276ac019df1c98656527cbdc1829fc7fbf097253c31546',
      'tuned · active v1',
      'v1 (active) · run 20261026-065637 · promoted 2026-10-26T06:56:37Z · eval 1.00, passed',
      'gguf /Users/someone/.apogee/models/tiny/gguf/cf368ccf9b45/tuned-v1.gguf',
    ])
    expect(await buttons(ui)).toEqual([
      { key: 'reread', label: 'Read again' },
      { key: 'promote:pipe-20261026-074116-s0', label: 'Promote' },
      { key: 'promote:pipe-20261026-074115-s0', label: 'Promote' },
      { key: 'promote:pipe-20261026-074112-s1', label: 'Promote' },
      { key: 'promote:pipe-20261026-074112-s0', label: 'Promote' },
    ])
    await ui.unmount()
  }
})

test('the ledger lists the versions document as-is: each ledger in its order, each version with what it carries', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: either(served(), paneReads({ models: { stdout: MODELS_LIST_ADAPTERS }, roles: { stdout: MODELS_STATUS_CHAT }, training: { stdout: TRAIN_STATUS_NONE }, ledger: { stdout: TRAIN_VERSIONS_TWO_HANDMADE } })),
  })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)
    const drawn = await lines(ui)

    expect(drawn.slice(drawn.indexOf('Version ledger'))).toEqual([
      'Version ledger',
      'tuned · active v2',
      'v1 · run 20261026-065637 · promoted 2026-10-26T06:56:37Z · eval 1.00, passed',
      'gguf /Users/someone/.apogee/models/tiny/gguf/cf368ccf9b45/tuned-v1.gguf',
      'v2 (active) · run 20261027-091500 · promoted 2026-10-27T09:15:00Z · eval 0.75, passed',
      'gguf /Users/someone/.apogee/models/tiny/gguf/0d1e2f3a4b5c/tuned-v2.gguf',
      'helper · active v2',
      'v1 · run 20261019-101500 · promoted 2026-10-19T10:30:00Z',
      'mlx /Users/someone/.apogee/models/helper/mlx/1a2b3c4d5e6f · pruned 2026-10-20T09:00:00Z',
      'v2 (active) · run 20261020-111500 · promoted 2026-10-20T11:30:00Z · eval 0.50, failed, scored on helper-Q4_K_M.gguf (Q4_K_M)',
      'mlx /Users/someone/.apogee/models/helper/mlx/6f5e4d3c2b1a',
    ])
    expect((await buttons(ui)).filter(button => button.key?.startsWith('rollback:'))).toEqual([
      { key: 'rollback:tuned', label: 'Roll back' },
      { key: 'rollback:helper', label: 'Roll back' },
    ])
    await ui.unmount()
  }
})

test('nothing trained, an empty ledger and no backends are each said in plain words', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: either(served(), paneReads({ models: { stdout: MODELS_LIST_EMPTY_HANDMADE }, roles: { stdout: MODELS_STATUS_CHAT }, training: { stdout: TRAIN_STATUS_NONE }, ledger: { stdout: TRAIN_VERSIONS_EMPTY_HANDMADE } })),
  })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)
    const drawn = await lines(ui)

    expect(drawn).toContain('No backends configured and no models stored.')
    expect(drawn.slice(drawn.indexOf('Training'))).toEqual(['Training', 'Nothing trained yet: no runs, no pipeline, no cycle.', 'Version ledger', 'No version promoted yet.'])
    expect((await buttons(ui)).map(button => button.key)).toEqual(['reread'])
    await ui.unmount()
  }
})

test('a cycle history train status could not read is said in its words, never drawn as no cycle; a dead pass\'s lock is the cycle\'s state', async ($, on) => {
  const UNREADABLE = 'Cycle: history unreadable -- /Users/someone/.apogee/training/cycle/history.json: cycle history: runs must be an array'
  const LOCK = 'stopped -- cycle.lock is left; remove /Users/someone/.apogee/training/cycle/cycle.lock to run again'
  // Hand-made from the recorded none: nothing trained, and the history unreadable or a dead pass's lock left.
  const bare = JSON.stringify({ ...JSON.parse(TRAIN_STATUS_NONE), cycle_error: '/Users/someone/.apogee/training/cycle/history.json: cycle history: runs must be an array' })
  const locked = JSON.stringify({ ...JSON.parse(TRAIN_STATUS_NONE), stale_lock: '/Users/someone/.apogee/training/cycle/cycle.lock' })
  let training = TRAIN_STATUS_CYCLE_UNREADABLE
  const made = machine(on, FOUND, undefined, {
    script: either(served(), (args, init) => (args.join(' ') === TRAIN_STATUS ? { stdout: training } : RECORDED(args, init))),
  })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()
  const section = async (ui: Awaited<ReturnType<typeof mountPane>>): Promise<string[]> => {
    const drawn = await lines(ui)

    return drawn.slice(drawn.indexOf('Training') + 1, drawn.indexOf('Recent runs') === -1 ? drawn.indexOf('Version ledger') : drawn.indexOf('Recent runs'))
  }

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)

    expect(await section(ui)).toEqual(['Running: none', 'Pipeline: cycle-20261109-041521 aborted (cyclic, 0 of 1 stages passed)', UNREADABLE])
    await ui.unmount()
  }
  training = TRAIN_STATUS_CYCLE_STALE_LOCK
  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)
    await ui.press({ key: 'reread' })

    expect((await section(ui)).at(-1)).toBe(`Cycle: nightly-model, ${LOCK}`)
    await ui.unmount()
  }
  training = bare
  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)
    await ui.press({ key: 'reread' })
    const drawn = await lines(ui)

    expect(await section(ui)).toEqual(['Running: none', UNREADABLE, 'Recent runs: none'])
    expect(drawn.some(line => line.includes('no cycle'))).toBe(false)
    await ui.unmount()
  }
  training = locked
  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)
    await ui.press({ key: 'reread' })

    expect(await section(ui)).toEqual(['Running: none', `Cycle: ${LOCK}`, 'Recent runs: none'])
    await ui.unmount()
  }
})

test('stored models with no backend configured say so above the stored rows', async ($, on) => {
  const stored = JSON.stringify({ data: [JSON.parse(MODELS_LIST_ADAPTERS).data[0]], folded: 0, object: 'list' })
  const made = machine(on, FOUND, undefined, {
    script: either(served(), paneReads({ models: { stdout: stored }, roles: { stdout: MODELS_STATUS_CHAT }, training: { stdout: TRAIN_STATUS_NONE }, ledger: { stdout: TRAIN_VERSIONS_EMPTY_HANDMADE } })),
  })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)
    const drawn = await lines(ui)

    expect(drawn.slice(drawn.indexOf('Local') - 1, drawn.indexOf('Training'))).toEqual([
      'No backend is configured.',
      'Local',
      '(not configured) · - · tiny/safetensors/aaaaaaaaaaaa · safetensors',
    ])
    await ui.unmount()
  }
})

test('with no binary the pane says where the mod looked and how to install it, and reads nothing', async ($, on) => {
  const made = machine(on, {}, undefined, { script: RECORDED })
  await $.session.start(START)

  expect((await apogee($, 'pane')).text).toBe("Apogee's pane is open.")
  await made.clock.advance(60_000)

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)

    expect(await lines(ui)).toEqual([
      'Apogee is not installed where the mod looks: ~/.local/bin/apogee (not installed), ~/.local/bin/apogee-dev (not installed), ~/.local/bin/apogee-test (not installed). ' + INSTALL_HINT,
    ])
    expect(await buttons(ui)).toEqual([])
    await ui.unmount()
  }
  expect(made.runs.filter(run => run.argv[1] !== 'version')).toEqual([])
})

test('before the session has looked for the binary, the pane says so', async ($, on) => {
  machine(on, FOUND)

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)

    expect(await lines(ui)).toEqual(['Apogee has not been looked for yet: the session has not started.'])
    await ui.unmount()
  }
})

test('a document that failed says which command and why; the others are drawn', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: either(
      served(),
      paneReads({ models: { refuses: 'permission denied' }, roles: { exitCode: 1, stderr: 'error: config unreadable\n' }, training: { stdout: TRAIN_STATUS_NONE }, ledger: { stdout: 'not json' } }),
    ),
  })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)
    const drawn = await lines(ui)

    expect(drawn.slice(1, drawn.indexOf('Training'))).toEqual([
      'Backends and models',
      `${BIN}/apogee models status --output-format json answered no document: exited with status 1: error: config unreadable`,
      expect.stringMatching(new RegExp(`^${BIN}/apogee models list --output-format json could not run: .*permission denied`)),
    ])
    expect(drawn.slice(drawn.indexOf('Version ledger'))).toEqual(['Version ledger', `${BIN}/apogee train versions --output-format json answered no document: not a JSON document: "not json"`])
    expect(drawn).toContain('Nothing trained yet: no runs, no pipeline, no cycle.')
    await ui.unmount()
  }
})

test('a read that fails after a good one keeps what was read, its age and the failure said, until a read succeeds', async ($, on) => {
  let isBroken = false
  const made = machine(on, FOUND, undefined, {
    script: either(served(), (args, init) =>
      isBroken && args.join(' ') === TRAIN_VERSIONS ? { exitCode: 2, stderr: 'error: the ledger is locked\n' } : RECORDED(args, init),
    ),
  })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()
  isBroken = true
  await made.clock.advance(120_000)

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)
    await ui.press({ key: 'reread' })
    const drawn = await lines(ui)

    expect(drawn.slice(drawn.indexOf('Version ledger'), drawn.indexOf('Version ledger') + 3)).toEqual([
      'Version ledger',
      `Shown as read 2 min ago; the latest read failed: ${BIN}/apogee train versions --output-format json answered no document: exited with status 2: error: the ledger is locked`,
      'based · active v1',
    ])
    await ui.unmount()
  }

  isBroken = false
  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)
    await ui.press({ key: 'reread' })
    const drawn = await lines(ui)

    expect(drawn.slice(drawn.indexOf('Version ledger'), drawn.indexOf('Version ledger') + 2)).toEqual(['Version ledger', 'based · active v1'])
    expect(drawn.some(line => line.startsWith('Shown as read'))).toBe(false)
    await ui.unmount()
  }
})

test('a field a document lacked is said under its section', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: either(
      served(),
      paneReads({
        models: { stdout: '{"data":[{"backend":"q4","backend_type":"llamacpp","model":"m.gguf","section":"local"}]}' },
        roles: { stdout: MODELS_STATUS_CHAT },
        training: { stdout: TRAIN_STATUS_NONE },
        ledger: { stdout: '{"data":[{"backend_name":"q4","active_version":1,"versions":[{"version":1,"run_id":"r1","gguf_path":"/g"}]}]}' },
      }),
    ),
  })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)
    const drawn = await lines(ui)

    expect(drawn).toContain('models list lacked: data[0].state')
    expect(drawn.slice(drawn.indexOf('Version ledger'))).toEqual(['Version ledger', 'q4 · active v1', 'v1 (active) · run r1 · promoted ', 'gguf /g', 'train versions lacked: data[0].versions[0].promoted_at'])
    await ui.unmount()
  }
})

test('the dev channel is said in the heading', async ($, on) => {
  const made = machine(on, { [`${BIN}/apogee-dev`]: { stdout: DEV_VERSION } }, undefined, { script: either(served(), RECORDED) })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)

    expect((await lines(ui))[0]).toBe('Apogee 0.2.0 · dev channel')
    await ui.unmount()
  }
})

test('before the documents are read, each section says it is reading', async ($, on) => {
  let isSlow = false
  const made = machine(on, FOUND, undefined, {
    script: either(served(), async (args, init) => {
      if (isSlow && args.join(' ') === MODELS_STATUS) await made.clock.sleep(5_000)

      return RECORDED(args, init)
    }),
  })
  await $.session.start(START)
  await made.clock.settle()
  isSlow = true
  await apogee($, 'pane')
  await made.clock.advance(1)

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)

    expect((await lines(ui)).filter(line => line.startsWith('Reading'))).toEqual(['Reading models status…', 'Reading models list…', 'Reading train status…', 'Reading train versions…'])
    await ui.unmount()
  }
})

test('a pane drawn as a widget is a glance: what runs and each active version, no buttons', async ($, on) => {
  const made = machine(on, FOUND, undefined, { script: either(served(), RECORDED) })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  const ui = await mountPane($, 'desktop', 'widget')

  expect(await lines(ui)).toEqual(['Apogee: 1 run training · based v1, q4 v1, tuned v1'])
  expect(await buttons(ui)).toEqual([])
  await ui.unmount()
})

test('a widget says a document that failed as read failed, never unread', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: either(
      served(),
      paneReads({ models: { stdout: MODELS_LIST_ADAPTERS }, roles: { stdout: MODELS_STATUS_CHAT }, training: { stdout: TRAIN_STATUS_PIPELINE_RUNNING }, ledger: { exitCode: 1, stderr: 'error: the ledger is locked\n' } }),
    ),
  })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  const ui = await mountPane($, 'desktop', 'widget')

  expect(await lines(ui)).toEqual(['Apogee: 1 run training · ledger read failed'])
  await ui.unmount()
})

test('a widget over two failed reads says each failed', async ($, on) => {
  const made = machine(on, FOUND, undefined, {
    script: either(
      served(),
      paneReads({
        models: { stdout: MODELS_LIST_ADAPTERS },
        roles: { stdout: MODELS_STATUS_CHAT },
        training: { exitCode: 1, stderr: 'error: the store is locked\n' },
        ledger: { exitCode: 1, stderr: 'error: the ledger is locked\n' },
      }),
    ),
  })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()

  const ui = await mountPane($, 'desktop', 'widget')

  expect(await lines(ui)).toEqual(['Apogee: training read failed · ledger read failed'])
  await ui.unmount()
})

test('a widget keeping good reads past later failures says how old each is and that the latest failed', async ($, on) => {
  let isBroken = false
  const made = machine(on, FOUND, undefined, {
    script: either(served(), (args, init) =>
      isBroken && (args.join(' ') === TRAIN_VERSIONS || args.join(' ') === TRAIN_STATUS) ? { exitCode: 2, stderr: 'error: the store is locked\n' } : RECORDED(args, init),
    ),
  })
  await $.session.start(START)
  await apogee($, 'pane')
  await made.clock.settle()
  isBroken = true
  await made.clock.advance(600_000)
  const docked = await mountPane($, 'desktop')
  await docked.press({ key: 'reread' })
  await docked.unmount()

  const ui = await mountPane($, 'desktop', 'widget')

  expect(await lines(ui)).toEqual([
    'Apogee: 1 run training (read 10 min ago; the latest read failed) · based v1, q4 v1, tuned v1 (read 10 min ago; the latest read failed)',
  ])
  await ui.unmount()
})

test('an open the engine refuses, or one it leaves undrawn, is said', async ($, on) => {
  machine(on, FOUND, undefined, { script: either(served(), RECORDED), paneRefusal: 'the pane apogee is held by another plugin (other)' })
  await $.session.start(START)

  expect((await apogee($, 'pane')).text).toMatch(/^Apogee's pane could not be opened: .*the pane apogee is held by another plugin \(other\)$/)
})

test('an open the engine leaves undrawn says why, and the pane waits to be drawn', async ($, on) => {
  machine(on, FOUND, undefined, { script: either(served(), RECORDED), paneWaits: 'no-surface: no surface is attached' })
  await $.session.start(START)

  expect((await apogee($, 'pane')).text).toBe("Apogee's pane is open, not drawn yet: no-surface: no surface is attached")
})

test('a drawing that fails draws its fault in its place, on every surface', async ($, on) => {
  on('state.get', (_, e, next) => (e.plugin === 'apogee' && e.key === 'pane' ? { deny: 'the store is unreadable' } : next(e)))
  const made = machine(on, FOUND, undefined, { script: either(served(), RECORDED) })
  await $.session.start(START)
  await made.clock.settle()

  for (const surface of SURFACES) {
    const ui = await mountPane($, surface)

    expect(await lines(ui)).toEqual(["Apogee's pane stopped before it drew; `claude --debug` says why."])
    await ui.unmount()
  }
})

test("another pane's drawing is left to what is beneath", async ($, on) => {
  on('ui.render', ($, e) => {
    const { Text } = $.ui.resolve(e)

    return <Text>engine</Text>
  })
  machine(on, FOUND)

  for (const surface of SURFACES) {
    const ui = await $.ui.mount({
      plugin: 'apogee',
      surface,
      component: 'Pane',
      requestId: 'other',
      props: { title: 'Other', isFocused: false, bodyColumns: 80, placement: 'dock', scroll: { offset: 0, bodyRows: 20 }, view: {} },
    })

    expect(await lines(ui)).toEqual(['engine'])
    await ui.unmount()
  }
})
