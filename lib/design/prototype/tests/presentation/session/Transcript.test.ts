import executePlay from '@recorded/data/protocol/Fixtures/execute-play.jsonl?raw'
import executeProject from '@recorded/data/protocol/Fixtures/execute-project.jsonl?raw'
import markdownAnswer from '@recorded/data/protocol/Fixtures/markdown-answer.jsonl?raw'
import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import privateUnsent from '@recorded/data/protocol/Fixtures/private-unsent.jsonl?raw'
import setSuiteRefused from '@recorded/data/protocol/Fixtures/set-suite-refused.jsonl?raw'
import stoppedTurn from '@recorded/data/protocol/Fixtures/stopped-turn.jsonl?raw'
import permissionCancelled from '@recorded/data/protocol/Fixtures/permission-cancelled.jsonl?raw'
import chatError from '../../../fixtures/data/protocol/chat-error-handmade.jsonl?raw'
import permissionYes from '../../../fixtures/data/protocol/chat-permission-yes.jsonl?raw'
import questionBatch from '../../../fixtures/data/protocol/chat-question-batch-handmade.jsonl?raw'
import sourcesStream from '../../../fixtures/data/protocol/chat-sources.jsonl?raw'
import streaming from '../../../fixtures/data/protocol/chat-streaming-handmade.jsonl?raw'
import resumedKv from '../../../fixtures/data/protocol/resumed-kv-handmade.jsonl?raw'
import resumedKvPair from '../../../fixtures/data/protocol/resumed-kv-pair-handmade.jsonl?raw'
import chatsShowKv from '../../../fixtures/business/reads/chats-show-kv-handmade.json?raw'
import { SavedChat } from '../../../source/business/reads/SavedChat'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'
import { describe, expect, test } from 'vitest'
import { SessionEnd } from '../../../source/business/session/ChatSession'
import type { SessionEvent } from '../../../source/business/session/ChatSession'
import { EventDecoder } from '../../../source/data/protocol/EventDecoder'
import { BoundedTail } from '../../../source/infrastructure/process/BoundedTail'
import { ChildExit } from '../../../source/infrastructure/process/ChildExit'
import { LineFramer } from '../../../source/infrastructure/process/LineFramer'
import { Transcript } from '../../../source/presentation/session/ViewModels/Transcript'
import type { TranscriptItem } from '../../../source/presentation/session/ViewModels/Transcript'

/** Each line of a recording, as the session says it. */
const events = (recording: string): SessionEvent[] => LineFramer.lines(recording).map((line) => ({ case: 'event', event: EventDecoder.decode(line) }))

/** A transcript folded from `recording`, `typed` sent as each turn when the turn's first line arrives, as a person sends between turns. */
function folded(recording: string, typed: readonly string[] = []): Transcript {
  const transcript = new Transcript()
  let sent = 0
  for (const event of events(recording)) {
    const turn = event.case === 'event' ? event.event.turn : undefined
    if (turn !== undefined && turn > sent && typed[sent] !== undefined) {
      transcript.userSent(typed[sent] ?? '')
      sent += 1
    }
    transcript.apply(event)
  }
  return transcript
}

/** What each row is, in a few words. */
const rows = (items: readonly TranscriptItem[]) =>
  items.map((item) => {
    const content = item.content
    switch (content.case) {
      case 'user':
      case 'notice':
        return `${content.case}: ${content.text}`
      case 'answer':
        return `answer${content.answer.finished ? '' : ' (streaming)'}: ${content.answer.text}`
      case 'activity':
        return `activity${content.activity.live ? ' (live)' : ''}: ${content.activity.lines.length} lines`
      case 'unknown':
        return `unknown: ${content.type}`
      case 'ended':
        return `ended: ${content.ending.message}`
      case 'stopped':
        return 'stopped'
      case 'warning':
        return `warning: ${content.text}`
      case 'divider':
        return `divider: ${content.text}`
      case 'recalled':
        return `recalled: ${content.recall.chats.map((chat) => chat.name).join(', ')}`
      case 'sources':
        return `sources: ${content.sources.map((source) => source.file).join(', ')}`
      case 'question':
        return `question${content.card.closed ? ' (closed)' : content.card.answers ? ` → ${content.card.answers.join('; ')}` : ''}: ${content.card.question.items[0]?.question ?? ''}`
      case 'permission':
        return `permission${content.card.closed ? ' (closed)' : content.card.answer ? ` → ${content.card.answer}` : ''}: ${content.card.question.tool ?? ''} ${content.card.question.target ?? ''}`
      case 'error':
        return `error: ${content.message.split('\n')[0] ?? ''}`
      case 'notSent':
        return `not sent: ${content.text}`
      case 'unanswered':
        return 'unanswered'
      case 'todo':
        return `todo: ${content.items.map((item) => item.text).join(', ')}`
    }
  })

