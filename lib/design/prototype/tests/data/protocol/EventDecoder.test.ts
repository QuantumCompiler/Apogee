import { describe, expect, test } from 'vitest'
import mockActor from '@recorded/data/protocol/Fixtures/mock-actor.jsonl?raw'
import trainPipelineRun from '@recorded/business/jobs/Fixtures/train-pipeline-run.jsonl?raw'
import agentsList from '@recorded/business/reads/Fixtures/agents-list.json?raw'
import { DriverLine } from '../../../source/data/protocol/DriverLine'
import { EventDecoder } from '../../../source/data/protocol/EventDecoder'
import { JobEvent } from '../../../source/data/protocol/JobEvent'
import { JSONValue } from '../../../source/data/protocol/JSONValue'
import { MachineProtocol } from '../../../source/data/protocol/MachineProtocol'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'

const lines = (text: string) => text.split('\n').filter((line) => line !== '')

describe('a read\'s document', () => {
  test('text becomes one JSON document, or none', () => {
    const document = ReadDocument.from(agentsList)
    expect(document?.value).toEqual(JSON.parse(agentsList))
    expect(document?.object).toBe('list')
    expect(document?.entries?.length).toBe((JSON.parse(agentsList) as { data: unknown[] }).data.length)
    expect(document?.problems).toEqual([])
    for (const text of ['', 'not json', '[1, 2]', '"text"', '3', 'null', '{"a":1} {"b":2}']) {
      expect(ReadDocument.from(text), text).toBeUndefined()
    }
  })

  test('a whole number is an int, as the Mac app reads one; a fraction or a huge number is not', () => {
    expect(JSONValue.int(3)).toBe(3)
    expect(JSONValue.int(3.5)).toBeUndefined()
    expect(JSONValue.int(1e15)).toBeUndefined()
    expect(JSONValue.int('3')).toBeUndefined()
    expect(JSONValue.string('a')).toBe('a')
    expect(JSONValue.bool(false)).toBe(false)
    expect(JSONValue.array({})).toBeUndefined()
    expect(JSONValue.object([])).toBeUndefined()
  })
})

describe('a chat\'s events, as tolerant as the Mac app\'s', () => {
  const recorded = lines(mockActor)

  test('the session line: protocol, model, the next turn, what it can do', () => {
    const event = EventDecoder.decode(recorded[0] ?? '')
    expect(event.kind).toEqual({
      case: 'session',
      session: {
        protocolVersion: 1,
        model: 'actor',
        nextTurn: 1,
        capabilities: expect.objectContaining({ ask: true, tools: true, schema: '2026-10-13' }),
        isPrivate: false,
        chatID: undefined,
        suite: undefined,
        orchestrating: undefined,
      },
    })
    expect(event.type).toBe('session')
    expect(MachineProtocol.understands(1)).toBe(true)
    expect(MachineProtocol.understands(2)).toBe(false)
  })

  test('a question, a delta, a result, each in its case, with its turn', () => {
    const question = EventDecoder.decode(recorded[9] ?? '')
    expect(question.turn).toBe(1)
    expect(question.kind).toEqual({
      case: 'question',
      question: {
        kind: undefined,
        tool: undefined,
        target: undefined,
        outbound: false,
        detail: undefined,
        items: [
          {
            header: 'Name',
            question: expect.any(String),
            multiSelect: false,
            options: [
              { label: 'todo.md', description: 'A to-do list' },
              { label: 'ideas.md', description: 'Loose ideas' },
            ],
          },
        ],
      },
    })
    expect(EventDecoder.decode(recorded[12] ?? '').kind).toMatchObject({ case: 'question', question: { kind: 'permission' } })
    expect(EventDecoder.decode(recorded[3] ?? '').kind).toEqual({ case: 'thinkingDelta', text: 't a note' })
    expect(EventDecoder.decode(recorded.at(-1) ?? '').kind).toEqual({
      case: 'result',
      result: { text: 'I wrote todo.md with one item: buy milk.', model: 'actor', finishReason: 'stop', usage: undefined },
    })
  })

  test('an unknown type is kept under its name, a known one missing a field is unknown, a task_ type is a task, and a line that is not JSON is unreadable', () => {
    expect(EventDecoder.decode('{"type":"hologram","depth":3}')).toMatchObject({
      kind: { case: 'unknown', type: 'hologram' },
      fields: { type: 'hologram', depth: 3 },
    })
    expect(EventDecoder.decode('{"type":"answer_delta"}').kind).toEqual({ case: 'unknown', type: 'answer_delta' })
    expect(EventDecoder.decode('{"type":"task_round","round":2}').kind).toEqual({ case: 'task', type: 'task_round' })
    expect(EventDecoder.decode('no json here').kind).toEqual({ case: 'unreadable', line: 'no json here' })
    expect(EventDecoder.decode('{"text":"no type"}').kind).toEqual({ case: 'unreadable', line: '{"text":"no type"}' })
  })
})

