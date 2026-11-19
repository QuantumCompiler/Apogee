import { describe, expect, test } from 'vitest'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import suitesSystem from '@recorded/business/reads/Fixtures/suites-system.json?raw'
import apogeeUnknown from '../../../fixtures/business/reads/system-apogee-unknown-handmade.json?raw'
import modelsHeld from '../../../fixtures/business/reads/system-models-held-handmade.json?raw'
import unknown from '../../../fixtures/business/reads/system-unknown-handmade.json?raw'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { Answer, ApogeeMode } from '../../../source/infrastructure/process/StandInChild'
import { MonitorViewModel } from '../../../source/presentation/monitor/ViewModels/MonitorViewModel'

/** Lets every read the clock started land. */
const settled = () => new Promise((resolve) => setTimeout(resolve, 0))

/** A monitor over a stand-in whose `system` read answers `answers`, one a call, on a clock the test drives. */
function monitor(answers: readonly Answer[] = [{ stdout: suitesSystem }], apogee: ApogeeMode = 'installed') {
  const child = new StandInChild(
    { rules: { reads: [{ arguments: ['system'], answer: answers as [Answer, ...Answer[]] }] }, apogee },
    { configTypoRefusal: agentsListBadConfig },
  )
  const clock = new Clock({ still: true })
  const model = new MonitorViewModel(new ReadRunner(child), clock)
  const found = child.locateBinary()
  model.connect(found.case === 'found' ? found.binary : undefined)
  return { child, clock, model }
}

describe('the monitor reads `system` while it is open, and only then', () => {
  test('closed, nothing is read; open, it reads at once and every 5 ticks (2 s); closed again, no more', async () => {
    const { child, clock, model } = monitor()
    clock.advance(20)
    expect(child.calls).toEqual([])
    model.opened()
    model.opened()
    await settled()
    expect(child.calls).toEqual([{ kind: 'reads', arguments: ['system'] }])
    expect(model.isOpen).toBe(true)
    clock.advance(4)
    expect(child.calls).toHaveLength(1)
    clock.advance(1)
    clock.advance(5)
    await settled()
    expect(child.calls).toHaveLength(3)
    expect(model.reading?.cpuPercent).toBe(2.36)
    expect(model.reading?.modelBudget).toBe(115_448_725_504)
    expect(MonitorViewModel.percent(model.reading?.memoryUsed, model.reading?.memoryTotal)).toBe(20)
    model.closed()
    clock.advance(30)
    await settled()
    expect(child.calls, 'closed: the reads stop').toHaveLength(3)
    expect(model.isOpen).toBe(false)
    expect(child.unanswered).toEqual([])
  })

  test('a read that lands after the monitor closed is dropped, as the Mac app drops it', async () => {
    const { model } = monitor()
    model.opened()
    model.closed()
    await settled()
    expect(model.reading).toBeUndefined()
  })
})

describe('what the monitor says', () => {
  test("missing: the Mac app's words, and nothing run", async () => {
    const { child, model } = monitor(undefined, 'missing')
    model.opened()
    await settled()
    expect(model.failure).toBe('Apogee isn’t installed, so the system can’t be read.')
    expect(model.face().state).toBe('failed')
    expect(child.calls).toEqual([])
  })

  test("config-typo: the CLI's refusal, word for word, as the old prototype's line says it", async () => {
    const { model } = monitor(undefined, 'config-typo')
    model.opened()
    await settled()
    expect(model.failure).toBe(
      "apogee system: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)",
    )
    expect(model.face().failure).toBe(model.failure)
  })

  test("the recorded document's numbers, in the old monitor's regions", async () => {
    const { model } = monitor()
    model.opened()
    await settled()
    const face = model.face()
    expect(face.state).toBe('reading')
    expect(face.machine).toBe('THIS MAC · APPLE M3 MAX · 128 GB')
    expect(face.cpu).toEqual({ value: '2%', bars: [2.36] })
    expect(face.gpu).toEqual({ value: 'Apple M3 Max', detail: 'models may use 107.5 GB', stale: undefined })
    expect(face.memory).toEqual({
      value: '26.0',
      warning: false,
      apogee: '3.8 MB',
      others: '26.0 GB',
      free: '102.0 GB',
      shares: { apogee: 3_949_072 / 2 ** 30, others: (27_916_550_144 - 3_949_072) / 2 ** 30, free: 109_522_403_328 / 2 ** 30 },
    })
    expect(face.models).toEqual({ total: '3.8 MB', rows: [], empty: true })
    expect(face.store).toEqual({ heading: 'MODEL STORE · /Users/someone/.apogee/models', total: '240.0 GB', rows: [{ label: 'All models', size: '240.0 GB', percent: 100 }], free: '4.5 TB free on the store’s volume' })
  })

  test('the models a process holds, by name, each with Unload, which calls the composition; no size is made up', async () => {
    const { model } = monitor([{ stdout: modelsHeld }])
    const unloaded: string[] = []
    model.unload = (name) => unloaded.push(name)
    model.opened()
    await settled()
    const rows = model.face().models.rows
    expect(rows.map((row) => [row.name, row.size, row.action])).toEqual([
      ['root', '', 'Unload'],
      ['helper', '', 'Unload'],
    ])
    expect(rows[0]?.detail).toBe('held by apogee’s reading process · the CLI reports no size for it')
    model.unloadModel('helper')
    expect(unloaded).toEqual(['helper'])
  })

  test('"Open models library" calls the composition', () => {
    const { model } = monitor()
    let opened = 0
    model.openLibrary = () => (opened += 1)
    model.showLibrary()
    expect(opened).toBe(1)
  })

  test('a field the platform cannot read is said unknown, with the CLI\'s reason, never a number', async () => {
    const { model } = monitor([{ stdout: unknown }])
    model.opened()
    await settled()
    const face = model.face()
    expect(face.machine).toBe('THIS MAC · CPU UNKNOWN · MEMORY UNKNOWN')
    expect(face.cpu).toEqual({ value: 'unknown', bars: [] })
    expect(face.gpu).toEqual({ value: 'unknown', detail: 'not read on this platform yet · models may use: unknown (this build has no llama.cpp)', stale: undefined })
    expect(face.memory.value).toBe('unknown')
    expect(face.memory.shares).toEqual({ apogee: 0, others: 0, free: 0 })
    expect(face.store.total).toBe('unknown')
    expect(JSON.stringify(face)).not.toMatch(/NaN|undefined|null/)
  })

  test("with memory used known and Apogee's own bytes not, other apps are unknown too, and the bar invents no split", async () => {
    const { model } = monitor([{ stdout: apogeeUnknown }])
    model.opened()
    await settled()
    const face = model.face()
    expect(face.memory.value).toBe('26.0')
    expect(face.memory.apogee).toBe('unknown')
    expect(face.memory.others).toBe('unknown')
    expect(face.memory.free).toBe('102.0 GB')
    expect(face.memory.shares).toEqual({ apogee: 0, others: 0, free: 0 })
  })
})

