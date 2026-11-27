import { describe, expect, test } from 'vitest'
import { settle, suitesPlace } from './SuitesHarness'

describe('the Suites place over the recorded suites (design 1p): the Mac’s SuitesViewModel, drawn as the old prototype’s place', () => {
  test('it reads config get suites, each suite’s members, the default models status names and each suite’s footprint -- and selects the default', async () => {
    const { suites, child } = suitesPlace()
    await suites.appeared()
    expect(child.calls.map((call) => `${call.kind} ${call.arguments.join(' ')}`)).toEqual([
      'commands config get suites',
      'commands config get suites.cloud',
      'commands config get suites.fast',
      'commands config get suites.pair',
      'commands config get suites.shared',
      'commands config get suites.vast',
      'reads models status',
      'reads models status --suite cloud',
      'reads models status --suite fast',
      'reads models status --suite pair',
      'reads models status --suite shared',
      'reads models status --suite vast',
    ])
    const face = suites.face()
    expect(face.list.map((row) => [row.name, row.isDefault, row.summary, row.over, row.current])).toEqual([
      ['cloud', false, 'fits', false, false],
      ['fast', true, 'no member holds memory on this machine', false, true],
      ['pair', false, 'no member holds memory on this machine', false, false],
      ['shared', false, 'fits', false, false],
      ['vast', false, 'it does not fit', true, false],
    ])
    expect(face.detail).toMatchObject({ shown: true, name: 'fast', defaultLabel: 'Stop using it by default', total: 'suite fast: no member holds memory on this machine', tone: 'plain' })
    expect(face.empty).toBe('')
  })

  test('a suite’s page: its members as the CLI priced them, the window its config pins, its verdict, and the old footer naming the read', async () => {
    const { suites } = suitesPlace()
    await suites.appeared()
    suites.select('shared')
    const detail = suites.face().detail
    expect(detail.members).toEqual([
      { role: 'chat', backend: 'root', window: 'its own', holds: '41096 MiB -- 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens' },
      { role: 'extraction', backend: 'scribe', window: 'its own', holds: "136 MiB -- shares root's 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens" },
      { role: 'utility', backend: 'helper', window: 'its own', holds: "136 MiB -- shares root's 40960 MiB of weights, 136 MiB of q8_0 cache at 8192 tokens, 0 MiB of adapters" },
    ])
    expect(detail.tone).toBe('fits')
    expect(detail.total).toBe("suite shared needs 42392 MiB of this machine's 110100 MiB: root 41096 + scribe 136 (shares root's weights, 40960 MiB saved) + helper 136 (shares root's weights, 40960 MiB saved) + 1024 margin -- fits")
    expect(detail.footer).toBe("apogee's own verdict, from models status --suite shared: the one chat --suite admits a session by, refusing a suite that does not fit unless forced.")
    expect([detail.openLabel, detail.openHelp, detail.switchLabel, detail.switchHelp, detail.defaultLabel]).toEqual([
      'Open a chat under it',
      'apogee chat --suite shared',
      'Switch the session to it',
      'Carries the open conversation on under shared, as /suite does: the session restarts with --resume and --suite',
      'Use for new sessions',
    ])
  })

  test('a suite over budget relabels both actions “…anyway” and adds --force, and each door carries the force', async () => {
    const { suites, opened, switched } = suitesPlace()
    await suites.appeared()
    suites.select('vast')
    const detail = suites.face().detail
    expect([detail.openLabel, detail.openHelp, detail.switchLabel, detail.switchHelp, detail.tone]).toEqual([
      'Open a chat under it anyway',
      "apogee chat --suite vast --force: run it over this machine's memory",
      'Switch the session to it anyway',
      'Carries the open conversation on under vast, as /suite does: the session restarts with --resume and --suite --force',
      'over',
    ])
    suites.openChatUnderIt()
    suites.switchSessionToIt()
    suites.select('cloud')
    suites.openChatUnderIt()
    expect(opened).toEqual([
      ['vast', true],
      ['cloud', false],
    ])
    expect(switched).toEqual([['vast', true]])
  })

  test('Switch the session to it is off, and does nothing, while the open session cannot move (a private one with a turn sent)', async () => {
    const { suites, switched } = suitesPlace({ switchable: false })
    await suites.appeared()
    expect(suites.face().detail.canSwitch).toBe(false)
    suites.switchSessionToIt()
    expect(switched).toEqual([])
  })

  test('Use for new sessions runs config set-default-suite, says it in the old words, keeps what the CLI reported over the stand-in’s unchanged read, and tells the composition; Stop using it by default sets it off', async () => {
    const { suites, said, child, defaults } = suitesPlace()
    await suites.appeared()
    suites.select('cloud')
    await suites.makeDefault()
    expect(child.calls.some((call) => call.arguments.join(' ') === 'config set-default-suite cloud')).toBe(true)
    expect(said).toEqual(['cloud is the default suite'])
    expect(suites.face().list.filter((row) => row.isDefault).map((row) => row.name)).toEqual(['cloud'])
    expect(suites.face().detail.defaultLabel).toBe('Stop using it by default')
    expect(defaults()).toBe(1)
    await suites.makeDefault()
    expect(said.at(-1)).toBe('No default suite')
    expect(suites.face().list.some((row) => row.isDefault)).toBe(false)
    await suites.appeared()
    expect(suites.face().list.some((row) => row.isDefault), 'kept over the read back').toBe(false)
  })

  test('New suite… keeps the old toast: no CLI verb is behind it yet', async () => {
    const { suites, said, child } = suitesPlace()
    await suites.appeared()
    const before = child.calls.length
    suites.newSuite()
    expect(said).toEqual(['New suite: pick a main model and helpers (prototype)'])
    expect(child.calls.length).toBe(before)
  })

  test('no apogee: nothing read and nothing drawn, as the old place draws no missing state', async () => {
    const { suites, child } = suitesPlace({ apogee: 'missing' })
    await suites.appeared()
    await settle()
    expect(child.calls).toEqual([])
    expect(suites.face()).toMatchObject({ list: [], empty: '', detail: { shown: false } })
  })

  test('a config it cannot read: the CLI’s refusal said in the list, never drawn as no suites', async () => {
    const { suites } = suitesPlace({ apogee: 'config-typo' })
    await suites.appeared()
    expect(suites.face().list).toEqual([])
    expect(suites.face().empty).toMatch(/^apogee config: /)
  })
})
