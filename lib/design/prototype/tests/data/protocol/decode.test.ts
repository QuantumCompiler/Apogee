import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs'
import { dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
import { EventDecoder } from '../../../source/data/protocol/EventDecoder'
import { JobEvent } from '../../../source/data/protocol/JobEvent'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'
import { recordedRoot } from '../../../tooling/sources'

/**
 * The guardrail over every recording (design 1d): read from the trees at test
 * time, with Node, never bundled. The counts are read from disk -- the floors
 * are the 325 documents and 77 streams of 2026-11-14 -- so a recording a later
 * item adds is decoded too, and never breaks the count.
 */
const prototype = resolve(dirname(fileURLToPath(import.meta.url)), '../../..')

/** Every file in a `Fixtures/` folder under the Mac app's tests, and every file under the design's own `fixtures/`. */
function recordings(): string[] {
  const found: string[] = []
  const walk = (directory: string, inFixtures: boolean) => {
    for (const name of readdirSync(directory)) {
      if (name.startsWith('.')) continue
      const path = join(directory, name)
      if (statSync(path).isDirectory()) walk(path, inFixtures || name === 'Fixtures')
      else if (inFixtures) found.push(path)
    }
  }
  walk(recordedRoot(prototype), false)
  const own = join(prototype, 'fixtures')
  if (existsSync(own)) walk(own, true)
  return found.sort()
}

const all = recordings()
const named = (path: string) => relative(resolve(prototype, '../..'), path)

describe('every recording decodes', () => {
  test('every recorded .json decodes to one document', () => {
    const documents = all.filter((path) => path.endsWith('.json'))
    expect(documents.length).toBeGreaterThanOrEqual(325)
    const refused = documents.filter((path) => ReadDocument.from(readFileSync(path, 'utf8')) === undefined).map(named)
    expect(refused).toEqual([])
  })

  test('every line of every recorded .jsonl decodes, as an event and as a job event, an unknown one kept', () => {
    const streams = all.filter((path) => path.endsWith('.jsonl'))
    expect(streams.length).toBeGreaterThanOrEqual(77)
    let lines = 0
    const problems: string[] = []
    const unknown = new Set<string>()
    for (const path of streams) {
      for (const line of readFileSync(path, 'utf8').split('\n')) {
        if (line.trim() === '') continue
        lines += 1
        const parsed = JSON.parse(line) as Record<string, unknown>
        const event = EventDecoder.decode(line)
        if (event.kind.case === 'unreadable') problems.push(`${named(path)}: unreadable: ${line.slice(0, 80)}`)
        if (event.type !== parsed.type) problems.push(`${named(path)}: type ${event.type}, not ${String(parsed.type)}`)
        expect(event.fields).toEqual(parsed)
        if (event.kind.case === 'unknown') unknown.add(event.kind.type)
        const job = JobEvent.decode(line)
        if (job.case === 'other' && job.type === 'unreadable') problems.push(`${named(path)}: unreadable as a job event`)
      }
    }
    expect(problems).toEqual([])
    expect(lines).toBeGreaterThanOrEqual(1617)
    // A job stream's own types are unknown to a chat's decoder, and kept under their own names.
    expect([...unknown]).toEqual(expect.arrayContaining(['job', 'stage', 'iteration']))
  })
})
