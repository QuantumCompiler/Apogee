import { describe, expect, test } from 'vitest'
import ingestUnregistrableErr from '@recorded/business/jobs/Fixtures/embed-ingest-unregistrable.err?raw'
import captureValidatedErr from '@recorded/business/jobs/Fixtures/knowledge-capture-validated.err?raw'
import captureValidatedOut from '@recorded/business/jobs/Fixtures/knowledge-capture-validated.out?raw'
import { LineFramer } from '../../../source/infrastructure/process/LineFramer'
import type { Rules } from '../../../source/infrastructure/process/StandInChild'
import { knowledgeNotedRules, knowledgePlaceRules, notedCapture, notedIngest } from '../../../source/presentation/knowledge/Previews/KnowledgeWorlds'
import { callsSince, knowledgePlace, settle } from './KnowledgeHarness'

/** The place's answers with the noted sandbox's over them, as the noted presets compose them. */
function notedRules(extra: Rules = {}): Rules {
  const place = knowledgePlaceRules()
  const noted = knowledgeNotedRules()
  return { reads: [...place.reads, ...noted.reads], commands: place.commands, jobs: [...noted.jobs, ...(extra.jobs ?? [])] }
}

async function noted(extra: Rules = {}) {
  const place = knowledgePlace({ rules: notedRules(extra) })
  await place.collections.appeared()
  await place.records.appeared()
  return place
}

describe("a run's notes on the said-card, the Mac's GUI M22 (design)", () => {
  test('an ingest the config cannot register: the toast is stdout’s line, and my notes’ page shows the CLI’s note word for word', async () => {
    const { collections, child, said, clock } = await noted()
    const from = child.calls.length
    collections.runIngest(notedIngest.collection, notedIngest.path)
    expect(collections.face().run).toEqual({ shown: true, text: 'Working' })
    clock.advance(3)
    await settle()
    expect(callsSince(child, from)).toEqual([`jobs embed ingest -- ${notedIngest.collection} ${notedIngest.path}`, 'reads embed list', 'reads embed info my notes'])
    expect(said).toEqual(['1 file(s), 1 chunk(s)'])
    expect(collections.face().run.shown).toBe(false)
    expect(collections.face().page.name).toBe('my notes')
    expect(collections.face().notes).toEqual({ shown: true, title: 'What the ingest said', lines: LineFramer.lines(ingestUnregistrableErr) })
  })

  test('the card belongs to its collection: another chosen drops it, and so does Dismiss', async () => {
    const { collections, clock } = await noted()
    collections.runIngest(notedIngest.collection, notedIngest.path)
    clock.advance(3)
    await settle()
    expect(collections.face().notes.shown).toBe(true)
    await collections.select('notes')
    expect(collections.face().notes.shown).toBe(false)
    collections.runIngest(notedIngest.collection, notedIngest.path)
    clock.advance(3)
    await settle()
    collections.dismissNotes()
    expect(collections.face().notes.shown).toBe(false)
  })

  test('an ingest that said nothing on stderr leaves no card; a failed one says its last line, with no card', async () => {
    const quiet = { arguments: ['embed', 'ingest', '--', 'notes', '/Users/someone/docs/a note.txt'], exact: true, answer: { stdout: '1 file(s), 1 chunk(s)\n' } }
    const failing = { arguments: ['embed', 'ingest', '--', 'notes', '/Users/someone/docs/broken'], exact: true, answer: { stderr: 'apogee embed: a note first\napogee embed: the ingest failed\n', exit: 1 } }
    const { collections, clock } = await noted({ jobs: [quiet, failing] })
    collections.runIngest('notes', '/Users/someone/docs/a note.txt')
    clock.advance(3)
    await settle()
    expect(collections.face().notes.shown).toBe(false)
    collections.runIngest('notes', '/Users/someone/docs/broken')
    clock.advance(3)
    await settle()
    expect(collections.failure).toBe('apogee embed: the ingest failed')
    expect(collections.face().notes.shown).toBe(false)
  })

  test('a capture its suite’s verifier validated: the toast is its record, and Records keeps the verdict', async () => {
    const { records, said, clock } = await noted()
    records.runCapture(notedCapture)
    clock.advance(LineFramer.lines(captureValidatedOut).length + 2)
    await settle()
    expect(said).toEqual([LineFramer.lines(captureValidatedOut)[0]])
    expect(records.face().notes).toEqual({ shown: true, title: 'What the capture said', lines: LineFramer.lines(captureValidatedErr) })
    records.dismissNotes()
    expect(records.face().notes.shown).toBe(false)
  })
})
