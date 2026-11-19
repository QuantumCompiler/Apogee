import chatsShowAttached from '@recorded/business/reads/Fixtures/chats-show-attached.json?raw'
import chatsShowWarningsOlder from '@recorded/business/reads/Fixtures/chats-show-warnings-older.json?raw'
import suitesSystem from '@recorded/business/reads/Fixtures/suites-system.json?raw'
import chatsShowKv from '../../../fixtures/business/reads/chats-show-kv-handmade.json?raw'
import { describe, expect, test } from 'vitest'
import { SavedChat } from '../../../source/business/reads/SavedChat'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'

const read = (text: string) => {
  const document = ReadDocument.from(text)
  if (!document) throw new Error('not a document')
  return SavedChat.of(document)
}

describe("a saved chat, as chats show reads it back (the CLI's M27; design 1v)", () => {
  test('chats-show-kv-handmade.json: its turn, user and answer, and the warning the loader gave (55b)', () => {
    const chat = read(chatsShowKv)
    expect(chat?.name).toBe('Where it’s sized')
    expect(chat?.turns.length).toBe(1)
    expect(chat?.turns[0]).toMatchObject({ turn: 1, text: 'Where does llama.cpp decide how big the KV cache is, and what does it cost for gpt-oss-20b?', answered: true, compaction: undefined })
    expect(chat?.turns[0]?.answer).toContain('| 128K | 2.4 GB | 14.5 GB |')
    expect(chat?.warnings).toEqual([{ kind: 'field_dropped', message: 'a message could not be read and was skipped' }])
  })

  test('chats-show-attached.json: what the chat holds attached, with its files and bytes; warnings [] read as none', () => {
    const chat = read(chatsShowAttached)
    expect(chat?.attachments).toEqual([{ name: 'Documents/report.md', files: 1, bytes: 30 }])
    expect(chat?.warnings).toEqual([])
  })

  test('a binary before 55b writes no warnings: undefined, never read as none', () => {
    expect(read(chatsShowWarningsOlder)?.warnings).toBeUndefined()
  })

  test("another read's document is no chat", () => {
    expect(read(suitesSystem)).toBeUndefined()
    expect(SavedChat.arguments('20261119-111551-8425')).toEqual(['chats', 'show', '20261119-111551-8425'])
  })
})