describe('a job\'s events', () => {
  const recorded = lines(trainPipelineRun)

  test('the recorded pipeline run decodes line by line into its cases', () => {
    const events = recorded.map((line) => JobEvent.decode(line))
    expect(events[0]).toEqual({ case: 'job', verb: 'train pipeline run' })
    expect(events[1]).toMatchObject({ case: 'notice' })
    expect(events[4]).toEqual({
      case: 'stage',
      index: 1,
      count: 2,
      label: 'greetings',
      state: 'training',
      pipelineRunID: 'pipe-20261026-074112',
      runID: 'pipe-20261026-074112-s0',
      cumulativeScore: undefined,
    })
    expect(events[7]).toEqual({ case: 'iteration', iteration: 1, iterations: 4, loss: 0.72, lr: 0.0001, throughput: 10, stage: 1 })
    expect(events.at(-2)).toMatchObject({ case: 'document' })
    expect(events.at(-1)).toMatchObject({ case: 'error', message: expect.stringContaining('pipeline aborted') })
  })

  test('a line this client does not read here is kept whole, never dropped', () => {
    expect(JobEvent.decode('{"type":"task_started","task":"t1"}')).toEqual({
      case: 'other',
      type: 'task_started',
      fields: { type: 'task_started', task: 't1' },
    })
    expect(JobEvent.decode('{"type":"stage","index":1}')).toMatchObject({ case: 'other', type: 'stage' })
    expect(JobEvent.decode('garbled')).toEqual({ case: 'other', type: 'unreadable', fields: { line: 'garbled' } })
  })
})

describe('the lines a driver writes', () => {
  test('each is one JSON object, its type first, as the Mac app writes it', () => {
    expect(DriverLine.encoded({ case: 'user', text: 'hi "you"' })).toBe('{"type":"user","text":"hi \\"you\\""}')
    expect(DriverLine.encoded({ case: 'answer', text: 'todo.md' })).toBe('{"type":"answer","text":"todo.md"}')
    expect(DriverLine.encoded({ case: 'attach', path: 'src/', graph: 'code' })).toBe('{"type":"attach","path":"src/","graph":"code"}')
    expect(DriverLine.encoded({ case: 'cancel' })).toBe('{"type":"cancel"}')
    expect(DriverLine.encoded({ case: 'set', settings: { model: 'local', temperature: 1 } })).toBe(
      '{"type":"set","model":"local","temperature":1}',
    )
  })
})

describe('a memory event names the chats it recalled (the CLI\'s 52a; design 1v)', () => {
  test('chat-streaming-handmade.jsonl\'s recall: each session by id, name and time; an entry with no id skipped; a build before 52a names none', () => {
    const event = EventDecoder.decode(
      '{"chats":1,"decisions":0,"sessions":[{"id":"20260928-101500-5c3a","name":"asyncio timeouts in the scraper","updated":"2026-09-28T10:41:07Z"},{"name":"no id"}],"turn":2,"type":"memory"}',
    )
    expect(event.kind).toEqual({ case: 'memory', chats: 1, decisions: 0 })
    expect(event.recalled).toEqual([{ id: '20260928-101500-5c3a', name: 'asyncio timeouts in the scraper', updated: '2026-09-28T10:41:07Z' }])
    expect(EventDecoder.decode('{"chats":2,"decisions":1,"type":"memory"}').recalled).toEqual([])
    expect(EventDecoder.decode('{"chats":1,"sessions":[{"id":"x"}],"type":"memory"}').recalled).toEqual([{ id: 'x', name: 'x', updated: undefined }])
  })
})
