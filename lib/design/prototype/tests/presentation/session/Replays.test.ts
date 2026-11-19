import chatUnderSuite from '@recorded/data/protocol/Fixtures/chat-under-suite.jsonl?raw'
import chatUnderSuiteM29 from '@recorded/data/protocol/Fixtures/chat-under-suite-m29.jsonl?raw'
import codeAnswer from '@recorded/data/protocol/Fixtures/code-answer.jsonl?raw'
import executePlayRecorded from '@recorded/data/protocol/Fixtures/execute-play.jsonl?raw'
import executeProject from '@recorded/data/protocol/Fixtures/execute-project.jsonl?raw'
import markdownAnswer from '@recorded/data/protocol/Fixtures/markdown-answer.jsonl?raw'
import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import privateUnsent from '@recorded/data/protocol/Fixtures/private-unsent.jsonl?raw'
import setSuiteRefused from '@recorded/data/protocol/Fixtures/set-suite-refused.jsonl?raw'
import stoppedTurn from '@recorded/data/protocol/Fixtures/stopped-turn.jsonl?raw'
import executePlay from '../../../fixtures/data/protocol/execute-play-summarize-verify.jsonl?raw'
import { describe, expect, test } from 'vitest'
import { EventDecoder } from '../../../source/data/protocol/EventDecoder'
import { LineFramer } from '../../../source/infrastructure/process/LineFramer'
import { presetFor } from '../../../source/presentation/app/Previews/Presets'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { AppViewModel, playLine } from '../../../source/presentation/app/ViewModels/AppViewModel'
import { Transcript } from '../../../source/presentation/session/ViewModels/Transcript'
import type { RowFace } from '../../../source/presentation/session/ViewModels/SessionViewModel'

/** Every stream this part replays: the four presets', the replay tests', and those its previews draw. */
const streams: Readonly<Record<string, readonly [string, readonly string[]]>> = {
  'new-unsent.jsonl': [newUnsent, []],
  'private-unsent.jsonl': [privateUnsent, []],
  'execute-play-summarize-verify.jsonl (captured)': [executePlay, [playLine]],
  'execute-play.jsonl': [executePlayRecorded, ['/play echo2 hello']],
  'execute-project.jsonl': [executeProject, ['hi']],
  'chat-under-suite.jsonl': [chatUnderSuite, ['hello']],
  'chat-under-suite-m29.jsonl': [chatUnderSuiteM29, ['hello']],
  'set-suite-refused.jsonl': [setSuiteRefused, ['hello']],
  'markdown-answer.jsonl': [markdownAnswer, ['How do I keep the batch going?']],
  'code-answer.jsonl': [codeAnswer, ['Show me a retry in every language you know.']],
  'stopped-turn.jsonl': [stoppedTurn, ['Which port does the dev server use?', 'When is the launch?', 'And the report?']],
}

/** How each event type these streams carry is drawn: a row, a part of one, or the named skip. */
const drawnAs: Readonly<Record<string, string>> = {
  session: 'the header: the model, what it accepts, private, the suite',
  notice: 'a notice row',
  thinking: "the turn's activity",
  thinking_delta: "the activity's reasoning",
  tool_status: "the activity's lines",
  tool_call: 'skipped: its call is the tool_status line (as the Mac app skips it)',
  answer_start: 'the answer row',
  answer_delta: 'the answer, its deltas joined',
  answer_end: 'the answer, finished',
  result: "the answer's footer, or the stopped line",
}

describe("event coverage: every type in this part's streams is a row or a named skip, never guessed", () => {
  for (const [name, [recording, sent]] of Object.entries(streams)) {
    test(name, () => {
      const transcript = new Transcript()
      for (const text of sent) transcript.userWroteAhead(text)
      for (const line of LineFramer.lines(recording)) {
        const event = EventDecoder.decode(line)
        expect(drawnAs[event.type], `${name}: ${event.type} is mapped`).toBeDefined()
        const before = transcript.revision
        transcript.apply({ case: 'event', event })
        if (event.type !== 'tool_call') expect(transcript.revision, `${name}: ${event.type} moved what the window draws`).toBeGreaterThan(before)
      }
      expect(transcript.items.filter((item) => item.content.case === 'unknown')).toEqual([])
    })
  }
})

/**
 * The rows `rows` draws that no replayed event, recorded document or sent
 * text accounts for: a user line is a text sent; a notice, a recorded
 * notice; an activity's lines, recorded tool_status lines, and its
 * reasoning, its turn's deltas joined; an answer, its turn's first k deltas
 * joined, for some k -- never a slice of a string -- or its result's text; a
 * quiet line, a stop the stream recorded or a type it carried.
 */
