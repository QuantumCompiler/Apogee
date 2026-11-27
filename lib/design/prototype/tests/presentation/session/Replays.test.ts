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
import chatsShowKv from '../../../fixtures/business/reads/chats-show-kv-handmade.json?raw'
import chatErrorRetry from '../../../fixtures/data/protocol/chat-error-retry-handmade.jsonl?raw'
import chatErrorSwitch from '../../../fixtures/data/protocol/chat-error-switch-handmade.jsonl?raw'
import chatError from '../../../fixtures/data/protocol/chat-error-handmade.jsonl?raw'
import permissionAlways from '../../../fixtures/data/protocol/chat-permission-always.jsonl?raw'
import permissionCancel from '../../../fixtures/data/protocol/chat-permission-cancel.jsonl?raw'
import permissionNo from '../../../fixtures/data/protocol/chat-permission-no.jsonl?raw'
import permissionSession from '../../../fixtures/data/protocol/chat-permission-session.jsonl?raw'
import permissionYes from '../../../fixtures/data/protocol/chat-permission-yes.jsonl?raw'
import questionBatch from '../../../fixtures/data/protocol/chat-question-batch-handmade.jsonl?raw'
import questionCancel from '../../../fixtures/data/protocol/chat-question-cancel.jsonl?raw'
import questionTwo from '../../../fixtures/data/protocol/chat-question-two-handmade.jsonl?raw'
import questionHost from '../../../fixtures/data/protocol/chat-question-host-handmade.jsonl?raw'
import questionOwn from '../../../fixtures/data/protocol/chat-question-own-handmade.jsonl?raw'
import questionSkip from '../../../fixtures/data/protocol/chat-question-skip-handmade.jsonl?raw'
import questionUpload from '../../../fixtures/data/protocol/chat-question-upload-handmade.jsonl?raw'
import sourcesStream from '../../../fixtures/data/protocol/chat-sources.jsonl?raw'
import streaming from '../../../fixtures/data/protocol/chat-streaming-handmade.jsonl?raw'
import streamingCancel from '../../../fixtures/data/protocol/chat-streaming-cancel.jsonl?raw'
import resumedKvEnded from '../../../fixtures/data/protocol/resumed-kv-ended-handmade.jsonl?raw'
import resumedKv from '../../../fixtures/data/protocol/resumed-kv-handmade.jsonl?raw'
import resumedKvHeld from '../../../fixtures/data/protocol/resumed-kv-held-handmade.jsonl?raw'
import resumedKvTurn from '../../../fixtures/data/protocol/resumed-kv-turn.jsonl?raw'
import resumedKvPair from '../../../fixtures/data/protocol/resumed-kv-pair-handmade.jsonl?raw'
import { existsSync, readFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
import { recipes } from '../../../scripts/capture.ts'
import { EventDecoder } from '../../../source/data/protocol/EventDecoder'
import { LineFramer } from '../../../source/infrastructure/process/LineFramer'
import { presetFor } from '../../../source/presentation/app/Previews/Presets'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { AppViewModel, playLine, scenarioTurns } from '../../../source/presentation/app/ViewModels/AppViewModel'
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
  // 1v's: each scenario's streams and their branches, and the kv chat's children.
  'chat-streaming-handmade.jsonl': [streaming, scenarioTurns.streaming],
  'chat-permission-yes.jsonl': [permissionYes, [scenarioTurns.retry]],
  'chat-permission-session.jsonl': [permissionSession, [scenarioTurns.retry]],
  'chat-permission-always.jsonl': [permissionAlways, [scenarioTurns.retry]],
  'chat-permission-no.jsonl': [permissionNo, [scenarioTurns.retry]],
  'chat-sources.jsonl': [sourcesStream, [scenarioTurns.wal]],
  'chat-error-handmade.jsonl': [chatError, [scenarioTurns.crash]],
  'chat-error-retry-handmade.jsonl': [chatErrorRetry, [scenarioTurns.crash, scenarioTurns.crash]],
  'chat-error-switch-handmade.jsonl': [chatErrorSwitch, [scenarioTurns.crash, scenarioTurns.crash]],
  'chat-question-upload-handmade.jsonl': [questionUpload, [scenarioTurns.pr]],
  'chat-question-batch-handmade.jsonl': [questionBatch, [scenarioTurns.pr]],
  'chat-question-host-handmade.jsonl': [questionHost, [scenarioTurns.pr]],
  'chat-question-own-handmade.jsonl': [questionOwn, [scenarioTurns.pr]],
  'chat-question-skip-handmade.jsonl': [questionSkip, [scenarioTurns.pr]],
  'resumed-kv-handmade.jsonl': [resumedKv, []],
  'resumed-kv-pair-handmade.jsonl': [resumedKvPair, []],
  'resumed-kv-ended-handmade.jsonl': [resumedKvEnded, [scenarioTurns.kv]],
  // Each scenario's Stop, a capture whose turn a cancel ended; the kv chat's returned and restarted children; two items asked at once (a preview's).
  'chat-streaming-cancel.jsonl': [streamingCancel, scenarioTurns.streaming],
  'chat-permission-cancel.jsonl': [permissionCancel, [scenarioTurns.retry]],
  'chat-question-cancel.jsonl': [questionCancel, [scenarioTurns.pr]],
  'chat-question-two-handmade.jsonl': [questionTwo, [scenarioTurns.pr]],
  'resumed-kv-turn.jsonl': [resumedKvTurn, [scenarioTurns.kv]],
  'resumed-kv-held-handmade.jsonl': [resumedKvHeld, []],
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
  question: 'the question or permission card (1v)',
  sources: "the sources row, drawn after the turn's answer, its citations (1v)",
  memory: 'the recall row, each past chat named (1v)',
  error: "the failed turn's card, in the CLI's words (1v)",
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

/** What a preset's rows may come from: its recordings (every child's and every branch's), what it sent, the `chats show` documents it read back, and the ends its rules give. */
interface Sources {
  readonly recordings: readonly string[]
  readonly sent: readonly string[]
  readonly documents?: readonly string[]
  /** The end a rule gives a child: its exit and stderr, as `SessionEnd.message` says them. */
  readonly ends?: readonly string[]
}

/**
 * The rows `rows` draws that no replayed event, recorded document or sent
 * text accounts for: a user line is a text sent, or a turn read back; a
 * notice, a recorded notice (or the Mac's move words where the CLI said
 * nothing of a move); an activity's lines, recorded tool_status lines, and
 * its reasoning, its turn's deltas joined; an answer, its turn's first k
 * deltas joined, for some k -- never a slice of a string -- or its result's
 * text, or an answer read back; a card, a recorded question; a recall, a
 * recorded memory's chats; sources, a recorded sources event's; an error, a
 * recorded error's words; a warning, a document's; a quiet line, a stop the
 * stream recorded or a type it carried.
 */
function untraceable(rows: readonly RowFace[], from: Sources): string[] {
  const events = from.recordings.flatMap((recording) => LineFramer.lines(recording).map((line) => EventDecoder.decode(line)))
  const documents = (from.documents ?? []).map((text) => JSON.parse(text) as { turns: Array<{ user: { text: string }; answer: { text: string } }>; warnings?: Array<{ message: string }> })
  const texts = (type: string) => new Set(events.filter((event) => event.type === type).map((event) => String(event.fields.text ?? '')))
  const notices = texts('notice')
  const statuses = texts('tool_status')
  const results = texts('result')
  const joins = (type: string) => {
    const byTurn = new Map<string, string[]>()
    let child = 0
    for (const event of events) {
      if (event.type === 'session') child += 1
      if (event.type !== type) continue
      const key = `${child}:${event.turn}`
      const so = byTurn.get(key) ?? ['']
      so.push((so.at(-1) ?? '') + String(event.fields.text ?? ''))
      byTurn.set(key, so)
    }
    // Nothing said yet is the empty join of every turn.
    return new Set(['', ...[...byTurn.values()].flat()])
  }
  const answers = joins('answer_delta')
  const reasonings = joins('thinking_delta')
  const types = new Set(events.map((event) => event.type))
  const cancelled = events.some((event) => event.fields.finish_reason === 'cancelled')
  const questions = new Set(events.flatMap((event) => (event.kind.case === 'question' ? event.kind.question.items.map((item) => item.question) : [])))
  const targets = new Set(events.flatMap((event) => (event.kind.case === 'question' && event.kind.question.target ? [event.kind.question.target] : [])))
  const recalled = new Set(events.flatMap((event) => event.recalled.map((chat) => chat.name)))
  const files = new Set(events.flatMap((event) => (event.kind.case === 'sources' ? event.kind.sources.map((source) => source.file) : [])))
  const errors = new Set(events.flatMap((event) => (event.kind.case === 'error' ? [event.kind.message.split('\n')[0] ?? ''] : [])))
  const readBack = new Set(documents.flatMap((document) => document.turns.flatMap((turn) => [turn.user.text, turn.answer.text])))
  const warnings = new Set(documents.flatMap((document) => (document.warnings ?? []).map((warning) => warning.message)))
  return rows.flatMap((row): string[] => {
    switch (row.kind) {
      case 'user':
        return from.sent.includes(row.text) || readBack.has(row.text) ? [] : [`user: ${row.text}`]
      case 'notice':
        return notices.has(row.text) || /^switching to suite \S+ \(--suite \S+\) -- machine mode carries no suite/.test(row.text) ? [] : [`notice: ${row.text}`]
      case 'activity':
        return [...row.lines.filter((line) => !statuses.has(line.text)).map((line) => `activity line: ${line.text}`), ...(reasonings.has(row.reasoning) ? [] : [`reasoning: ${row.reasoning}`])]
      case 'answer':
        return answers.has(row.text) || results.has(row.text) || readBack.has(row.text) ? [] : [`answer: ${row.text}`]
      case 'quiet': {
        const type = /^apogee said something this app does not know yet: (.+)$/.exec(row.text)?.[1]
        if (row.text === 'stopped' && cancelled) return []
        return type !== undefined && types.has(type) ? [] : [`quiet: ${row.text}`]
      }
      case 'warning':
        return warnings.has(row.text) ? [] : [`warning: ${row.text}`]
      case 'divider':
        return documents.length > 0 && row.text === 'earlier in this conversation' ? [] : [`divider: ${row.text}`]
      case 'recalled':
        return row.chats.every((chat) => recalled.has(chat.name)) ? [] : [`recalled: ${row.summary}`]
      case 'sources':
        return row.sources.every((source) => files.has(source.file)) ? [] : ['sources']
      case 'question':
        return row.items.every((item) => questions.has(item.question)) && row.answered.every((said) => from.sent.includes(said) || said === 'skipped') ? [] : [`question: ${row.items.map((item) => item.question).join('; ')}`]
      case 'permission':
        return targets.has(row.target) ? [] : [`permission: ${row.target}`]
      case 'error':
        return errors.has(row.message) ? [] : [`error: ${row.message}`]
      case 'notSent':
        return from.sent.includes(row.text) ? [] : [`not sent: ${row.text}`]
      case 'todo':
        return types.has('todo') ? [] : ['todo']
      case 'ended':
        return (from.ends ?? []).includes(row.text) ? [] : [`ended: ${row.text}`]
    }
  })
}

describe('no counter remains: every line of the transcript is a replayed event’s, a document read back, or what the preset sent', () => {
  const presets: ReadonlyArray<readonly [string, Sources]> = [
    ['chat:new', { recordings: [newUnsent], sent: [] }],
    ['chat:private', { recordings: [privateUnsent], sent: [] }],
    ['chat:play', { recordings: [executePlay], sent: [playLine] }],
    ['chat:suite', { recordings: [newUnsent], sent: [] }],
    ['chat', { recordings: [streaming, streamingCancel], sent: scenarioTurns.streaming }],
    ['monitor', { recordings: [streaming, streamingCancel], sent: scenarioTurns.streaming }],
    ['chat:compacting', { recordings: [streaming, streamingCancel], sent: scenarioTurns.streaming }],
    ['chat:retry', { recordings: [permissionYes, permissionCancel], sent: [scenarioTurns.retry] }],
    ['chat:wal', { recordings: [sourcesStream], sent: [scenarioTurns.wal] }],
    ['chat:crash', { recordings: [chatError], sent: [scenarioTurns.crash] }],
    ['chat:pr', { recordings: [questionBatch, questionCancel], sent: [scenarioTurns.pr] }],
    ['chat:restarting', { recordings: [resumedKv, resumedKvPair], sent: [], documents: [chatsShowKv] }],
    ['chat:notsent', { recordings: [resumedKv, resumedKvTurn], sent: [scenarioTurns.kv], documents: [chatsShowKv] }],
    ['chat:ended', { recordings: [resumedKvEnded, resumedKv, resumedKvHeld], sent: [scenarioTurns.kv], documents: [chatsShowKv], ends: ['apogee exited with status 2: fatal: the model went away'] }],
  ]
  for (const [name, from] of presets) {
    test(`${name}, still and after 30 ticks`, async () => {
      for (const still of [true, false]) {
        const app = new AppViewModel(presetFor({ start: name, still, apogee: 'installed' }).preset, modeRules)
        await app.start()
        await new Promise((resolve) => setTimeout(resolve, 0))
        expect(untraceable(app.session.face().rows, from), `${name} still=${still}`).toEqual([])
        app.clock.advance(30)
        await new Promise((resolve) => setTimeout(resolve, 0))
        expect(untraceable(app.session.face().rows, from), `${name} still=${still}, 30 ticks on`).toEqual([])
        app.stop()
      }
    })
  }

  test('each scenario’s every branch, played to its end: its rows traced to its own recording', async () => {
    const branches: ReadonlyArray<readonly [string, Sources, (app: AppViewModel) => void | Promise<void>]> = [
      ['chat:retry', { recordings: [permissionNo], sent: [scenarioTurns.retry] }, (app) => app.session.answer(app.session.transcript.standingCard ?? -1, ['no'])],
      ['chat:retry', { recordings: [permissionAlways], sent: [scenarioTurns.retry] }, (app) => app.session.answer(app.session.transcript.standingCard ?? -1, ['always'])],
      ['chat:retry', { recordings: [permissionYes, permissionCancel], sent: [scenarioTurns.retry] }, (app) => app.session.cancel()],
      ['chat:pr', { recordings: [questionHost], sent: [scenarioTurns.pr, 'Per host'] }, (app) => app.session.answer(app.session.transcript.standingCard ?? -1, ['Per host'])],
      ['chat:pr', { recordings: [questionSkip], sent: [scenarioTurns.pr] }, (app) => app.session.skip(app.session.transcript.standingCard ?? -1)],
      ['chat:pr', { recordings: [questionBatch, questionCancel], sent: [scenarioTurns.pr] }, (app) => app.session.cancel()],
      ['chat:crash', { recordings: [chatErrorRetry], sent: [scenarioTurns.crash] }, (app) => app.session.retry()],
      ['chat:crash', { recordings: [chatErrorSwitch], sent: [scenarioTurns.crash] }, (app) => app.session.askInstead('claude')],
      ['chat', { recordings: [streaming, streamingCancel], sent: scenarioTurns.streaming }, (app) => app.session.cancel()],
      ['chat:notsent', { recordings: [resumedKv, resumedKvTurn], sent: [scenarioTurns.kv], documents: [chatsShowKv] }, (app) => app.session.resend(scenarioTurns.kv)],
      ['chat:ended', { recordings: [resumedKvEnded, resumedKv, resumedKvHeld], sent: [scenarioTurns.kv], documents: [chatsShowKv], ends: ['apogee exited with status 2: fatal: the model went away'] }, (app) => app.session.start()],
    ]
    for (const [name, from, act] of branches) {
      const app = new AppViewModel(presetFor({ start: name, still: false, apogee: 'installed' }).preset, modeRules)
      await app.start()
      app.stop()
      await new Promise((resolve) => setTimeout(resolve, 0))
      await act(app)
      app.clock.advance(150)
      await new Promise((resolve) => setTimeout(resolve, 0))
      expect(untraceable(app.session.face().rows, from), name).toEqual([])
      expect(app.child.unanswered, name).toEqual([])
    }
  })

  test('a turn at every stage of its replay, delta by delta: markdown-answer.jsonl, code-answer.jsonl, stopped-turn.jsonl and the Streaming session', () => {
    for (const name of ['markdown-answer.jsonl', 'code-answer.jsonl', 'stopped-turn.jsonl', 'chat-streaming-handmade.jsonl']) {
      const stream = streams[name]
      if (!stream) throw new Error(`no stream ${name}`)
      const [recording, sent] = stream
      const transcript = new Transcript()
      for (const text of sent) transcript.userWroteAhead(text)
      for (const line of LineFramer.lines(recording)) {
        transcript.apply({ case: 'event', event: EventDecoder.decode(line) })
        const rows = transcript.items.flatMap((item, id): RowFace[] => {
          const content = item.content
          if (content.case === 'answer') return [{ id, kind: 'answer', text: content.answer.text, finished: content.answer.finished, footer: { model: '', kind: 'unknown', usage: [] }, sources: [], cited: 0 }]
          if (content.case === 'user') return [{ id, kind: 'user', text: content.text }]
          if (content.case === 'notice') return [{ id, kind: 'notice', text: content.text, billed: false }]
          if (content.case === 'activity') return [{ id, kind: 'activity', summary: '', live: false, waiting: false, reasoning: content.activity.reasoning, lines: content.activity.lines.map((text) => ({ mark: '', text })) }]
          if (content.case === 'stopped') return [{ id, kind: 'quiet', text: 'stopped' }]
          return []
        })
        expect(untraceable(rows, { recordings: [recording], sent })).toEqual([])
      }
    }
  })

  test('proven on a planted canned reply, a counter over a recorded answer, and a REASON-style string revealed slice by slice: each fails, named', () => {
    const canned: RowFace[] = [
      { id: 0, kind: 'user', text: 'How do I keep the batch going?' },
      { id: 1, kind: 'answer', text: '(Prototype) gpt-oss-20b would answer here. Answers aren’t connected in this prototype; everything around them works.', finished: true, footer: { model: 'gpt-oss', kind: 'local', usage: [] }, sources: [], cited: 0 },
    ]
    expect(untraceable(canned, { recordings: [markdownAnswer], sent: ['How do I keep the batch going?'] })).toEqual([
      'answer: (Prototype) gpt-oss-20b would answer here. Answers aren’t connected in this prototype; everything around them works.',
    ])
    const result = /"finish_reason":"stop","model":"chatty","text":"((?:[^"\\]|\\.)*)"/.exec(markdownAnswer)?.[1] ?? ''
    const answer = JSON.parse(`"${result}"`) as string
    const counted: RowFace = { id: 0, kind: 'answer', text: answer.slice(0, Math.round(answer.length * 0.37)), finished: false, footer: { model: '', kind: 'unknown', usage: [] }, sources: [], cited: 0 }
    expect(untraceable([counted], { recordings: [markdownAnswer], sent: [] })).toEqual([`answer: ${counted.text}`])
    // The old prototype's Streaming: its hardcoded REASON revealed by a progress counter (main.html:4947, 4964 at b375fac9).
    const REASON = 'They want partial-failure tolerance. TaskGroup is all-or-nothing by design: the first child that raises cancels the rest.'
    const revealed: RowFace = { id: 0, kind: 'activity', summary: 'thinking', live: true, waiting: false, reasoning: REASON.slice(0, Math.round((REASON.length * 30) / 40)), lines: [] }
    expect(untraceable([revealed], { recordings: [streaming], sent: [] })).toEqual([`reasoning: ${revealed.kind === 'activity' ? revealed.reasoning : ''}`])
  })
})