describe('a failed read keeps the last reading, said stale, as the old GPU row says it', () => {
  test('good, then a read that never finished: the last value stands, and how old it is; the next good one clears it', async () => {
    const { clock, model } = monitor([{ stdout: suitesSystem }, { timedOut: true }, { stdout: suitesSystem }])
    model.opened()
    await settled()
    clock.advance(5)
    await settled()
    expect(model.failure).toBe('it did not finish within 60.0 seconds')
    expect(model.reading).toBeDefined()
    expect(model.face().state).toBe('reading')
    expect(model.face().gpu.stale).toBe('reading failed · last value, 2 s old')
    expect(model.face().cpu.bars, 'a failed read adds no bar').toEqual([2.36])
    clock.advance(5)
    await settled()
    expect(model.face().gpu.stale).toBeUndefined()
    expect(model.face().cpu.bars).toEqual([2.36, 2.36])
  })

  test('the history keeps the last 30 readings', async () => {
    const { clock, model } = monitor()
    model.opened()
    for (let read = 0; read < 34; read += 1) {
      clock.advance(5)
      await settled()
    }
    expect(model.history).toHaveLength(30)
  })
})

describe("the system strip's summary, which the shell draws", () => {
  test("the monitor's numbers, in the old strip's lines", async () => {
    const { model } = monitor()
    expect(model.strip).toBeUndefined()
    model.opened()
    await settled()
    expect(model.strip).toEqual({
      summary: 'mem 26.0/128.0 GB · cpu 2%',
      warning: false,
      rows: [
        { label: 'cpu', percent: 2, value: '2%' },
        { label: 'mem', percent: 20, value: '26.0/128.0 GB', warning: false },
        { label: 'gpu', value: 'Apple M3 Max' },
        { label: 'apogee', value: '3.8 MB · models held: none by this process' },
      ],
    })
  })

  test("the models held, by name, in the CLI's words; none, as the CLI says a one-shot read holds none", async () => {
    const { model } = monitor([{ stdout: modelsHeld }])
    model.opened()
    await settled()
    expect(model.strip?.rows.at(-1)).toEqual({ label: 'apogee', value: '3.8 MB · models held: root, helper' })
  })

  test('with no reading, the strip says the failure', async () => {
    const { model } = monitor(undefined, 'missing')
    model.opened()
    await settled()
    expect(model.strip).toEqual({ summary: 'Apogee isn’t installed, so the system can’t be read.', warning: false, rows: [] })
  })
})

describe('sizes, as the old monitor says them, in the unit that shows them', () => {
  test('bytes, KB, MB, GB and TB, binary, one decimal; unknown when unknown', () => {
    expect(MonitorViewModel.size(undefined)).toBe('unknown')
    expect(MonitorViewModel.size(0)).toBe('0 bytes')
    expect(MonitorViewModel.size(1028)).toBe('1.0 KB')
    expect(MonitorViewModel.size(3_949_072)).toBe('3.8 MB')
    expect(MonitorViewModel.size(27_916_550_144)).toBe('26.0 GB')
    expect(MonitorViewModel.size(4_982_360_883_200)).toBe('4.5 TB')
    expect(MonitorViewModel.percent(5, undefined)).toBeUndefined()
    expect(MonitorViewModel.percent(5, 0)).toBeUndefined()
  })
})
