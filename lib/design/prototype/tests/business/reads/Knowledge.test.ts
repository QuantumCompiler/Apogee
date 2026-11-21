import collectionInfo from '@recorded/business/reads/Fixtures/collection-info.json?raw'
import collectionInfoVectors from '@recorded/business/reads/Fixtures/collection-info-vectors.json?raw'
import collectionInfoMixed from '@recorded/business/reads/Fixtures/collection-info-vectors-mixed.json?raw'
import collectionsList from '@recorded/business/reads/Fixtures/collections-list.json?raw'
import knowledgeInfoRaw from '@recorded/business/reads/Fixtures/knowledge-info-raw.txt?raw'
import knowledgeInfoRawUnavailable from '@recorded/business/reads/Fixtures/knowledge-info-raw-unavailable.txt?raw'
import knowledgeList from '@recorded/business/reads/Fixtures/knowledge-list.json?raw'
import knowledgeQuery from '@recorded/business/reads/Fixtures/knowledge-query.json?raw'
import { describe, expect, test } from 'vitest'
import { CollectionInfo, CollectionRow, KnowledgeQuery, KnowledgeRecord, RawConversation } from '../../../source/business/reads/Knowledge'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'

const documentOf = (text: string) => ReadDocument.from(text) as ReadDocument

describe("Knowledge's reads (design 1o): the Mac app's KnowledgeRecord, KnowledgeQuery, CollectionRow, CollectionInfo and RawConversation (Knowledge.swift)", () => {
  test('knowledge list: every record in its order, each field the CLI’s, its provenance and link read from their own keys', () => {
    const records = KnowledgeRecord.list(documentOf(knowledgeList))
    expect(records.map((record) => record.id)).toEqual(['kr-20261021T045954Z-0084c5', 'kr-20261021T045944Z-3346a9', 'kr-20261021T045944Z-1e2354'])
    expect(records[2]).toEqual({
      id: 'kr-20261021T045944Z-1e2354',
      intent: 'We dropped the cancel button because testers kept mistaking it for back.',
      decision: 'Remove the cancel button from checkout.',
      status: 'rejected',
      discipline: 'ux',
      link: '',
      source: 'meeting',
      attribution: 'Ada Lovelace',
      timestamp: '2026-10-21T04:59:44.463192Z',
      supersedes: undefined,
      rawRef: '/Users/someone/.apogee/knowledge/raw/kr-20261021T045944Z-1e2354.md',
    })
    expect(records[1]?.link).toBe('PROJ-42')
  })

  test('knowledge query: the matches in the retriever’s order with their scores, which retriever ran, and whether it reranked', () => {
    const query = KnowledgeQuery.read(documentOf(knowledgeQuery)) as KnowledgeQuery
    expect(query.matches.map((match) => [match.record.id, match.score])).toEqual([
      ['kr-20261021T045944Z-1e2354', 0.5962886409906911],
      ['kr-20261021T045944Z-3346a9', 1.542680546962327e-6],
      ['kr-20261021T045954Z-0084c5', 9.656479224850218e-7],
    ])
    expect([query.retriever, query.reranked]).toEqual(['lexical', false])
    expect(KnowledgeQuery.read(documentOf('{"object":"list"}'))).toBeUndefined()
  })

  test('embed list and embed info: the rows, and a collection’s chunks, index, sources and vectors -- null a text index only, one model, or several', () => {
    expect(CollectionRow.list(documentOf(collectionsList))).toEqual([
      { name: 'knowledge', chunks: 3, sources: 3 },
      { name: 'notes', chunks: 5, sources: 5 },
    ])
    const notes = CollectionInfo.read(documentOf(collectionInfo)) as CollectionInfo
    expect([notes.name, notes.chunks, notes.index, notes.sources, notes.vectors, notes.path]).toEqual(['notes', 5, 'ok', ['graph://community/1', 'heat-pumps.md', 'kr-20261021T050657Z-c74d1a', 'solar.md', 'windows.md'], undefined, '/Users/someone/.apogee/embeddings/notes.db'])
    expect(CollectionInfo.read(documentOf(collectionInfoVectors))?.vectors).toEqual({ chunks: 3, dimension: 8, widths: 1, model: 'mock-embed', mixed: [] })
    expect(CollectionInfo.read(documentOf(collectionInfoMixed))?.vectors).toEqual({ chunks: 4, dimension: 8, widths: 1, model: undefined, mixed: ['mock-embed', 'mock-embed-b'] })
  })

  test('the Vectors line in embed info’s own text words: none, one model, mixed models said as the CLI says them, and several widths', () => {
    const info = (text: string) => CollectionInfo.read(documentOf(text)) as CollectionInfo
    expect(CollectionInfo.vectorsLine(info(collectionInfo))).toBe('none -- a text index only')
    expect(CollectionInfo.vectorsLine(info(collectionInfoVectors))).toBe('3 of 3 chunks, 8-d [mock-embed]')
    expect(CollectionInfo.vectorsLine(info(collectionInfoMixed))).toBe("4 of 4 chunks, 8-d [mixed: 'mock-embed' and 'mock-embed-b' -- re-ingest every source with one model]")
    const planted = info(collectionInfoMixed)
    expect(CollectionInfo.vectorsLine({ ...planted, chunks: 1, vectors: { chunks: 1, dimension: 4, widths: 2, model: undefined, mixed: ['a', undefined, 'c'] } })).toBe(
      "1 of 1 chunk, 4-d [mixed: 'a', an unrecorded model and 'c' -- re-ingest every source with one model] -- MIXED: 2 widths",
    )
    expect(CollectionInfo.vectorsLine({ ...planted, vectors: { chunks: 4, dimension: 8, widths: 1, model: undefined, mixed: [] } })).toBe('4 of 4 chunks, 8-d [model not recorded -- re-ingest to bind]')
  })

  test('knowledge info --raw, printed: what follows the CLI’s marker; the archive gone; neither line, nothing', () => {
    expect(RawConversation.printed(knowledgeInfoRaw)).toEqual({ case: 'text', text: 'Ada: testers keep hitting cancel when they mean back. Grace: then we drop the cancel button from checkout. Ada: agreed, ship it.' })
    expect(RawConversation.printed(knowledgeInfoRawUnavailable)).toEqual({ case: 'unavailable' })
    expect(RawConversation.printed('ID: kr-1\n')).toBeUndefined()
  })
})
