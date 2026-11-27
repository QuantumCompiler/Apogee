import { describe, expect, test } from 'vitest'
import suitesSystem from '@recorded/business/reads/Fixtures/suites-system.json?raw'
import modelsHeld from '../../../fixtures/business/reads/system-models-held-handmade.json?raw'
import unknown from '../../../fixtures/business/reads/system-unknown-handmade.json?raw'
import { SystemReading } from '../../../source/business/reads/SystemReading'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'

const read = (text: string) => {
  const document = ReadDocument.from(text)
  if (!document) throw new Error('not one document')
  return new SystemReading(document)
}

describe('the system reading: `apogee system --output-format json`, as the Mac app reads it', () => {
  test('the recorded document, every field the monitor draws', () => {
    expect({ ...read(suitesSystem) }).toEqual({
      cpuModel: 'Apple M3 Max',
      cpuPercent: 2.36,
      memoryTotal: 137_438_953_472,
      memoryUsed: 27_916_550_144,
      memoryAvailable: 109_522_403_328,
      apogeeBytes: 3_949_072,
      modelsHeld: [],
      gpuName: 'Apple M3 Max',
      gpuUnknown: undefined,
      modelBudget: 115_448_725_504,
      modelBudgetUnknown: undefined,
      store: '/Users/someone/.apogee/models',
      storeBytes: 257_698_037_868,
      diskAvailable: 4_982_360_883_200,
      diskCapacity: 7_998_551_654_400,
    })
  })

  test('the models a process holds, by name: the CLI says no size for them', () => {
    expect(read(modelsHeld).modelsHeld).toEqual(['root', 'helper'])
  })

  test('a field the platform cannot read is unknown, and its reason kept where the CLI gives one', () => {
    const reading = read(unknown)
    expect(reading.cpuModel).toBe('')
    expect([reading.cpuPercent, reading.memoryTotal, reading.memoryUsed, reading.apogeeBytes, reading.gpuName, reading.modelBudget, reading.storeBytes]).toEqual([
      undefined, undefined, undefined, undefined, undefined, undefined, undefined,
    ])
    expect(reading.gpuUnknown).toBe('not read on this platform yet')
    expect(reading.modelBudgetUnknown).toBe('this build has no llama.cpp')
    expect(reading.store).toBe('/store/models')
  })
})
