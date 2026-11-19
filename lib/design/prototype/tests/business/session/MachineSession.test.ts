import { describe, expect, test, vi } from 'vitest'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import launchRefusedModel from '@recorded/data/protocol/Fixtures/launch-refused-model.err?raw'
import launchRefusedModelExit from '@recorded/data/protocol/Fixtures/launch-refused-model.exit?raw'
import mockActor from '@recorded/data/protocol/Fixtures/mock-actor.jsonl?raw'
import questionCancelled from '@recorded/data/protocol/Fixtures/question-cancelled.jsonl?raw'
import setModel from '@recorded/data/protocol/Fixtures/set-model.jsonl?raw'
import stoppedTurn from '@recorded/data/protocol/Fixtures/stopped-turn.jsonl?raw'
import { ApogeeClient } from '../../../source/business/session/ApogeeClient'
import { ChatLaunch } from '../../../source/business/session/ChatLaunch'
import { SessionEnd } from '../../../source/business/session/ChatSession'
import type { ChatSession, SessionEvent } from '../../../source/business/session/ChatSession'
import { MachineSession } from '../../../source/business/session/MachineSession'
import { EventDecoder } from '../../../source/data/protocol/EventDecoder'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import type { ClockSettings } from '../../../source/infrastructure/clock/Clock'
import { BoundedTail } from '../../../source/infrastructure/process/BoundedTail'
import { ChildExit } from '../../../source/infrastructure/process/ChildExit'
import { LineFramer } from '../../../source/infrastructure/process/LineFramer'
import { recordedExit, StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'

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
