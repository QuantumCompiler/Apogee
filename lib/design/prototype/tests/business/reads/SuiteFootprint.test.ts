import suitesConfigGet from '@recorded/business/reads/Fixtures/suites-config-get.txt?raw'
import statusCloud from '@recorded/business/reads/Fixtures/suites-models-status-cloud.json?raw'
import statusFast from '@recorded/business/reads/Fixtures/suites-models-status-fast.json?raw'
import statusShared from '@recorded/business/reads/Fixtures/suites-models-status-shared.json?raw'
import statusVast from '@recorded/business/reads/Fixtures/suites-models-status-vast.json?raw'
import { describe, expect, test } from 'vitest'
import { SuiteFootprint, Suites } from '../../../source/business/reads/SuiteFootprint'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'

const footprint = (text: string) => SuiteFootprint.of(ReadDocument.from(text) as ReadDocument)

describe("a suite as the CLI prices it, models status --suite <name>'s footprint (the Mac app's SuiteFootprint)", () => {
  test("config get suites: one name a line", () => {
    expect(Suites.names(suitesConfigGet)).toEqual(['cloud', 'fast', 'pair', 'shared', 'vast'])
    expect(Suites.names('\n  fast \n\n')).toEqual(['fast'])
  })

  test('vast: it does not fit, its total the CLI’s whole line', () => {
    const vast = footprint(statusVast)
    expect(vast?.suite).toBe('vast')
    expect(vast?.verdict).toBe('doesNotFit')
    expect(vast?.verdictWords).toBe('it does not fit')
    expect(vast?.total).toBe("suite vast needs 247056 MiB of this machine's 110100 MiB: root 41096 + vast 204936 + 1024 margin -- it does not fit")
    expect(vast?.members.map((member) => [member.backend, member.roles])).toEqual([
      ['root', ['chat']],
      ['vast', ['extraction']],
    ])
  })

  test('cloud and shared fit; fast holds nothing here, in the CLI’s words', () => {
    expect(footprint(statusCloud)?.verdictWords).toBe('fits')
    expect(footprint(statusShared)?.verdict).toBe('fits')
    expect(footprint(statusFast)?.verdict).toBe('nothingHeld')
    expect(footprint(statusFast)?.verdictWords).toBe('no member holds memory on this machine')
  })

  test('a document with no footprint carries none', () => {
    expect(footprint('{"fields":[{"field":"suite","value":"(none)"}]}')).toBeUndefined()
  })
})
