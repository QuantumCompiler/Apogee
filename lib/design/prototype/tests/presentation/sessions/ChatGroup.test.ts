import { describe, expect, test } from 'vitest'
import chatsListHandmade from '../../../fixtures/business/reads/chats-list-handmade.json?raw'
import { ChatSummary } from '../../../source/business/reads/Chats'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { ChatGroup } from '../../../source/presentation/sessions/ViewModels/ChatGroup'

/** The stand-in's fixed "now": 2026-11-14T12:00:00Z, a Saturday. */
const now = new Date(Clock.defaultNow)
const chats = ChatSummary.list(ReadDocument.from(chatsListHandmade) as ReadDocument)
const chat = (updated: string | undefined): ChatSummary => ({ id: 'x', name: 'x', updated, turns: 1, backend: undefined, model: undefined })

describe("the list's date groups, measured against the stand-in's fixed now (the Mac's ChatGroup.grouping and when(_:))", () => {
  test('the recorded chats fall into their date groups, in the design’s order: Today 5, Yesterday 3, Previous 7 days 3, Earlier 2', () => {
    const groups = ChatGroup.grouping(chats, now)
    expect(groups.map((group) => [group.title, group.chats.length])).toEqual([
      ['Today', 5],
      ['Yesterday', 3],
      ['Previous 7 days', 3],
      ['Earlier', 2],
    ])
    expect(groups[0]?.chats.map((row) => row.name)).toEqual([
      'TaskGroup timeouts in the ingest runner',
      'Make fetch retries configurable',
      'Group commit, from the storage notes',
      'What’s in this crash screenshot?',
      'The board’s minutes, summarized',
    ])
  })

  test('each day is a calendar day in the zone of "now" (UTC here), never the viewer’s: one minute past midnight is today, one minute before it yesterday', () => {
    expect(ChatGroup.of(chat('2026-11-14T00:01:00Z'), now)).toBe('Today')
    expect(ChatGroup.of(chat('2026-11-13T23:59:00Z'), now)).toBe('Yesterday')
    expect(ChatGroup.of(chat('2026-11-12T08:00:00Z'), now)).toBe('Previous 7 days')
    expect(ChatGroup.of(chat('2026-11-07T08:00:00Z'), now)).toBe('Previous 7 days')
    expect(ChatGroup.of(chat('2026-11-06T23:00:00Z'), now)).toBe('Earlier')
    expect(ChatGroup.of(chat(undefined), now), 'a row with no readable date is Earlier').toBe('Earlier')
  })

  test('an empty group is left out', () => {
    expect(ChatGroup.grouping([chat('2026-11-14T09:00:00Z')], now).map((group) => group.title)).toEqual(['Today'])
  })

  test('when a row last changed, as the list shows it: the time today or yesterday, the weekday within the week, the date before that', () => {
    expect(chats.map((row) => ChatGroup.when(row, now))).toEqual(['14:03', '13:40', '11:52', '10:18', '09:40', '17:26', '16:47', '10:15', 'Tue', 'Mon', 'Sun', 'Oct 2', 'Sep 28'])
    expect(ChatGroup.when(chat(undefined), now)).toBe('')
  })
})