/** The events of `recording` up to and including the first of type `type`. */
const upTo = (recording: string, type: string): SessionEvent[] => {
  const all = events(recording)
  const at = all.findIndex((event) => event.case === 'event' && event.event.type === type)
  return all.slice(0, at + 1)
}

describe('the transcript: a pure reducer over the session’s events (the Mac app’s Transcript)', () => {
  test('new-unsent.jsonl: the session event names the model and what it accepts, and draws no row; the input is the person’s', () => {
    const transcript = folded(newUnsent)
    expect(transcript.items).toEqual([])
    expect(transcript.status).toEqual({ case: 'ready' })
    expect(transcript.acceptsInput).toBe(true)
    expect(transcript.model).toBe('claude')
    expect(transcript.accepts).toContain('set')
    expect(transcript.isPrivate).toBe(false)
  })

  test('private-unsent.jsonl: private, and the CLI’s own notice kept as a row', () => {
    const transcript = folded(privateUnsent)
    expect(transcript.isPrivate).toBe(true)
    expect(rows(transcript.items)).toEqual(['notice: private: nothing of this chat is written -- no transcript, no recall'])
  })

  test('before the session speaks it is connecting, and takes no turn', () => {
    const transcript = new Transcript()
    expect(transcript.status).toEqual({ case: 'connecting' })
    expect(transcript.acceptsInput).toBe(false)
  })

  test('markdown-answer.jsonl: the turn’s activity, then the answer exactly its deltas joined, finished by the result', () => {
    const recorded = events(markdownAnswer)
    const result = recorded.at(-1)
    if (result?.case !== 'event' || result.event.kind.case !== 'result') throw new Error('the recording ends in its result')
    const transcript = new Transcript()
    transcript.apply(recorded[0] as SessionEvent)
    expect(transcript.userSent('How do I keep the batch going?')).toBe(1)
    expect(transcript.status).toEqual({ case: 'working', turn: 1 })
    for (const event of recorded.slice(1, 4)) transcript.apply(event)
    expect(transcript.status).toEqual({ case: 'answering', turn: 1 })
    expect(rows(transcript.items).slice(0, 2)).toEqual(['user: How do I keep the batch going?', 'activity: 0 lines'])
    for (const event of recorded.slice(4, -2)) transcript.apply(event)
    const streaming = transcript.items.at(-1)?.content
    expect(streaming?.case === 'answer' && !streaming.answer.finished).toBe(true)
    for (const event of recorded.slice(-2)) transcript.apply(event)
    const answer = transcript.items.at(-1)?.content
    expect(answer?.case === 'answer' ? answer.answer.text : undefined).toBe(result.event.kind.result.text)
    expect(answer?.case === 'answer' ? answer.answer.finished : undefined).toBe(true)
    expect(transcript.status).toEqual({ case: 'ready' })
    expect(transcript.model).toBe('chatty')
    expect(answer?.case === 'answer' ? answer.answer.model : undefined, 'the footer names the model the result says answered').toBe('chatty')
    expect(transcript.answeredTurns).toBe(1)
  })

  test('execute-play.jsonl: the stage lines are the activity’s, kept whole, and narrate the play typed', () => {
    const transcript = folded(executePlay, ['/play echo2 hello'])
    const activity = transcript.items.find((item) => item.content.case === 'activity')?.content
    if (activity?.case !== 'activity') throw new Error('the play has its activity')
    expect(activity.activity.lines).toEqual(['stage 1/2 first — asking utility (helper): One: hello', 'stage 2/2 second — asking chat (root): Two: HELPER<One: hello>'])
    expect(activity.activity.typed).toBe('/play echo2 hello')
    expect(activity.activity.live).toBe(false)
    expect(Transcript.play(activity.activity)?.summary(false)).toBe('played echo2 · 2 stages')
  })

  test('execute-project.jsonl: the launch’s notices before the turn, the turn’s own after the person’s line', () => {
    const transcript = folded(executeProject, ['hi'])
    expect(rows(transcript.items).slice(0, 6)).toEqual([
      'notice: persona: coder, the shipped coding prompt, in every request -- -s replaces it',
      'notice: project context: AGENTS.md, 11 words, in every request',
      'notice: attaching . (2 files, 89 bytes)',
      'user: hi',
      expect.stringMatching(/^notice: attached \.: 2 files/),
      'notice: graph: 2 nodes, 1 edge (supported: c 1; skipped: .md 1)',
    ])
  })

  test('a turn written before the session spoke is drawn where the child reads it: after its launch lines, before its turn', () => {
    const transcript = new Transcript()
    transcript.userWroteAhead('hi')
    expect(transcript.items).toEqual([])
    for (const event of events(executeProject)) transcript.apply(event)
    expect(rows(transcript.items)[3]).toBe('user: hi')
    expect(transcript.items[3]?.turn).toBe(1)
    expect(transcript.status).toEqual({ case: 'ready' })
  })

  test('two turns written ahead each land before their own turn: stopped-turn.jsonl’s answered turn, then its stopped one', () => {
    const transcript = new Transcript()
    transcript.userWroteAhead('Which port does the dev server use?')
    transcript.userWroteAhead('When is the launch?')
    for (const event of events(stoppedTurn).slice(0, 14)) transcript.apply(event)
    expect(rows(transcript.items)).toEqual([
      'user: Which port does the dev server use?',
      'activity: 0 lines',
      'answer: The dev server listens on port 8080.',
      'user: When is the launch?',
      'activity: 0 lines',
      'answer: The laun',
      'stopped',
    ])
  })

  test('a turn written ahead that its child never read is not drawn as sent', () => {
    const transcript = new Transcript()
    transcript.userWroteAhead('hi')
    transcript.apply({ case: 'ended', end: SessionEnd.of({ case: 'failed' }, ChildExit.exited(1), BoundedTail.of('apogee chat: refused\n')) })
    expect(rows(transcript.items)).toEqual(['ended: apogee chat: refused'])
  })

  test('stopped-turn.jsonl: a cancel stops the turn until its result says it stopped, then a quiet stopped row', () => {
    const recorded = events(stoppedTurn)
    const transcript = new Transcript()
    for (const event of recorded.slice(0, 1)) transcript.apply(event)
    transcript.userSent('Which port does the dev server use?')
    for (const event of recorded.slice(1, 10)) transcript.apply(event)
    expect(transcript.status).toEqual({ case: 'ready' })
    transcript.userSent('When is the launch?')
    for (const event of recorded.slice(10, 12)) transcript.apply(event)
    expect(transcript.turnInFlight).toBe(true)
    transcript.cancelling()
    expect(transcript.status).toEqual({ case: 'stopping', turn: 2 })
    transcript.apply(recorded[12] as SessionEvent)
    expect(transcript.status, 'the delta keeps it stopping').toEqual({ case: 'stopping', turn: 2 })
    transcript.apply(recorded[13] as SessionEvent)
    expect(rows(transcript.items).slice(-2)).toEqual(['answer: The laun', 'stopped'])
    expect(transcript.status).toEqual({ case: 'ready' })
    expect(transcript.answeredTurns).toBe(1)
  })

  test('set-suite-refused.jsonl: a notice between turns is a row of its own', () => {
    const transcript = folded(setSuiteRefused, ['hello'])
    expect(rows(transcript.items)[0]).toBe(
      "notice: set: 'suite' is not a setting a session moves, as text -- model, system, think, effort or temperature; nothing was set",
    )
  })

  test('a tool_call is a named skip: its call is the tool_status line, so it adds no row (as the Mac app skips it)', () => {
    const transcript = folded(newUnsent)
    transcript.userSent('hi')
    transcript.apply({ case: 'event', event: EventDecoder.decode('{"type":"tool_call","id":"call-1","name":"read_file","state":"running","turn":1}') })
    expect(rows(transcript.items)).toEqual(['user: hi'])
  })

  test('a type this app does not know is kept as a quiet row, never guessed -- and so is a line that is not JSON', () => {
    const transcript = folded(newUnsent)
    transcript.apply({ case: 'event', event: EventDecoder.decode('{"type":"sparkle","turn":1}') })
    transcript.apply({ case: 'event', event: EventDecoder.decode('{"type":"sources","turn":1}') })
    transcript.apply({ case: 'event', event: EventDecoder.decode('not json') })
    expect(rows(transcript.items)).toEqual(['unknown: sparkle', 'unknown: sources', 'unknown: a line that is not JSON'])
  })

  test('its end: the session ended row in the CLI’s words, and a launch refused before the session spoke said in its own', () => {
    const ended = folded(newUnsent)
    ended.apply({ case: 'ended', end: SessionEnd.of({ case: 'finished' }, ChildExit.exited(0), new BoundedTail()) })
    expect(rows(ended.items)).toEqual(['ended: the session ended'])
    expect(ended.status).toEqual({ case: 'ended' })
    expect(ended.launchRefusal).toBeUndefined()

    const refused = new Transcript()
    refused.apply({ case: 'ended', end: SessionEnd.of({ case: 'failed' }, ChildExit.exited(1), BoundedTail.of('apogee chat: the model "nope" is not configured\n')) })
    expect(refused.launchRefusal?.message).toBe('apogee chat: the model "nope" is not configured')
    expect(refused.launchRefusal?.atLaunch).toBe(true)
  })

  test('every change moves the revision, which a view follows', () => {
    const transcript = new Transcript()
    const before = transcript.revision
    for (const event of events(privateUnsent)) transcript.apply(event)
    expect(transcript.revision).toBeGreaterThan(before)
  })
})