function untraceable(rows: readonly RowFace[], recording: string, sent: readonly string[]): string[] {
  const events = LineFramer.lines(recording).map((line) => EventDecoder.decode(line))
  const texts = (type: string) => new Set(events.filter((event) => event.type === type).map((event) => String(event.fields.text ?? '')))
  const notices = texts('notice')
  const statuses = texts('tool_status')
  const results = texts('result')
  const joins = (type: string) => {
    const byTurn = new Map<number | undefined, string[]>()
    for (const event of events.filter((entry) => entry.type === type)) {
      const so = byTurn.get(event.turn) ?? ['']
      so.push((so.at(-1) ?? '') + String(event.fields.text ?? ''))
      byTurn.set(event.turn, so)
    }
    // Nothing said yet is the empty join of every turn.
    return new Set(['', ...[...byTurn.values()].flat()])
  }
  const answers = joins('answer_delta')
  const reasonings = joins('thinking_delta')
  const types = new Set(events.map((event) => event.type))
  const cancelled = events.some((event) => event.fields.finish_reason === 'cancelled')
  return rows.flatMap((row): string[] => {
    switch (row.kind) {
      case 'user':
        return sent.includes(row.text) ? [] : [`user: ${row.text}`]
      case 'notice':
        return notices.has(row.text) ? [] : [`notice: ${row.text}`]
      case 'activity':
        return [...row.lines.filter((line) => !statuses.has(line.text)).map((line) => `activity line: ${line.text}`), ...(reasonings.has(row.reasoning) ? [] : [`reasoning: ${row.reasoning}`])]
      case 'answer':
        return answers.has(row.text) || results.has(row.text) ? [] : [`answer: ${row.text}`]
      case 'quiet': {
        const type = /^apogee said something this app does not know yet: (.+)$/.exec(row.text)?.[1]
        if (row.text === 'stopped' && cancelled) return []
        return type !== undefined && types.has(type) ? [] : [`quiet: ${row.text}`]
      }
      case 'ended':
        return [`ended: ${row.text}`]
    }
  })
}

describe('no counter remains: every line of the transcript is a replayed event’s, or what the preset sent', () => {
  const presets: ReadonlyArray<readonly [string, string, readonly string[]]> = [
    ['chat:new', newUnsent, []],
    ['chat:private', privateUnsent, []],
    ['chat:play', executePlay, [playLine]],
    ['chat:suite', newUnsent, []],
  ]
  for (const [name, recording, sent] of presets) {
    test(`${name}, still and after 30 ticks`, async () => {
      for (const still of [true, false]) {
        const app = new AppViewModel(presetFor({ start: name, still, apogee: 'installed' }).preset, modeRules)
        await app.start()
        await new Promise((resolve) => setTimeout(resolve, 0))
        app.clock.advance(30)
        await new Promise((resolve) => setTimeout(resolve, 0))
        expect(untraceable(app.session.face().rows, recording, sent)).toEqual([])
        app.stop()
      }
    })
  }

  test('a turn at every stage of its replay, delta by delta: markdown-answer.jsonl, code-answer.jsonl and stopped-turn.jsonl', () => {
    for (const name of ['markdown-answer.jsonl', 'code-answer.jsonl', 'stopped-turn.jsonl']) {
      const stream = streams[name]
      if (!stream) throw new Error(`no stream ${name}`)
      const [recording, sent] = stream
      const transcript = new Transcript()
      for (const text of sent) transcript.userWroteAhead(text)
      for (const line of LineFramer.lines(recording)) {
        transcript.apply({ case: 'event', event: EventDecoder.decode(line) })
        const rows = transcript.items.map((item, id): RowFace => {
          const content = item.content
          if (content.case === 'answer') return { id, kind: 'answer', text: content.answer.text, finished: content.answer.finished, footer: { model: '', kind: 'unknown', usage: [] } }
          if (content.case === 'user' || content.case === 'notice') return { id, kind: content.case, text: content.text }
          if (content.case === 'activity') return { id, kind: 'activity', summary: '', live: false, reasoning: content.activity.reasoning, lines: content.activity.lines.map((text) => ({ mark: '', text })) }
          return { id, kind: 'quiet', text: content.case === 'stopped' ? 'stopped' : '' }
        })
        expect(untraceable(rows, recording, sent)).toEqual([])
      }
    }
  })

  test('proven on a planted canned reply, and on a counter over a recorded answer: each fails, named', () => {
    const canned: RowFace[] = [
      { id: 0, kind: 'user', text: 'How do I keep the batch going?' },
      { id: 1, kind: 'answer', text: '(Prototype) gpt-oss-20b would answer here. Answers aren’t connected in this prototype; everything around them works.', finished: true, footer: { model: 'gpt-oss', kind: 'local', usage: [] } },
    ]
    expect(untraceable(canned, markdownAnswer, ['How do I keep the batch going?'])).toEqual([
      'answer: (Prototype) gpt-oss-20b would answer here. Answers aren’t connected in this prototype; everything around them works.',
    ])
    const result = /"finish_reason":"stop","model":"chatty","text":"((?:[^"\\]|\\.)*)"/.exec(markdownAnswer)?.[1] ?? ''
    const answer = JSON.parse(`"${result}"`) as string
    const counted: RowFace = { id: 0, kind: 'answer', text: answer.slice(0, Math.round(answer.length * 0.37)), finished: false, footer: { model: '', kind: 'unknown', usage: [] } }
    expect(untraceable([counted], markdownAnswer, [])).toEqual([`answer: ${counted.text}`])
  })
})
