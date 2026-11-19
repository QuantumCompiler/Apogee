import { describe, expect, test, vi } from 'vitest'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import launchRefusedModel from '@recorded/data/protocol/Fixtures/launch-refused-model.err?raw'
import launchRefusedModelExit from '@recorded/data/protocol/Fixtures/launch-refused-model.exit?raw'
import mockActor from '@recorded/data/protocol/Fixtures/mock-actor.jsonl?raw'
import questionCancelled from '@recorded/data/protocol/Fixtures/question-cancelled.jsonl?raw'
import setModel from '@recorded/data/protocol/Fixtures/set-model.jsonl?raw'
import stoppedTurn from '@recorded/data/protocol/Fixtures/stopped-turn.jsonl?raw'
import chatErrorRetry from '../../../fixtures/data/protocol/chat-error-retry-handmade.jsonl?raw'
import chatErrorSwitch from '../../../fixtures/data/protocol/chat-error-switch-handmade.jsonl?raw'
import chatError from '../../../fixtures/data/protocol/chat-error-handmade.jsonl?raw'
import permissionAlways from '../../../fixtures/data/protocol/chat-permission-always.jsonl?raw'
import permissionNo from '../../../fixtures/data/protocol/chat-permission-no.jsonl?raw'
import permissionSession from '../../../fixtures/data/protocol/chat-permission-session.jsonl?raw'
import permissionYes from '../../../fixtures/data/protocol/chat-permission-yes.jsonl?raw'
import permissionCancel from '../../../fixtures/data/protocol/chat-permission-cancel.jsonl?raw'
import questionBatch from '../../../fixtures/data/protocol/chat-question-batch-handmade.jsonl?raw'
import questionCancel from '../../../fixtures/data/protocol/chat-question-cancel.jsonl?raw'
import streamingCancel from '../../../fixtures/data/protocol/chat-streaming-cancel.jsonl?raw'
import streaming from '../../../fixtures/data/protocol/chat-streaming-handmade.jsonl?raw'
import resumedKvEnded from '../../../fixtures/data/protocol/resumed-kv-ended-handmade.jsonl?raw'
import resumedKv from '../../../fixtures/data/protocol/resumed-kv-handmade.jsonl?raw'
import resumedKvPair from '../../../fixtures/data/protocol/resumed-kv-pair-handmade.jsonl?raw'
import { ApogeeClient } from '../../../source/business/session/ApogeeClient'
import { ChatLaunch } from '../../../source/business/session/ChatLaunch'
import { SessionEnd, SessionError } from '../../../source/business/session/ChatSession'
import type { ChatSession, SessionEvent } from '../../../source/business/session/ChatSession'
import { MachineSession } from '../../../source/business/session/MachineSession'
import { EventDecoder } from '../../../source/data/protocol/EventDecoder'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import type { ClockSettings } from '../../../source/infrastructure/clock/Clock'
import { BoundedTail } from '../../../source/infrastructure/process/BoundedTail'
import { ChildExit } from '../../../source/infrastructure/process/ChildExit'
import { LineFramer } from '../../../source/infrastructure/process/LineFramer'
import { recordedExit, StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { Answer, ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'

const binary = StandInChild.binary
const chatRules: Rules = { chats: [{ arguments: ['chat'], answer: { stdout: mockActor } }] }

function client(rules: Rules = chatRules, clockSettings: ClockSettings = {}, apogee: ApogeeMode = 'installed') {
  const child = new StandInChild({ rules, apogee }, { configTypoRefusal: agentsListBadConfig })
  const clock = new Clock(clockSettings)
  return { chats: new ApogeeClient(child, clock), clock, child }
}

/** The types the session has said so far, its end as `ended: <message>`. */
const said = (session: ChatSession) =>
  (session.events as MachineSession['events']).yielded.map((event: SessionEvent) =>
    event.case === 'event' ? event.event.type : `ended: ${SessionEnd.message(event.end)}`,
  )

describe('a chat replays a recorded machine-mode stream', () => {
  const recorded = LineFramer.lines(mockActor)

  test('mock-actor.jsonl (24 lines): the session line first, then the turn a send releases, waiting at each question until answer', async () => {
    const { chats, clock } = client()
    expect(recorded.length).toBe(24)
    const session = chats.openChat(binary)
    expect(said(session)).toEqual([])
    clock.advance(1)
    expect(said(session)).toEqual(['session'])
    clock.advance(5)
    expect(said(session), 'nothing more until a turn is sent').toEqual(['session'])

    expect(await session.send('Write me a note')).toBe(1)
    clock.advance(30)
    // The turn stops at its first question: thinking, 7 deltas, the question.
    expect(said(session)).toEqual(recorded.slice(0, 10).map((line) => EventDecoder.decode(line).type))
    expect(said(session).at(-1)).toBe('question')

    await session.answer('todo.md')
    clock.advance(30)
    // On to the permission gate, the second question.
    expect(said(session).length).toBe(13)
    expect((session.events as MachineSession['events']).yielded.at(-1)).toMatchObject({
      case: 'event',
      event: { kind: { case: 'question', question: { kind: 'permission' } } },
    })

    await session.answer('yes')
    clock.advance(30)
    expect((session.events as MachineSession['events']).yielded).toEqual(
      recorded.map((line) => ({ case: 'event', event: EventDecoder.decode(line) })),
    )
  })

  test('at a faster pace a question still stops the turn where it stands', async () => {
    const { chats, clock } = client(chatRules, { pace: 6 })
    const session = chats.openChat(binary)
    await session.send('Write me a note')
    clock.advance(10)
    expect(said(session).length).toBe(10)
    expect(said(session).at(-1)).toBe('question')
    await session.answer('todo.md')
    clock.advance(1)
    expect(said(session).length).toBe(13)
    expect(said(session).at(-1)).toBe('question')
  })

  test('the lines written are kept, as the binary would read them', async () => {
    const { chats, clock } = client()
    const session = chats.openChat(binary) as MachineSession
    clock.advance(1)
    await session.send('Write me a note')
    await session.attach('notes/', 'off')
    await session.set({ model: 'local' })
    await session.answer('todo.md')
    expect(session.written).toEqual([
      '{"type":"user","text":"Write me a note"}',
      '{"type":"attach","path":"notes/","graph":"off"}',
      '{"type":"set","model":"local"}',
      '{"type":"answer","text":"todo.md"}',
    ])
  })

  test('a turn sent before the session line is numbered from 1 and plays after it; a later send waits its turn', async () => {
    const { chats, clock } = client({ chats: [{ arguments: ['chat'], answer: { stdout: setModel } }] })
    const session = chats.openChat(binary)
    expect(await session.send('first')).toBe(1)
    expect(await session.send('second')).toBe(2)
    clock.advance(200)
    const types = LineFramer.lines(setModel).map((line) => EventDecoder.decode(line).type)
    expect(said(session)).toEqual(types)
    expect(said(session)[0]).toBe('session')
  })

  test('a send past the recording plays nothing, and is logged for the per-preset tests', async () => {
    const { chats, clock, child } = client()
    const session = chats.openChat(binary)
    clock.advance(1)
    await session.send('one')
    await session.answer('todo.md')
    await session.answer('yes')
    clock.advance(40)
    await session.send('two')
    clock.advance(40)
    expect(said(session).length).toBe(24)
    expect(child.unanswered).toEqual(['chats: turn 2 of apogee chat --output-format stream-json --input-format stream-json --tools'])
  })

  test('a send to a child that refused its launch is no turn past a recording: it exits unread, as launch-refused-model.err records, and nothing is logged (design 1u)', async () => {
    const refused = { chats: [{ arguments: ['chat'], answer: { stderr: launchRefusedModel, exit: recordedExit(launchRefusedModelExit) } }] }
    for (const [rules, apogee] of [[refused, 'installed'], [chatRules, 'config-typo']] as const) {
      const { chats, clock, child } = client(rules, {}, apogee)
      const session = chats.openChat(binary)
      await session.send('written before it spoke')
      clock.advance(3)
      expect(said(session).at(-1)).toMatch(/^ended: /)
      expect(child.unanswered, apogee).toEqual([])
    }
  })

  test('the launch is the Mac app\'s: chat or execute, both directions in stream-json', () => {
    expect(ChatLaunch.arguments()).toEqual(['chat', '--output-format', 'stream-json', '--input-format', 'stream-json', '--tools'])
    expect(
      ChatLaunch.arguments({ resume: 'wal', settings: { model: 'local', effort: 'auto', temperature: 0.2 }, execute: { suite: 'fast', orchestrate: true } }),
    ).toEqual([
      'execute', '--output-format', 'stream-json', '--input-format', 'stream-json', '--tools',
      '--resume', 'wal', '-m', 'local', '-t', '0.2', '--suite', 'fast', '--orchestrate',
    ])
    expect(ChatLaunch.arguments({ tools: false, privately: true, suite: ChatLaunch.noSuite, forceSuite: true })).toEqual([
      'chat', '--output-format', 'stream-json', '--input-format', 'stream-json', '--private', '--suite', 'off', '--force',
    ])
  })

  test('a still preset applies its start ticks at once, then does not move', async () => {
    vi.useFakeTimers()
    try {
      const { chats, clock } = client(chatRules, { start: 3, still: true })
      const session = chats.openChat(binary)
      await session.send('Write me a note')
      clock.start()
      expect(said(session)).toEqual(['session', 'thinking', 'thinking_delta'])
      vi.advanceTimersByTime(60_000)
      expect(said(session)).toEqual(['session', 'thinking', 'thinking_delta'])
    } finally {
      vi.useRealTimers()
    }
  })

  test('a chat opened after start() in a still frame has played its opening; a send written after start() waits for ticks that never come', async () => {
    vi.useFakeTimers()
    try {
      const { chats, clock } = client(chatRules, { start: 3, still: true })
      clock.start()
      await Promise.resolve()
      const session = chats.openChat(binary)
      expect(said(session)).toEqual(['session'])
      await session.send('Write me a note')
      vi.advanceTimersByTime(60_000)
      expect(said(session)).toEqual(['session'])
    } finally {
      vi.useRealTimers()
    }
  })
})

describe('how a chat ends', () => {
  test('close ends it the protocol\'s way: finished, and nothing more can be written', async () => {
    const { chats, clock } = client()
    const session = chats.openChat(binary)
    clock.advance(1)
    await session.close()
    expect(said(session)).toEqual(['session', 'ended: the session ended'])
    await expect(session.send('more')).rejects.toThrow('the session has ended, or its input is closed')
  })

  test('stop cancels a turn in flight, then closes; terminateNow is a stop', async () => {
    const { chats, clock } = client()
    const session = chats.openChat(binary) as MachineSession
    clock.advance(1)
    await session.send('Write me a note')
    clock.advance(3)
    await session.stop()
    expect(session.written.at(-1)).toBe('{"type":"cancel"}')
    expect(said(session).at(-1)).toBe('ended: the session ended')

    const other = client()
    const stopped = other.chats.openChat(binary)
    other.clock.advance(1)
    await stopped.terminateNow()
    expect(said(stopped).at(-1)).toBe('ended: apogee did not end when asked and was stopped')
  })

  test('stop after a turn\'s result has played writes no cancel: no turn is in flight (stopped-turn.jsonl)', async () => {
    const { chats, clock } = client({ chats: [{ arguments: ['chat'], answer: { stdout: stoppedTurn } }] })
    const session = chats.openChat(binary) as MachineSession
    clock.advance(1)
    await session.send('one')
    // Turn 1 is 9 lines, its result last.
    clock.advance(9)
    expect((session.events as MachineSession['events']).yielded.at(-1)).toMatchObject({
      case: 'event',
      event: { kind: { case: 'result', result: { finishReason: 'stop' } } },
    })
    await session.stop()
    expect(session.written).toEqual(['{"type":"user","text":"one"}'])
    expect(said(session).at(-1)).toBe('ended: the session ended')
  })

  test('a cancel at a standing question plays the turn\'s recorded result when that is its next line (question-cancelled.jsonl)', async () => {
    const { chats, clock } = client({ chats: [{ arguments: ['chat'], answer: { stdout: questionCancelled } }] })
    const session = chats.openChat(binary) as MachineSession
    await session.send('Write me a note')
    clock.advance(10)
    expect(said(session)).toEqual(['session', 'thinking', 'question'])
    await session.cancel()
    clock.advance(10)
    expect(said(session)).toEqual(['session', 'thinking', 'question', 'result'])
    expect((session.events as MachineSession['events']).yielded.at(-1)).toMatchObject({
      case: 'event',
      event: { kind: { case: 'result', result: { finishReason: 'cancelled' } } },
    })
    expect(session.written.at(-1)).toBe('{"type":"cancel"}')
  })

  test('a cancel at a question the recording answered leaves the turn parked there (mock-actor.jsonl)', async () => {
    const { chats, clock } = client()
    const session = chats.openChat(binary) as MachineSession
    await session.send('Write me a note')
    clock.advance(30)
    expect(said(session).at(-1)).toBe('question')
    const before = said(session).length
    await session.cancel()
    clock.advance(30)
    expect(said(session).length).toBe(before)
    expect(said(session).at(-1)).toBe('question')
  })

  test('a refused launch ends it failed, in the CLI\'s words', () => {
    const { chats, clock } = client({
      chats: [{ arguments: ['chat'], answer: { stderr: launchRefusedModel, exit: recordedExit(launchRefusedModelExit) } }],
    })
    const session = chats.openChat(binary)
    clock.advance(1)
    expect((session.events as MachineSession['events']).yielded).toEqual([
      { case: 'ended', end: SessionEnd.of({ case: 'failed' }, ChildExit.exited(1), BoundedTail.of(launchRefusedModel)) },
    ])
    expect(said(session)).toEqual([`ended: apogee exited with status 1: ${BoundedTail.of(launchRefusedModel).lastLine}`])
  })

  test('a stream announcing a protocol this client does not read is refused, not misread', () => {
    // Planted for the test alone: no binary has spoken protocol 2.
    const planted = '{"type":"session","protocol_version":2,"model":"actor"}\n{"type":"thinking","turn":1}\n'
    const { chats, clock } = client({ chats: [{ arguments: ['chat'], answer: { stdout: planted } }] })
    const session = chats.openChat(binary)
    clock.advance(5)
    expect(said(session)).toEqual([
      'ended: apogee speaks machine-mode protocol 2; this app reads protocol 1 -- update the app or the CLI',
    ])
  })

  test('nobody answered it, or apogee is missing: it could not start; config-typo: it exits 1 with the refusal', () => {
    const none = client({})
    const session = none.chats.openChat(binary)
    expect(said(session)).toEqual([
      'ended: apogee could not be started: no recorded answer for apogee chat --output-format stream-json --input-format stream-json --tools',
    ])
    const missing = client(chatRules, {}, 'missing')
    expect(said(missing.chats.openChat(binary))).toEqual(['ended: apogee could not be started: apogee is not installed'])

    const typo = client(chatRules, {}, 'config-typo')
    const refused = typo.chats.openChat(binary)
    typo.clock.advance(1)
    expect(said(refused)).toEqual([`ended: apogee exited with status 1: ${agentsListBadConfig.replace('apogee agents:', 'apogee chat:').trim()}`])
  })
})

describe('where a chat\'s binary is', () => {
  test('locateBinary is answered by the mode', async () => {
    expect(await client().chats.locateBinary()).toEqual({ case: 'found', binary: '/Users/someone/.local/bin/apogee' })
    expect(await client(chatRules, {}, 'config-typo').chats.locateBinary()).toEqual({ case: 'found', binary: '/Users/someone/.local/bin/apogee' })
    expect(await client(chatRules, {}, 'missing').chats.locateBinary()).toEqual({ case: 'notFound' })
  })
})

/** The events a session has said, decoded, its end as `ended: <message>`. */
const events = (session: ChatSession) => (session.events as MachineSession['events']).yielded

/** The texts of the answer deltas it has said, joined. */
const answered = (session: ChatSession) =>
  events(session)
    .flatMap((event) => (event.case === 'event' && event.event.kind.case === 'answerDelta' ? [event.event.kind.text] : []))
    .join('')

describe('a freeze at a named event (design 1v)', () => {
  // chat-streaming-handmade.jsonl's second turn reasons in 78 deltas; the 59th is three-quarters in, as the old progress 30 of 40 is.
  const freeze = { name: 'mid-reasoning', event: 'thinking_delta', nth: 59 }
  const rule = (answer: Answer): Rules => ({ chats: [{ arguments: ['chat'], answer }] })
  const turns = ['Where do fetches start in this repo, and what cancels them?', 'So one slow host takes the whole batch down.']

  test('a still frame stands right after it once its start ticks are applied, however few, and after advance(150)', async () => {
    const { chats, clock } = client(rule({ stdout: streaming, freeze }), { start: 3, still: true })
    const session = chats.openChat(binary)
    for (const text of turns) await session.send(text)
    clock.start()
    const deltas = () => events(session).filter((event) => event.case === 'event' && event.event.type === 'thinking_delta').length
    expect(deltas()).toBe(59)
    expect(said(session).at(-1)).toBe('thinking_delta')
    clock.advance(150)
    expect(deltas()).toBe(59)
    expect(said(session).filter((type) => type === 'result')).toEqual(['result'])
  })

  test('on a live clock the replay plays straight to it, then on at its pace: after 60 seconds of fake timers the turn has its result', async () => {
    vi.useFakeTimers()
    try {
      const { chats, clock } = client(rule({ stdout: streaming, freeze }), { start: 3 })
      const session = chats.openChat(binary)
      for (const text of turns) await session.send(text)
      clock.start()
      const at = said(session).length
      expect(events(session).filter((event) => event.case === 'event' && event.event.type === 'thinking_delta').length).toBeGreaterThanOrEqual(59)
      vi.advanceTimersByTime(400)
      expect(said(session).length, 'one line a tick past it').toBe(at + 1)
      vi.advanceTimersByTime(60_000)
      expect(said(session).filter((type) => type === 'result')).toEqual(['result', 'result'])
      expect(said(session).at(-1)).toBe('result')
      clock.stop()
    } finally {
      vi.useRealTimers()
    }
  })

  test('with no freeze a still frame plays only its start ticks, as before', async () => {
    const { chats, clock } = client(rule({ stdout: streaming }), { start: 3, still: true })
    const session = chats.openChat(binary)
    for (const text of turns) await session.send(text)
    clock.start()
    expect(said(session).length).toBe(3)
  })
})

describe('an exit replayed on the clock after the input closes (design 1v)', () => {
  test('resumed-kv-handmade.jsonl with exitAfter 5: closed, it takes no line, and ends finished 5 ticks later', async () => {
    const { chats, clock } = client({ chats: [{ arguments: ['chat'], answer: { stdout: resumedKv, exitAfter: 5 } }] })
    const session = chats.openChat(binary)
    clock.advance(2)
    expect(said(session)).toEqual(['session', 'notice'])
    await session.close()
    await expect(session.send('Would a 16-bit cache fit at 128K?')).rejects.toThrow(SessionError)
    clock.advance(4)
    expect(said(session).at(-1)).toBe('notice')
    clock.advance(1)
    expect(said(session).at(-1)).toBe('ended: the session ended')
  })

  test('stop() takes the same time; terminateNow() ends it at once', async () => {
    const { chats, clock } = client({ chats: [{ arguments: ['chat'], answer: { stdout: resumedKv, exitAfter: 5 } }] })
    const session = chats.openChat(binary)
    clock.advance(2)
    await session.stop()
    clock.advance(3)
    expect(said(session).at(-1)).toBe('notice')
    await session.terminateNow()
    expect(said(session).at(-1)).toBe('ended: apogee did not end when asked and was stopped')
  })

  test('a still frame closed before its start ticks stands between the stop and the exit while the delay outlasts them', async () => {
    vi.useFakeTimers()
    try {
      const { chats, clock } = client({ chats: [{ arguments: ['chat'], answer: { stdout: resumedKv, exitAfter: 5 } }] }, { start: 3, still: true })
      const session = chats.openChat(binary)
      await session.close()
      clock.start()
      vi.advanceTimersByTime(60_000)
      expect(said(session).filter((type) => type.startsWith('ended'))).toEqual([])
    } finally {
      vi.useRealTimers()
    }
  })

  test('closed before it spoke, its launch lines still drain while the exit counts down: the app reads what the child wrote', async () => {
    const { chats, clock } = client({ chats: [{ arguments: ['chat'], answer: { stdout: resumedKv, exitAfter: 5 } }] })
    const session = chats.openChat(binary)
    await session.close()
    clock.advance(2)
    expect(said(session)).toEqual(['session', 'notice'])
    clock.advance(2)
    expect(said(session)).toEqual(['session', 'notice'])
    clock.advance(1)
    expect(said(session).at(-1)).toBe('ended: the session ended')
  })

  test('with no delay a close ends it at once, as 1d did', async () => {
    const { chats, clock } = client({ chats: [{ arguments: ['chat'], answer: { stdout: resumedKv } }] })
    const session = chats.openChat(binary)
    clock.advance(2)
    await session.close()
    expect(said(session).at(-1)).toBe('ended: the session ended')
  })
})

describe('a restart is the rule\'s next answer (design 1v)', () => {
  test('a second launch of the kv chat replays resumed-kv-pair-handmade.jsonl, under the suite it names', () => {
    const launch = ['chat', '--output-format', 'stream-json', '--input-format', 'stream-json', '--tools', '--resume', '20261119-111551-8425']
    const { chats, clock } = client({ chats: [{ arguments: launch, answer: [{ stdout: resumedKv, exitAfter: 5 }, { stdout: resumedKvPair }] }] })
    const first = chats.openChat(binary, { resume: '20261119-111551-8425' })
    const second = chats.openChat(binary, { resume: '20261119-111551-8425', suite: 'pair' })
    clock.advance(3)
    expect(said(first)).toEqual(['session', 'notice'])
    expect(said(second)).toEqual(['session', 'notice', 'notice'])
    expect(events(second)[0]).toMatchObject({ case: 'event', event: { kind: { case: 'session', session: { suite: 'pair' } } } })
  })
})

describe('a continuation picked by what is written (design 1v)', () => {
  const permission: Answer = { stdout: permissionYes, continuations: { after: 'question', by: { yes: permissionYes, session: permissionSession, always: permissionAlways, no: permissionNo } } }
  const recordedAnswer = (recording: string) => {
    const lines = LineFramer.lines(recording)
    const question = lines.findIndex((line) => EventDecoder.decode(line).type === 'question')
    return lines.slice(question + 1).map((line) => EventDecoder.decode(line)).flatMap((event) => (event.kind.case === 'answerDelta' ? [event.kind.text] : [])).join('')
  }

  for (const [answer, recording] of [['yes', permissionYes], ['session', permissionSession], ['always', permissionAlways], ['no', permissionNo]] as const) {
    test(`the permission answered ${answer} plays chat-permission-${answer}.jsonl from the question on`, async () => {
      const { chats, clock, child } = client({ chats: [{ arguments: ['chat'], answer: permission }] })
      const session = chats.openChat(binary) as MachineSession
      await session.send('Make _fetch() retry timeouts.')
      clock.advance(200)
      expect(said(session).at(-1)).toBe('question')
      await session.answer(answer)
      clock.advance(200)
      expect(said(session).at(-1)).toBe('result')
      expect(answered(session)).toBe(recordedAnswer(recording))
      expect(said(session)).toEqual(LineFramer.lines(recording).map((line) => EventDecoder.decode(line).type))
      expect(child.unanswered).toEqual([])
    })
  }

  test('an answer no branch names is noted in unanswered, and the replay goes on as recorded', async () => {
    const { chats, clock, child } = client({ chats: [{ arguments: ['chat'], answer: permission }] })
    const session = chats.openChat(binary)
    await session.send('Make _fetch() retry timeouts.')
    clock.advance(200)
    await session.answer('maybe')
    clock.advance(200)
    expect(child.unanswered).toEqual(['chats: answer maybe after the question of apogee chat --output-format stream-json --input-format stream-json --tools'])
    expect(answered(session)).toBe(recordedAnswer(permissionYes))
  })

  test('a line written before the choice point is no choice: the first turn of chat-error-handmade.jsonl plays its error', async () => {
    const error: Answer = { stdout: chatError, continuations: { after: 'error', by: { 'set model=claude': chatErrorSwitch, user: chatErrorRetry } } }
    const { chats, clock, child } = client({ chats: [{ arguments: ['chat'], answer: error }] })
    const session = chats.openChat(binary)
    await session.send('What’s this stack trace telling me?')
    clock.advance(10)
    expect(said(session)).toEqual(['session', 'notice', 'notice', 'error'])
    expect(child.unanswered).toEqual([])
  })

  test("Error's set of the model to claude plays the switch branch: the CLI's switch notice now, then the turn sent again answered by claude", async () => {
    const error: Answer = { stdout: chatError, continuations: { after: 'error', by: { 'set model=claude': chatErrorSwitch, user: chatErrorRetry } } }
    const { chats, clock, child } = client({ chats: [{ arguments: ['chat'], answer: error }] })
    const session = chats.openChat(binary)
    await session.send('What’s this stack trace telling me?')
    clock.advance(10)
    await session.set({ model: 'claude' })
    clock.advance(1)
    expect(events(session).at(-1)).toMatchObject({ case: 'event', event: { kind: { case: 'notice', text: 'switched to claude -- from the next turn' } } })
    expect(await session.send('What’s this stack trace telling me?')).toBe(2)
    clock.advance(100)
    expect(events(session).at(-1)).toMatchObject({ case: 'event', event: { kind: { case: 'result', result: { model: 'claude' } } } })
    expect(child.unanswered).toEqual([])
  })

  test('a setting is named by what it sets: another after the error is no switch -- noted, and the replay goes on as recorded', async () => {
    const error: Answer = { stdout: chatError, continuations: { after: 'error', by: { 'set model=claude': chatErrorSwitch, user: chatErrorRetry } } }
    const { chats, clock, child } = client({ chats: [{ arguments: ['chat'], answer: error }] })
    const session = chats.openChat(binary)
    await session.send('What’s this stack trace telling me?')
    clock.advance(10)
    await session.set({ temperature: 0.4 })
    clock.advance(100)
    expect(child.unanswered).toEqual(['chats: set temperature=0.4 after the error of apogee chat --output-format stream-json --input-format stream-json --tools'])
    expect(said(session)).toEqual(['session', 'notice', 'notice', 'error'])
  })

  test('a model no branch names is noted as what it sets: set model=vast', async () => {
    const error: Answer = { stdout: chatError, continuations: { after: 'error', by: { 'set model=claude': chatErrorSwitch, user: chatErrorRetry } } }
    const { chats, clock, child } = client({ chats: [{ arguments: ['chat'], answer: error }] })
    const session = chats.openChat(binary)
    await session.send('What’s this stack trace telling me?')
    clock.advance(10)
    await session.set({ model: 'vast', think: 'on' })
    expect(child.unanswered).toEqual(['chats: set model=vast think=on after the error of apogee chat --output-format stream-json --input-format stream-json --tools'])
  })

  test("Error's retry (the turn sent again) plays the retry branch: answered by vast", async () => {
    const error: Answer = { stdout: chatError, continuations: { after: 'error', by: { 'set model=claude': chatErrorSwitch, user: chatErrorRetry } } }
    const { chats, clock, child } = client({ chats: [{ arguments: ['chat'], answer: error }] })
    const session = chats.openChat(binary)
    await session.send('What’s this stack trace telling me?')
    clock.advance(10)
    expect(await session.send('What’s this stack trace telling me?')).toBe(2)
    clock.advance(100)
    expect(events(session).at(-1)).toMatchObject({ case: 'event', event: { kind: { case: 'result', result: { model: 'vast' } } } })
    expect(child.unanswered).toEqual([])
  })
})

describe('a stream cut mid-answer (design 1v)', () => {
  test('resumed-kv-ended-handmade.jsonl, exit 2: the answer so far, then the end, failed, with its stderr tail', async () => {
    const { chats, clock } = client({ chats: [{ arguments: ['chat'], answer: { stdout: resumedKvEnded, exit: 2, stderr: 'loading\nfatal: the model went away\n' } }] })
    const session = chats.openChat(binary)
    await session.send('Would a 16-bit cache fit at 128K?')
    clock.advance(40)
    expect(answered(session)).toBe('A 16-bit cache is twice the 8-bit one, 4.8 GB at 128K, so with the weights it')
    expect(said(session).at(-1)).toBe('ended: apogee exited with status 2: fatal: the model went away')
  })
})

describe('a cancel ends the open turn as the binary ended one (design 1v)', () => {
  const lastResult = (session: ChatSession) => {
    const last = events(session).at(-1)
    return last?.case === 'event' && last.event.kind.case === 'result' ? last.event.kind.result : undefined
  }

  test('at the permission gate: the gated call failed, then the turn cancelled -- as chat-permission-cancel.jsonl recorded it', async () => {
    const answer: Answer = { stdout: permissionYes, continuations: { after: 'question', by: { yes: permissionYes } }, cancelled: permissionCancel }
    const { chats, clock, child } = client({ chats: [{ arguments: ['chat'], answer }] })
    const session = chats.openChat(binary)
    await session.send('Make _fetch() retry timeouts.')
    clock.advance(200)
    expect(said(session).at(-1)).toBe('question')
    await session.cancel()
    clock.advance(200)
    expect(said(session)).toEqual(LineFramer.lines(permissionCancel).map((line) => EventDecoder.decode(line).type))
    expect(events(session).at(-2)).toMatchObject({ case: 'event', event: { kind: { case: 'toolCall', call: { name: 'write_file', state: 'failed' } } } })
    expect(lastResult(session)).toMatchObject({ finishReason: 'cancelled', model: 'root' })
    expect(child.unanswered).toEqual([])
    // The choice point is spent: a turn sent after is past the recording, noted once.
    await session.send('Then just show me the patch.')
    expect(child.unanswered).toEqual(['chats: turn 2 of apogee chat --output-format stream-json --input-format stream-json --tools'])
  })

  test('while the question waits: the turn cancelled with the prose streamed before it, as chat-question-cancel.jsonl recorded it', async () => {
    const answer: Answer = { stdout: questionBatch, continuations: { after: 'question', by: { 'Per batch': questionBatch } }, cancelled: questionCancel }
    const { chats, clock, child } = client({ chats: [{ arguments: ['chat'], answer }] })
    const session = chats.openChat(binary)
    await session.send('Review the retry-budget change in PR 482 before I merge it.')
    clock.advance(200)
    expect(said(session).at(-1)).toBe('question')
    await session.cancel()
    clock.advance(200)
    expect(said(session).slice(-2)).toEqual(['question', 'result'])
    expect(lastResult(session)).toMatchObject({ finishReason: 'cancelled', model: 'claude', text: 'The change adds a `RetryBudget` that caps retries at 20 per minute. One thing decides whether it’s right:' })
    expect(child.unanswered).toEqual([])
  })

  test('mid-reasoning, wherever it comes: what had played stands, the rest of the turn is dropped, and chat-streaming-cancel.jsonl\'s cancelled result ends it', async () => {
    const { chats, clock, child } = client({ chats: [{ arguments: ['chat'], answer: { stdout: streaming, cancelled: streamingCancel } }] })
    const session = chats.openChat(binary)
    await session.send('Where do fetches start in this repo, and what cancels them?')
    await session.send('So one slow host takes the whole batch down.')
    const deltas = () => said(session).filter((type) => type === 'thinking_delta').length
    while (deltas() < 50) clock.advance(1)
    await session.cancel()
    const at = said(session).length
    clock.advance(200)
    expect(said(session).slice(at)).toEqual(['result'])
    expect(lastResult(session)).toMatchObject({ finishReason: 'cancelled', model: 'root', text: '' })
    expect(events(session).at(-1)).toMatchObject({ case: 'event', event: { turn: 2 } })
    expect(child.unanswered).toEqual([])
  })

  test('a cancel no recording answers -- mid-turn, the turn not ending there -- is noted, and the replay goes on as recorded', async () => {
    const answer: Answer = { stdout: permissionYes, continuations: { after: 'question', by: { yes: permissionYes } } }
    const { chats, clock, child } = client({ chats: [{ arguments: ['chat'], answer }] })
    const session = chats.openChat(binary)
    await session.send('Make _fetch() retry timeouts.')
    clock.advance(200)
    await session.cancel()
    clock.advance(200)
    expect(child.unanswered).toEqual(['chats: cancel during turn 1 of apogee chat --output-format stream-json --input-format stream-json --tools'])
    expect(said(session).at(-1)).toBe('question')
  })

  test('a cancel with no turn open is ignored, as the binary ignores it: nothing noted, nothing moved', async () => {
    const { chats, clock, child } = client({ chats: [{ arguments: ['chat'], answer: { stdout: streaming, cancelled: streamingCancel } }] })
    const session = chats.openChat(binary)
    clock.advance(5)
    await session.cancel()
    clock.advance(5)
    expect(child.unanswered).toEqual([])
    expect((session as MachineSession).written.at(-1)).toBe('{"type":"cancel"}')
  })
})