describe("the transcript's scenarios and states (design 1v, the Mac app's Transcript)", () => {
  test('chat-permission-yes.jsonl: the permission gate is a card that waits on the person; answered, the turn works on', () => {
    const transcript = new Transcript()
    transcript.userSent('Make _fetch() retry.')
    for (const event of upTo(permissionYes, 'question')) transcript.apply(event)
    expect(rows(transcript.items).at(-1)).toBe('permission: write_file src/ingest/runner.py')
    expect(transcript.status).toEqual({ case: 'waiting', turn: 1 })
    const card = transcript.standingCard
    expect(card).toBe(transcript.items.at(-1)?.id)
    expect(transcript.answered(card ?? -1, ['no'])).toBe(true)
    expect(rows(transcript.items).at(-1)).toBe('permission → no: write_file src/ingest/runner.py')
    expect(transcript.status).toEqual({ case: 'working', turn: 1 })
    expect(transcript.standingCard).toBeUndefined()
    expect(transcript.answered(card ?? -1, ['yes']), 'one answered takes no other').toBe(false)
  })

  test('chat-question-batch-handmade.jsonl: the lead streamed before ask_user, then the question card; answered with one answer per item', () => {
    const transcript = new Transcript()
    transcript.userSent('Review the retry-budget change in PR 482 before I merge it.')
    for (const event of upTo(questionBatch, 'question')) transcript.apply(event)
    expect(rows(transcript.items).slice(-2)).toEqual([
      'answer (streaming): The change adds a `RetryBudget` that caps retries at 20 per minute. One thing decides whether it’s right:',
      'question: Should the retry budget apply per upload, per batch, or per host?',
    ])
    expect(transcript.answered(transcript.standingCard ?? -1, ['Per batch'])).toBe(true)
    expect(rows(transcript.items).at(-1)).toBe('question → Per batch: Should the retry budget apply per upload, per batch, or per host?')
  })

  test('permission-cancelled.jsonl: a card its turn ended before an answer is closed, and takes none', () => {
    const transcript = folded(permissionCancelled, ['Write me a note'])
    expect(rows(transcript.items).filter((row) => row.startsWith('question') || row.startsWith('permission'))).toEqual([
      'question (closed): What should the note be called?',
      'permission (closed): write_file todo.md',
    ])
    expect(transcript.standingCard).toBeUndefined()
  })

  test('chat-sources.jsonl: the excerpts a turn drew on, a row in the order sent', () => {
    const transcript = folded(sourcesStream, ['Why does group commit make the WAL cheaper under load? Point me to the pages.'])
    expect(rows(transcript.items).filter((row) => row.startsWith('sources'))).toEqual(['sources: storage-engine-design.pdf, postgresql.conf, storage-engine-design.pdf'])
  })

  test('chat-streaming-handmade.jsonl: what turn 2 recalled, named', () => {
    const transcript = folded(streaming, ['one', 'two'])
    expect(rows(transcript.items).filter((row) => row.startsWith('recalled'))).toEqual(['recalled: asyncio timeouts in the scraper'])
    const recall = transcript.items.find((item) => item.content.case === 'recalled')
    expect(recall?.content.case === 'recalled' ? recall.content.recall : undefined).toEqual({ chats: [{ id: '20260928-101500-5c3a', name: 'asyncio timeouts in the scraper', updated: '2026-09-28T10:41:07Z' }], decisions: 0 })
  })

  test('in the CLI’s order -- the rewrite, the sources, the recall, then the reasoning -- turn 2 keeps one activity: the recall and sources rows between are looked past', () => {
    // chat_turn.cpp emits a turn's retrieval and its recall before the model's thinking; put in that order whatever order the file holds.
    const lines = LineFramer.lines(streaming)
    const memory = lines.find((line) => EventDecoder.decode(line).type === 'memory') ?? ''
    const rest = lines.filter((line) => line !== memory)
    const sources = rest.findIndex((line) => { const event = EventDecoder.decode(line); return event.type === 'sources' && event.turn === 2 })
    const ordered = [...rest.slice(0, sources + 1), memory, ...rest.slice(sources + 1)].join('\n')
    const transcript = folded(ordered, ['one', 'two'])
    const turnTwo = transcript.items.filter((item) => item.turn === 2)
    const activities = turnTwo.filter((item) => item.content.case === 'activity')
    expect(activities).toHaveLength(1)
    const activity = activities[0]?.content
    expect(activity?.case === 'activity' ? activity.activity.lines : []).toEqual(['utility — rewriting the follow-up into a search query with root', '[tool] fetch_url', '[tool] read_file'])
    expect(activity?.case === 'activity' ? activity.activity.reasoning.length : 0).toBeGreaterThan(0)
    expect(turnTwo.map((item) => item.content.case).slice(0, 4)).toEqual(['user', 'activity', 'sources', 'recalled'])
  })

  test('a memory event before 52a counts its chats in a notice, naming none', () => {
    const transcript = folded(newUnsent)
    transcript.apply({ case: 'event', event: EventDecoder.decode('{"chats":2,"decisions":1,"turn":1,"type":"memory"}') })
    expect(rows(transcript.items)).toEqual(['notice: recalled 2 earlier chats and 1 decision'])
  })

  test('chat-error-handmade.jsonl: the turn failed, in the CLI’s words; the input is the person’s again', () => {
    const transcript = folded(chatError, ['What’s this stack trace telling me?'])
    expect(rows(transcript.items).at(-1)).toBe("error: vast: could not load the model at '/Users/someone/.apogee/models/vast/gguf/eeeeeeeeeeee/vast-F16.gguf' -- check that the file exists and is a valid GGUF")
    expect(transcript.status).toEqual({ case: 'ready' })
  })

  test('an error keeps the model the session ran on when it came: a later answer by another model leaves it as it was', () => {
    const transcript = folded(chatError, ['What’s this stack trace telling me?'])
    transcript.apply({ case: 'event', event: EventDecoder.decode('{"finish_reason":"stop","model":"claude","text":"x","turn":2,"type":"result"}') })
    expect(transcript.model).toBe('claude')
    const error = transcript.items.find((item) => item.content.case === 'error')
    expect(error?.content).toMatchObject({ case: 'error', model: 'vast' })
  })

  test('a plan is drawn in place, not logged', () => {
    const transcript = folded(newUnsent)
    transcript.apply({ case: 'event', event: EventDecoder.decode('{"items":[{"state":"active","text":"read"}],"turn":1,"type":"todo"}') })
    transcript.apply({ case: 'event', event: EventDecoder.decode('{"items":[{"state":"done","text":"read"},{"state":"active","text":"write"}],"turn":1,"type":"todo"}') })
    expect(rows(transcript.items)).toEqual(['todo: read, write'])
  })

  test('restarting: nothing moves it but the next child; a result or an error arriving meanwhile leaves it restarting', () => {
    const transcript = folded(newUnsent)
    transcript.restarting()
    expect(transcript.status).toEqual({ case: 'restarting' })
    transcript.apply({ case: 'event', event: EventDecoder.decode('{"finish_reason":"stop","model":"claude","text":"x","turn":1,"type":"result"}') })
    expect(transcript.status).toEqual({ case: 'restarting' })
    expect(transcript.acceptsInput).toBe(false)
  })

  test('a child being stopped says nothing of the suite the session moves to: its late session line leaves the move as it was', () => {
    const transcript = new Transcript()
    transcript.forgetSuite()
    transcript.restarting()
    for (const event of events(resumedKv)) transcript.apply(event)
    expect(transcript.suite).toBeUndefined()
    expect(transcript.status).toEqual({ case: 'restarting' })
  })

  test('reopening: the next child folds into a range of its own, and a launch notice it says again is drawn once', () => {
    const transcript = folded(resumedKv)
    expect(rows(transcript.items)).toEqual(['notice: resume: a message could not be read and was skipped'])
    transcript.restarting()
    transcript.reopening()
    expect(transcript.status).toEqual({ case: 'connecting' })
    for (const event of events(resumedKvPair)) transcript.apply(event)
    expect(rows(transcript.items)).toEqual(['notice: resume: a message could not be read and was skipped', 'notice: suite pair: no member holds memory on this machine'])
    expect(transcript.suite).toEqual({ case: 'named', name: 'pair' })
    expect(new Set(transcript.items.map((item) => item.range))).toEqual(new Set([1, 2]))
  })

  test('not sent: the turn drawn as sent, its line refused, marked beneath it', () => {
    const transcript = folded(resumedKv)
    transcript.userSent('Would a 16-bit cache fit at 128K?')
    transcript.notSent('Would a 16-bit cache fit at 128K?')
    expect(rows(transcript.items).slice(-2)).toEqual(['user: Would a 16-bit cache fit at 128K?', 'not sent: Would a 16-bit cache fit at 128K?'])
  })

  test('a turn refused before the child spoke is drawn where the child would have read it: after its launch lines, once it is going', () => {
    const transcript = new Transcript()
    transcript.userWroteAhead('Would a 16-bit cache fit at 128K?')
    transcript.restarting()
    transcript.refused('Would a 16-bit cache fit at 128K?')
    expect(rows(transcript.items)).toEqual([])
    for (const event of events(resumedKv)) transcript.apply(event)
    transcript.reopening()
    expect(rows(transcript.items)).toEqual(['notice: resume: a message could not be read and was skipped', 'user: Would a 16-bit cache fit at 128K?', 'not sent: Would a 16-bit cache fit at 128K?'])
    expect(transcript.writtenAhead).toBe(false)
  })

  test('chats-show-kv-handmade.json read back: its warning, its turn, the divider -- into an empty transcript only, before the session speaks', () => {
    const chat = SavedChat.of(ReadDocument.from(chatsShowKv) ?? (undefined as never))
    if (!chat) throw new Error('no chat')
    const transcript = new Transcript()
    transcript.earlier(chat)
    for (const event of events(resumedKv)) transcript.apply(event)
    expect(rows(transcript.items)).toEqual([
      'warning: a message could not be read and was skipped',
      'user: Where does llama.cpp decide how big the KV cache is, and what does it cost for gpt-oss-20b?',
      expect.stringMatching(/^answer: ### Where it’s sized/),
      'divider: earlier in this conversation',
      'notice: resume: a message could not be read and was skipped',
    ])
    expect(transcript.items.slice(0, 4).map((item) => item.range)).toEqual([0, 0, 0, 0])
    const again = folded(resumedKv)
    again.earlier(chat)
    expect(rows(again.items), 'a transcript with rows takes none').toEqual(['notice: resume: a message could not be read and was skipped'])
  })

  test('its end closes a card left standing, and finishes an answer cut mid-stream', () => {
    const transcript = new Transcript()
    transcript.userSent('Make _fetch() retry.')
    for (const event of upTo(permissionYes, 'question')) transcript.apply(event)
    transcript.apply({ case: 'ended', end: SessionEnd.of({ case: 'failed' }, ChildExit.exited(2), BoundedTail.of('loading\nfatal: the model went away\n')) })
    expect(rows(transcript.items).slice(-2)).toEqual(['permission (closed): write_file src/ingest/runner.py', 'ended: apogee exited with status 2: fatal: the model went away'])
  })
})
