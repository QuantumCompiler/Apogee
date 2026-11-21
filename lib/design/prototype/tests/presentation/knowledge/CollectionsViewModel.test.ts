import { describe, expect, test } from 'vitest'
import { collectionQuestion } from '../../../source/presentation/knowledge/Previews/KnowledgeWorlds'
import { callsSince, knowledgePlace } from './KnowledgeHarness'

async function listed(options: Parameters<typeof knowledgePlace>[0] = {}) {
  const place = knowledgePlace(options)
  await place.collections.appeared()
  return place
}

const heatPumpLines = [
  '0.000  [lexical]  heat-pumps.md#0',
  '    # Heat pumps  A heat pump moves heat from a cold place to a warm one. In winter the outdoor unit pul…',
  '0.000  [lexical]  graph://community/1#0',
  '    The heat pump, its compressor and its outdoor unit: one machine moving heat indoors.',
  '0.000  [lexical]  windows.md#0',
  '    # Windows  Triple glazing halves the heat lost through a window. Frames matter as much as glass. ',
  '0.000  [lexical]  kr-20261021T050657Z-c74d1a#0',
  '    Customers kept asking why the outdoor unit runs in winter.  Decision: Explain the outdoor unit in th…',
  '0.000  [lexical]  solar.md#0',
  '    # Solar roofs  Panels make the most power at noon. A battery carries the surplus into the evening.  …',
]

describe("the collections (design 1o): the Mac app's CollectionsViewModel over embed list and embed info, drawn as the old Collections tab draws them", () => {
  test('embed list: the old three, each its sources and chunks; with none chosen, notes’ page shown and its row current, as the old kn.coll starts at notes, read by embed info', async () => {
    const { collections, child } = await listed()
    expect(callsSince(child, 0)).toEqual(['reads embed list', 'reads embed info notes'])
    const face = collections.face()
    expect(face.rows).toEqual([
      { name: 'knowledge', meta: '3 sources · 3 chunks', current: false },
      { name: 'mixed', meta: '4 sources · 4 chunks', current: false },
      { name: 'notes', meta: '5 sources · 5 chunks', current: true },
    ])
    expect(face.page).toEqual({
      shown: true,
      name: 'notes',
      chunks: '5',
      index: 'ok',
      vectors: 'none -- a text index only',
      question: '',
      searched: false,
      results: [],
      failure: '',
      sourceCount: 5,
      sources: ['graph://community/1', 'heat-pumps.md', 'kr-20261021T050657Z-c74d1a', 'solar.md', 'windows.md'],
    })
  })

  test('a collection searched shows collection-query.txt’s lines as the CLI printed them', async () => {
    const { collections, child } = await listed()
    await collections.select('notes')
    collections.setQuestion(collectionQuestion)
    const from = child.calls.length
    await collections.search()
    expect(callsSince(child, from)).toEqual([`commands embed query notes -- ${collectionQuestion}`])
    const page = collections.face().page
    expect([page.searched, page.question, page.results]).toEqual([true, collectionQuestion, heatPumpLines])
    expect(collections.face().rows.map((row) => row.current)).toEqual([false, false, true])
  })

  test('Search with nothing typed on notes searches the old question, filling the field, as the old kno.search does', async () => {
    const { collections, child } = await listed()
    const from = child.calls.length
    await collections.search()
    expect(callsSince(child, from)).toEqual([`commands embed query notes -- ${collectionQuestion}`])
    const page = collections.face().page
    expect([page.searched, page.question, page.results]).toEqual([true, collectionQuestion, heatPumpLines])
  })

  test('mixed: its Vectors line in embed info’s own words; another collection chosen drops the lines asked of the one before', async () => {
    const { collections } = await listed()
    await collections.select('notes')
    collections.setQuestion(collectionQuestion)
    await collections.search()
    await collections.select('mixed')
    const page = collections.face().page
    expect([page.name, page.chunks, page.vectors, page.sourceCount, page.searched, page.results]).toEqual([
      'mixed',
      '4',
      "4 of 4 chunks, 8-d [mixed: 'mock-embed' and 'mock-embed-b' -- re-ingest every source with one model]",
      4,
      false,
      [],
    ])
    await collections.select('knowledge')
    expect(collections.face().page.sources).toEqual(['kr-20261021T045944Z-1e2354', 'kr-20261021T045944Z-3346a9', 'kr-20261021T045954Z-0084c5'])
  })

  test('a search of a collection the CLI recorded none of says the old toast and runs nothing; the old ingest, source and delete toasts', async () => {
    const { collections, child, said } = await listed()
    await collections.select('mixed')
    collections.setQuestion(collectionQuestion)
    const from = child.calls.length
    await collections.search()
    collections.ingest()
    collections.removeSource('solar.md')
    collections.deleteCollection()
    expect(said).toEqual([
      'apogee embed query mixed: its lines, as printed (prototype)',
      'apogee embed ingest: chosen in a sheet, run supervised (prototype)',
      'apogee embed delete mixed --source solar.md: confirmed first (prototype)',
      'apogee embed delete mixed --yes: confirmed first (prototype)',
    ])
    expect(callsSince(child, from)).toEqual([])
  })

  test('a question nobody recorded on notes says the stand-in’s line in place of the results', async () => {
    const { collections } = await listed()
    collections.setQuestion('solar')
    await collections.search()
    expect([collections.face().page.searched, collections.face().page.failure]).toEqual([false, 'no recorded answer for apogee embed query notes -- solar'])
  })

  test('no apogee: no collections, no words and no page, as the old has no missing state (the Mac’s line a follow-up’s); a config typo: the CLI’s refusal', async () => {
    const missing = await listed({ apogee: 'missing' })
    expect([missing.collections.face().rows, missing.collections.face().note, missing.collections.face().page.shown]).toEqual([[], '', false])
    const typo = await listed({ apogee: 'config-typo' })
    expect(typo.collections.face().note).toBe("The collections could not be read: apogee embed: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)")
  })
})
