import chatsInfo from '@recorded/business/reads/Fixtures/chats-info.json?raw'
import chatsInfoCompacted from '@recorded/business/reads/Fixtures/chats-info-compacted.json?raw'
import chatsList from '@recorded/business/reads/Fixtures/chats-list.json?raw'
import chatsRecall from '@recorded/business/reads/Fixtures/chats-recall.json?raw'
import chatsRecallEmpty from '@recorded/business/reads/Fixtures/chats-recall-empty.json?raw'
import { describe, expect, test } from 'vitest'
import chatsListHandmade from '../../../fixtures/business/reads/chats-list-handmade.json?raw'
import chatsRecallHandmade from '../../../fixtures/business/reads/chats-recall-handmade.json?raw'
import { ChatDetails, ChatSummary, RecallListing } from '../../../source/business/reads/Chats'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'

const document = (text: string) => {
  const read = ReadDocument.from(text)
  if (!read) throw new Error('a recorded document')
  return read
}

describe("a saved conversation, as chats list lists it (the Mac app's ChatSummary)", () => {
  test("chats-list.json's one row: its id, name, turns, when it changed, its backend, and no pinned model", () => {
    expect(ChatSummary.list(document(chatsList))).toEqual([
      { id: '20261022-182803-f9f6', name: 'Planning A Note', updated: '2026-10-22T18:28:03Z', turns: 5, backend: 'chatty', model: undefined },
    ])
  })

  test('a row with no id is skipped, never guessed; an empty name is the id; an empty backend is none; a stamp that does not parse is no date', () => {
    const rows = ChatSummary.list(document('{"data":[{"name":"x"},{"id":"a","name":"","backend":"","turns":2,"updated":"later"}],"object":"list"}'))
    expect(rows).toEqual([{ id: 'a', name: 'a', updated: undefined, turns: 2, backend: undefined, model: undefined }])
  })

  test('the hand-made list holds the old thirteen, newest first', () => {
    const rows = ChatSummary.list(document(chatsListHandmade))
    expect(rows).toHaveLength(13)
    expect(rows[0]).toMatchObject({ id: '20261119-111221-123d', name: 'TaskGroup timeouts in the ingest runner', backend: 'root' })
    expect(rows.at(-1)).toMatchObject({ id: '20260928-101500-5c3a', name: 'asyncio timeouts in the scraper' })
  })
})

describe("a saved conversation's details, as chats info types them (the Mac app's ChatDetails)", () => {
  test("chats-info.json: the details sheet's fields under the text's own labels, never compacted", () => {
    const details = ChatDetails.of(document(chatsInfo))
    expect(details?.fields).toEqual([
      { label: 'id', value: '20261027-015932-30ca' },
      { label: 'title', value: 'Dev Server Port' },
      { label: 'backend', value: 'actor-port' },
      { label: 'started', value: '2026-10-27T01:59:32Z' },
      { label: 'updated', value: '2026-10-27T01:59:32Z' },
      { label: 'turns', value: '2' },
      { label: 'messages', value: '4' },
      { label: 'compacted', value: 'never' },
    ])
  })

  test('compacted once, it says so as the text says it; any other document is none', () => {
    expect(ChatDetails.of(document(chatsInfoCompacted))?.fields.find((field) => field.label === 'compacted')?.value).toBe('1 time(s)')
    expect(ChatDetails.of(document(chatsList))).toBeUndefined()
  })
})

describe("what recall holds, as chats recall lists it (the Mac app's RecallListing)", () => {
  test('chats-recall.json: each summary, newest first, and the count line the text says', () => {
    const listing = RecallListing.of(document(chatsRecall))
    expect(listing?.summaries.map((summary) => summary.title)).toEqual(['Launch Emails', 'Dev Server Port', 'Release Checklist'])
    expect(listing?.countLine).toBe('recall holds 3 chat summaries in /Users/someone/.apogee/memory/chats.db')
    expect(listing?.holdsAny).toBe(true)
  })

  test('empty, it holds nothing to forget, and one summary is said in the singular', () => {
    const empty = RecallListing.of(document(chatsRecallEmpty))
    expect(empty?.holdsAny).toBe(false)
    expect(empty?.countLine).toBe('recall holds 0 chat summaries in /Users/someone/.apogee/memory/chats.db')
    const one = RecallListing.of(document('{"data":[{"chat":"a","name":"","summary":"s","updated":"2026-10-27T01:59:33Z"}],"index":"/i","object":"list","pending":2}'))
    expect(one?.countLine).toBe('recall holds 1 chat summary in /i, 2 more due one')
    expect(one?.summaries[0]?.title).toBe('a')
  })

  test("the hand-made listing summarises the eight chats finished before today, the scraper's among them", () => {
    const listing = RecallListing.of(document(chatsRecallHandmade))
    expect(listing?.summaries).toHaveLength(8)
    expect(listing?.summaries.map((summary) => summary.title)).toContain('asyncio timeouts in the scraper')
  })
})
