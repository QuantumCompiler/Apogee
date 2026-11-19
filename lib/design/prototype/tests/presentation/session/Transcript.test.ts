import executePlay from '@recorded/data/protocol/Fixtures/execute-play.jsonl?raw'
import executeProject from '@recorded/data/protocol/Fixtures/execute-project.jsonl?raw'
import markdownAnswer from '@recorded/data/protocol/Fixtures/markdown-answer.jsonl?raw'
import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import privateUnsent from '@recorded/data/protocol/Fixtures/private-unsent.jsonl?raw'
import setSuiteRefused from '@recorded/data/protocol/Fixtures/set-suite-refused.jsonl?raw'
import stoppedTurn from '@recorded/data/protocol/Fixtures/stopped-turn.jsonl?raw'
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
    }
  })

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