describe("every stream's provenance: each file a preset names exists, and is a recording or named *-handmade*", () => {
  const here = dirname(fileURLToPath(import.meta.url))
  const prototype = resolve(here, '..', '..', '..')
  // The Models place's too (design 1h): its worlds, its preview's mode recording and the narration card's.
  const previews = [
    'source/presentation/session/Previews/SessionPresets.ts',
    'source/presentation/session/Previews/ScenarioStreams.ts',
    'source/presentation/monitor/Previews/MonitorPreset.ts',
    'source/presentation/app/Previews/ModeRules.ts',
    'source/presentation/models/Previews/ModelsWorlds.ts',
    'source/presentation/models/Previews/ModelsPreview.ts',
    'source/presentation/models/Previews/DeleteNarrationCard.preview.tsx',
    // The Training place's (design 1j): its recordings (`TrainingWorlds.ts`; its worlds' rules, `TrainingWorldRules.ts`, import none) and its preview's mode recording.
    'source/presentation/training/Previews/TrainingWorlds.ts',
    'source/presentation/training/Previews/TrainingPreview.ts',
  ]
  const named = previews.flatMap((file) => [...readFileSync(join(prototype, file), 'utf8').matchAll(/from '((?:@recorded|(?:\.\.\/)+fixtures)\/[^'?]+)\?raw'/g)].map((match) => ({ file, path: match[1] ?? '' })))
  const captured = new Set(Object.values(recipes).flatMap((recipe) => [recipe.output, ...(recipe.chatsShow ? [recipe.chatsShow] : []), ...(recipe.resumes ?? []).map((resume) => resume.output)]))

  test('the presets name the recordings they replay', () => {
    expect(named.length).toBeGreaterThan(30)
  })

  for (const { file, path } of named) {
    test(path, () => {
      const full = path.startsWith('@recorded/') ? resolve(prototype, '..', '..', 'src', 'darwin', 'tests', path.slice('@recorded/'.length)) : resolve(prototype, dirname(file), path)
      expect(existsSync(full), full).toBe(true)
      if (path.startsWith('@recorded/')) return
      const inFixtures = full.slice(full.indexOf('/fixtures/') + '/fixtures/'.length)
      expect(/-handmade\.[a-z]+$/.test(inFixtures) || captured.has(inFixtures), `${inFixtures}: a capture recipe's output, or *-handmade*`).toBe(true)
    })
  }
})
