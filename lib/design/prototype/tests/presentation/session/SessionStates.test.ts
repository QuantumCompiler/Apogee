import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import modelsListServerTools from '@recorded/business/reads/Fixtures/models-list-server-tools.json?raw'
import suitesModelsList from '@recorded/business/reads/Fixtures/suites-models-list.json?raw'
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
import chatKv from '../../../fixtures/data/protocol/chat-kv.jsonl?raw'
import resumedKvEnded from '../../../fixtures/data/protocol/resumed-kv-ended-handmade.jsonl?raw'
import resumedKv from '../../../fixtures/data/protocol/resumed-kv-handmade.jsonl?raw'
import resumedKvHeld from '../../../fixtures/data/protocol/resumed-kv-held-handmade.jsonl?raw'
import resumedKvTurn from '../../../fixtures/data/protocol/resumed-kv-turn.jsonl?raw'
import resumedKvPair from '../../../fixtures/data/protocol/resumed-kv-pair-handmade.jsonl?raw'
import { describe, expect, test } from 'vitest'
import { ModelKind, ModelRow } from '../../../source/business/reads/Models'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { ApogeeClient } from '../../../source/business/session/ApogeeClient'
import type { ChatLaunch } from '../../../source/business/session/ChatLaunch'
import type { MachineSession } from '../../../source/business/session/MachineSession'
import { EventDecoder } from '../../../source/data/protocol/EventDecoder'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { LineFramer } from '../../../source/infrastructure/process/LineFramer'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { Answer, Rule } from '../../../source/infrastructure/process/StandInChild'
import { SessionViewModel } from '../../../source/presentation/session/ViewModels/SessionViewModel'
import type { RowFace } from '../../../source/presentation/session/ViewModels/SessionViewModel'

const settled = () => new Promise((resolve) => setTimeout(resolve, 0))
const kvID = (JSON.parse(chatsShowKv) as { id: string }).id
const chat = ['chat', '--output-format', 'stream-json', '--input-format', 'stream-json', '--tools']

/**
 * A session over the stand-in, the chat rule `answer` (or a list of them, one
 * a launch) and any `chats` rules more, started; its reader the stand-in's
 * reads, which answer the kv chat's `chats show`; every child it opens kept.
 */
async function opened(answer: Answer | readonly [Answer, ...Answer[]], options: { launch?: ChatLaunch; models?: string; start?: boolean; chats?: readonly Rule[] } = {}) {
  const launch = options.launch ?? {}
  const child = new StandInChild(
    {
      rules: {
        chats: [{ arguments: launch.resume ? [...chat, '--resume', launch.resume] : ['chat'], answer }, ...(options.chats ?? [])],
        reads: [{ arguments: ['chats', 'show', kvID], answer: { stdout: chatsShowKv } }],
      },
      apogee: 'installed',
    },
    { configTypoRefusal: agentsListBadConfig },
  )
  const clock = new Clock({})
  const starter = new ApogeeClient(child, clock)
  const model = new SessionViewModel(starter, clock, launch, undefined, undefined, new ReadRunner(child))
  const list = ReadDocument.from(options.models ?? suitesModelsList) as ReadDocument
  model.modelKinds = ModelKind.byBackend(list)
  model.modelRows = ModelRow.list(list)
  const children: MachineSession[] = []
  const open = starter.openChat.bind(starter)
  starter.openChat = (binary, how) => {
    const session = open(binary, how) as MachineSession
    children.push(session)
    return session
  }
  if (options.start ?? true) {
    await model.start()
    // The session line plays: the input is the person's.
    clock.advance(1)
    await settled()
  }
  return { model, clock, child, children }
}

async function tick(clock: Clock, count = 1) {
  clock.advance(count)
  await settled()
}

/** The answer a recording gives after its question: every answer delta past it, joined. */
function replyOf(recording: string): string {
  const events = LineFramer.lines(recording).map((line) => EventDecoder.decode(line))
  const at = events.findIndex((event) => event.type === 'question')
  const after = events.slice(at + 1)
  const start = after.findIndex((event) => event.type === 'answer_start')
  return after.slice(start).flatMap((event) => (event.kind.case === 'answerDelta' ? [event.kind.text] : [])).join('')
}

const lastRow = (model: SessionViewModel) => model.face().rows.at(-1) as RowFace
const rowOf = <K extends RowFace['kind']>(model: SessionViewModel, kind: K) => model.face().rows.find((row) => row.kind === kind) as Extract<RowFace, { kind: K }> | undefined

describe('Permission (chat:retry): the gate as a card; each answer plays its own reply', () => {
  const rule: Answer = { stdout: permissionYes, continuations: { after: 'question', by: { yes: permissionYes, session: permissionSession, always: permissionAlways, no: permissionNo } }, cancelled: permissionCancel }
  const turn = 'Make `_fetch()` retry timeouts and connection errors, using `settings.fetch.retries` (it’s already in config.toml). Go ahead and edit the file.'

  test('the card: the change asked for, its diff folded, and the four answers', async () => {
    const { model, clock } = await opened(rule)
    model.setDraft(turn)
    model.submit()
    await tick(clock, 120)
    const card = rowOf(model, 'permission')
    expect(card).toMatchObject({ title: 'Change a file?', subtitle: 'root wants to write to an existing file', tool: 'write_file', target: 'src/ingest/runner.py', added: 11, removed: 3, answered: '', deniedNote: '', closed: false })
    expect(card?.diff).toContain('+                await asyncio.sleep(2 ** attempt)')
    expect(model.transcript.status).toEqual({ case: 'waiting', turn: 1 })
    expect(model.face().composer.status).toBe('waiting for you')
    // The card is the turn's state: no pending line beside it, and its activity stays open, paused.
    expect(model.face().pending.shown).toBe(false)
    expect(model.face().rows.find((row) => row.kind === 'activity')).toMatchObject({ waiting: true, summary: 'thinking · 2 steps' })
  })

  for (const [answer, recording, words] of [
    ['yes', permissionYes, 'allowed once'],
    ['session', permissionSession, 'allowed for this session'],
    ['always', permissionAlways, 'always allowed'],
    ['no', permissionNo, 'denied by you'],
  ] as const) {
    test(`${answer}: written as the protocol's answer, the card collapsed to "${words}", and its own reply played`, async () => {
      const { model, clock, children, child } = await opened(rule)
      model.setDraft(turn)
      model.submit()
      await tick(clock, 120)
      const card = rowOf(model, 'permission')
      model.answer(card?.id ?? -1, [answer])
      await tick(clock, 120)
      expect(children[0]?.written.at(-1)).toBe(`{"type":"answer","text":"${answer}"}`)
      expect(rowOf(model, 'permission')).toMatchObject({ answered: words })
      // Denied, the old note says the model was told and carried on (`main.html:424` at `b375fac9`); allowed, none.
      expect(rowOf(model, 'permission')).toMatchObject({ deniedNote: answer === 'no' ? 'you denied write_file — root was told and carried on without it' : '' })
      const reply = model.face().rows.filter((row): row is Extract<RowFace, { kind: 'answer' }> => row.kind === 'answer').at(-1)
      expect(reply?.text).toBe(replyOf(recording))
      expect(child.unanswered).toEqual([])
    })
  }

  test('Stop while the card stands: a cancel, the card closed -- not answered, the turn ended first -- then stopped, and the session takes a turn again', async () => {
    const { model, clock, children, child } = await opened(rule)
    model.setDraft(turn)
    model.submit()
    await tick(clock, 120)
    expect(model.canCancel).toBe(true)
    model.cancel()
    expect(children[0]?.written.at(-1)).toBe('{"type":"cancel"}')
    await tick(clock, 10)
    expect(rowOf(model, 'permission')).toMatchObject({ answered: '', closed: true })
    expect(lastRow(model)).toEqual({ id: lastRow(model).id, kind: 'quiet', text: 'stopped' })
    expect(model.transcript.status).toEqual({ case: 'ready' })
    expect(child.unanswered).toEqual([])
  })
})

describe('Question (chat:pr): the card; each answer and Skip plays its own reply; no change once written', () => {
  const rule: Answer = {
    stdout: questionBatch,
    continuations: { after: 'question', by: { 'Per upload': questionUpload, 'Per batch': questionBatch, 'Per host': questionHost, 'Per endpoint, shared by every upload to it': questionOwn, '': questionSkip } },
    cancelled: questionCancel,
  }
  const ask = async (answer: Answer = rule) => {
    const opening = await opened(answer, { models: modelsListServerTools })
    opening.model.setDraft('Review the retry-budget change in PR 482 before I merge it.')
    opening.model.submit()
    await tick(opening.clock, 120)
    return opening
  }

  test('the card: Claude is asking, its header, the question and three options, with room for an answer of one’s own', async () => {
    const { model } = await ask()
    expect(model.face().pending.shown).toBe(false)
    expect(rowOf(model, 'activity')).toMatchObject({ waiting: false, summary: 'thought · 2 steps' })
    expect(rowOf(model, 'question')).toMatchObject({
      label: 'Claude is asking',
      items: [
        {
          header: 'Scope',
          question: 'Should the retry budget apply per upload, per batch, or per host?',
          options: [
            { label: 'Per upload', description: 'Each file gets its own 20 retries.' },
            { label: 'Per batch', description: 'One shared budget for every upload in a batch.' },
            { label: 'Per host', description: 'A budget for each server, shared across uploads.' },
          ],
          picked: '',
        },
      ],
      answered: [],
      closed: false,
    })
  })

  test('a pick the preset opens it with (the old `pr: { choice: \'batch\' }`) is drawn picked, and writes nothing until Answer', async () => {
    const opening = await opened(rule, { models: modelsListServerTools })
    opening.model.preselect('Per batch')
    opening.model.setDraft('Review the retry-budget change in PR 482 before I merge it.')
    opening.model.submit()
    await tick(opening.clock, 120)
    expect(rowOf(opening.model, 'question')?.items[0]?.picked).toBe('Per batch')
    expect(opening.children[0]?.written.filter((line) => line.includes('"answer"'))).toEqual([])
  })

  for (const [answer, recording, said] of [
    ['Per upload', questionUpload, 'Per upload'],
    ['Per batch', questionBatch, 'Per batch'],
    ['Per host', questionHost, 'Per host'],
    ['Per endpoint, shared by every upload to it', questionOwn, 'Per endpoint, shared by every upload to it'],
    ['', questionSkip, 'skipped'],
  ] as const) {
    test(`${said}: its reply, and the card collapsed to what was asked and said -- with no "change answer"`, async () => {
      const { model, clock, child } = await ask()
      const card = rowOf(model, 'question')
      if (answer === '') model.skip(card?.id ?? -1)
      else model.answer(card?.id ?? -1, [answer])
      await tick(clock, 120)
      expect(rowOf(model, 'question')).toMatchObject({ answered: [said] })
      expect(rowOf(model, 'question')).not.toHaveProperty('canChange')
      const reply = model.face().rows.filter((row): row is Extract<RowFace, { kind: 'answer' }> => row.kind === 'answer').at(-1)
      expect(reply?.text).toBe(replyOf(recording))
      expect(reply?.footer.usage).toEqual(['tokens not reported by the Claude app'])
      // The turn's footer once, under its last answer: the prose before the question has none (`main.html:574` at `b375fac9`).
      const answers = model.face().rows.filter((row): row is Extract<RowFace, { kind: 'answer' }> => row.kind === 'answer')
      expect(answers.slice(0, -1).map((row) => row.footer)).toEqual(answers.slice(0, -1).map(() => ({ model: '', kind: 'subscription', usage: [] })))
      expect(answers.at(-1)?.footer).toMatchObject({ model: 'claude', usage: ['tokens not reported by the Claude app'] })
      expect(child.unanswered).toEqual([])
      // A written answer cannot be taken back: a second answer to the card takes nothing.
      model.answer(card?.id ?? -1, ['Per host'])
      expect(rowOf(model, 'question')).toMatchObject({ answered: [said] })
    })
  }

  test('Stop while it asks: a cancel, the card closed -- not answered, the turn ended first -- then stopped; the prose before it stays', async () => {
    const { model, clock, child } = await ask()
    model.cancel()
    await tick(clock, 10)
    expect(rowOf(model, 'question')).toMatchObject({ answered: [], closed: true })
    expect(lastRow(model)).toMatchObject({ kind: 'quiet', text: 'stopped' })
    expect(rowOf(model, 'answer')?.text).toBe('The change adds a `RetryBudget` that caps retries at 20 per minute. One thing decides whether it’s right:')
    expect(model.transcript.status).toEqual({ case: 'ready' })
    expect(child.unanswered).toEqual([])
  })

  test('a question of two items (chat-question-two-handmade.jsonl): the card holds both, takes one answer for each, and the turn plays on to its reply', async () => {
    const { model, clock, children, child } = await ask({ stdout: questionTwo, continuations: { after: 'question', by: { 'Per batch': questionTwo } } })
    const card = rowOf(model, 'question')
    expect(card?.items.map((item) => item.question)).toEqual(['Should the retry budget apply per upload, per batch, or per host?', 'Over what window does the budget refill?'])
    model.answer(card?.id ?? -1, ['Per batch', 'Per minute'])
    await tick(clock, 120)
    expect(children[0]?.written.slice(-2)).toEqual(['{"type":"answer","text":"Per batch"}', '{"type":"answer","text":"Per minute"}'])
    expect(rowOf(model, 'question')).toMatchObject({ answered: ['Per batch', 'Per minute'] })
    expect(model.transcript.status).toEqual({ case: 'ready' })
    expect(child.unanswered).toEqual([])
  })
})

describe('Sources (chat:wal): the excerpts numbered; each citation opens and closes', () => {
  test('the answer cites three sources; each opens its card, a second click or Close closes it', async () => {
    const { model, clock } = await opened({ stdout: sourcesStream })
    model.setDraft('Why does group commit make the WAL cheaper under load? Point me to the pages.')
    model.submit()
    await tick(clock, 120)
    const sources = rowOf(model, 'sources')
    expect(sources?.sources).toEqual([
      { index: 1, file: 'storage-engine-design.pdf', place: 'pages 185–189', score: 'score 0.94 · lexical' },
      { index: 2, file: 'postgresql.conf', place: 'lines 209–219', score: 'score 0.91 · lexical' },
      { index: 3, file: 'storage-engine-design.pdf', place: 'pages 191–193', score: 'score 0.91 · lexical' },
    ])
    const answer = rowOf(model, 'answer')
    expect(answer?.sources).toEqual(sources?.sources)
    for (const index of [1, 2, 3]) {
      model.openCitation(answer?.id ?? -1, index)
      expect(rowOf(model, 'answer')?.cited).toBe(index)
      model.openCitation(answer?.id ?? -1, index)
      expect(rowOf(model, 'answer')?.cited).toBe(0)
    }
    model.openCitation(answer?.id ?? -1, 2)
    model.closeCitation()
    expect(rowOf(model, 'answer')?.cited).toBe(0)
  })

  test('a preset opens a citation by its turn, before the answer has come: it stands open under it once it does', async () => {
    const { model, clock } = await opened({ stdout: sourcesStream })
    model.citeTurn(1, 1)
    model.writeAhead('Why does group commit make the WAL cheaper under load? Point me to the pages.')
    await tick(clock, 120)
    expect(rowOf(model, 'answer')?.cited).toBe(1)
    expect(rowOf(model, 'answer')?.sources[0]).toMatchObject({ index: 1, file: 'storage-engine-design.pdf', place: 'pages 185–189' })
  })

  test("the switch to the billed claude is the CLI's notice, marked billed per use from here", async () => {
    const { model, clock } = await opened({ stdout: sourcesStream })
    await tick(clock, 3)
    expect(model.face().rows.find((row) => row.kind === 'notice')).toMatchObject({ text: 'switched to claude -- from the next turn', billed: true })
  })
})

describe('Error (chat:crash): the failed load as the old card; both fixes play their own reply', () => {
  const rule: Answer = { stdout: chatError, continuations: { after: 'error', by: { 'set model=claude': chatErrorSwitch, user: chatErrorRetry } } }
  const failed = async () => {
    const opening = await opened(rule)
    opening.model.setDraft('What’s this stack trace telling me?')
    opening.model.submit()
    await tick(opening.clock, 10)
    return opening
  }

  test("the card: the model didn't load, the CLI's words, its details, and the two fixes -- the switch marked billed", async () => {
    const { model } = await failed()
    expect(lastRow(model)).toMatchObject({
      kind: 'error',
      title: 'vast didn’t load',
      message: "vast: could not load the model at '/Users/someone/.apogee/models/vast/gguf/eeeeeeeeeeee/vast-F16.gguf' -- check that the file exists and is a valid GGUF",
      loadFailed: true,
      instead: 'claude',
      canRetry: true,
    })
    const card = lastRow(model)
    expect(card.kind === 'error' ? card.details : '').toContain('llama.cpp said: llama_model_load: error loading model: unable to allocate Metal buffer of 7.9 GiB')
    // The screenshot's description was copied, not made: vast was never loaded before the turn failed to load it.
    expect(model.face().rows.some((row) => row.kind === 'notice' && row.text.includes("described by vast, from another chat's index"))).toBe(true)
  })

  test('Unload and retry: the turn sent again, answered by vast', async () => {
    const { model, clock, child } = await failed()
    model.retry()
    await tick(clock, 80)
    expect(model.transcript.model).toBe('vast')
    expect(model.face().rows.filter((row) => row.kind === 'answer').at(-1)).toMatchObject({ footer: { model: 'vast' } })
    expect(child.unanswered).toEqual([])
  })

  test('Ask claude instead · billed: the set line, then the turn sent again, answered by claude', async () => {
    const { model, clock, children, child } = await failed()
    model.askInstead('claude')
    await tick(clock, 80)
    expect(children[0]?.written.slice(-2)).toEqual(['{"type":"set","model":"claude"}', '{"type":"user","text":"What’s this stack trace telling me?"}'])
    expect(model.transcript.model).toBe('claude')
    expect(model.face().rows.some((row) => row.kind === 'notice' && row.text === 'switched to claude -- from the next turn' && row.billed)).toBe(true)
    // The card above still names the model that failed to load.
    expect(rowOf(model, 'error')).toMatchObject({ title: 'vast didn’t load' })
    expect(child.unanswered).toEqual([])
  })

  const noSwitch = (model: SessionViewModel) => {
    expect(model.face().rows.some((row) => row.kind === 'notice' && row.text.startsWith('switched to claude'))).toBe(false)
    expect(model.transcript.model).toBe('vast')
  }

  test('another setting after the error is no switch: thinking on is noted by what it sets, and nothing billed is drawn', async () => {
    const { model, clock, child } = await failed()
    model.pickThink('on')
    await tick(clock, 80)
    noSwitch(model)
    expect(child.unanswered).toEqual([`chats: set think=on after the error of apogee ${chat.join(' ')}`])
  })

  test('a temperature, then Unload and retry: no switch to claude -- the setting noted, and the retry past the recording noted too', async () => {
    const { model, clock, child } = await failed()
    model.apply({ temperature: 0.4 })
    model.retry()
    await tick(clock, 80)
    noSwitch(model)
    expect(child.unanswered).toEqual([`chats: set temperature=0.4 after the error of apogee ${chat.join(' ')}`, `chats: turn 2 of apogee ${chat.join(' ')}`])
  })

  test('another model picked is noted as the model it names: set model=vast', async () => {
    const { model, clock, child } = await failed()
    model.pickModel('vast')
    await tick(clock, 80)
    noSwitch(model)
    expect(child.unanswered).toEqual([`chats: set model=vast after the error of apogee ${chat.join(' ')}`])
  })
})

describe('Streaming (chat): the live activity, and Stop', () => {
  test('mid-reasoning the activity is the turn at work: live, its lines drawn, and no pending line beside it', async () => {
    const { model, clock } = await opened({ stdout: streaming })
    model.writeAhead('Where do fetches start in this repo, and what cancels them?')
    model.writeAhead('So one slow host takes the whole batch down.')
    await tick(clock, 150)
    expect(model.transcript.status.case).toBe('thinking')
    expect(model.face().rows.filter((row) => row.kind === 'activity').at(-1)).toMatchObject({ kind: 'activity', live: true, lines: [{ text: 'utility — rewriting the follow-up into a search query with root' }, { text: '[tool] fetch_url' }, { text: '[tool] read_file' }] })
    expect(model.face().pending.shown).toBe(false)
  })

  test('Stop mid-reasoning writes a cancel, the turn reads stopping, then ends: stopped, no answer, the session taking a turn again', async () => {
    const { model, clock, children, child } = await opened({ stdout: streaming, cancelled: streamingCancel })
    model.setDraft('Where do fetches start in this repo, and what cancels them?')
    model.submit()
    await tick(clock, 95)
    model.setDraft('So one slow host takes the whole batch down.')
    model.submit()
    await tick(clock, 20)
    expect(model.transcript.status.case).toBe('thinking')
    model.cancel()
    expect(children[0]?.written.at(-1)).toBe('{"type":"cancel"}')
    expect(model.transcript.status.case).toBe('stopping')
    expect(model.face().composer.status).toBe('stopping')
    await tick(clock, 200)
    expect(lastRow(model)).toMatchObject({ kind: 'quiet', text: 'stopped' })
    expect(model.face().rows.filter((row) => row.kind === 'answer')).toHaveLength(1)
    expect(model.face().rows.filter((row) => row.kind === 'activity').at(-1)).toMatchObject({ live: false })
    expect(model.transcript.status).toEqual({ case: 'ready' })
    expect(child.unanswered).toEqual([])
  })
})

describe('the session states (GUI 6a): Restarting, Not sent, Session ended, the held lines', () => {
  test('restarting: a suite move after a turn stops the child, Send off and the input saying why; an attach made then is held, then reaches the next child, which resumes the chat under the suite', async () => {
    const { model, clock, children, child } = await opened([{ stdout: resumedKv, exitAfter: 5 }, { stdout: resumedKvPair }], { launch: { resume: kvID } })
    await tick(clock, 1)
    expect(model.face().rows.map((row) => row.kind)).toEqual(['warning', 'user', 'answer', 'divider', 'notice'])
    model.presentSuites([{ name: 'pair', footprint: undefined, isDefault: false }], undefined, 'pair')
    expect(model.canSwitch).toBe(true)
    await model.switchSuite()
    expect(model.transcript.status).toEqual({ case: 'restarting' })
    model.attach('storage-engine-design.pdf')
    expect(model.face().composer).toMatchObject({ placeholder: 'The session is restarting', status: 'restarting', canSend: false })
    expect(model.face().held).toEqual({ shown: true, words: 'Sent once the session is back: attach storage-engine-design.pdf', help: 'Nothing reaches the session while its child is stopped or started; these go to the next one as soon as it speaks.' })
    await tick(clock, 4)
    expect(children.length).toBe(1)
    await tick(clock, 1)
    expect(children.length).toBe(2)
    expect(child.calls.at(-1)?.arguments).toEqual([...chat, '--resume', kvID, '--suite', 'pair'])
    await tick(clock, 3)
    expect(model.transcript.status).toEqual({ case: 'ready' })
    expect(children[1]?.written).toEqual(['{"type":"attach","path":"storage-engine-design.pdf"}'])
    expect(model.face().held.shown).toBe(false)
    expect(model.suiteWords).toBe('suite pair')
  })

  test('not sent: a turn its closed child never took is marked so, with Send again enabled only once the session takes a turn', async () => {
    const { model, clock, children, child } = await opened([{ stdout: resumedKv, exitAfter: 2 }, { stdout: resumedKvTurn }], { launch: { resume: kvID } })
    await tick(clock, 2)
    await model.restart()
    model.writeAhead('Would a 16-bit cache fit at 128K?')
    await settled()
    const notSent = lastRow(model)
    expect(notSent).toMatchObject({ kind: 'notSent', text: 'Would a 16-bit cache fit at 128K?', canResend: true, enabled: false })
    await tick(clock, 2)
    await tick(clock, 2)
    expect(model.transcript.status).toEqual({ case: 'ready' })
    expect(model.face().rows.find((row) => row.kind === 'notSent')).toMatchObject({ canResend: true, enabled: true })
    model.resend('Would a 16-bit cache fit at 128K?')
    expect(children[1]?.written).toEqual(['{"type":"user","text":"Would a 16-bit cache fit at 128K?"}'])
    expect(model.face().rows.find((row) => row.kind === 'notSent')).toMatchObject({ canResend: false })
    // The returned child answers it, as its capture recorded the turn.
    await tick(clock, 60)
    expect(model.transcript.status).toEqual({ case: 'ready' })
    expect(lastRow(model)).toMatchObject({ kind: 'answer', finished: true })
    expect(lastRow(model).kind === 'answer' ? (lastRow(model) as Extract<RowFace, { kind: 'answer' }>).text : '').toMatch(/^A 16-bit cache is twice the 8-bit one/)
    expect(child.unanswered).toEqual([])
  })

  test('session ended: a stream cut mid-answer, the card in the CLI’s words and its details; lines held for its turn stay said, and Start again sends them to the next child once it speaks', async () => {
    const { model, clock, children, child } = await opened(
      [{ stdout: resumedKvEnded, exit: 2, stderr: 'loading\nfatal: the model went away\n' }, { stdout: resumedKv, continuations: { after: 'session', by: { 'set temperature=0.4': resumedKvHeld } } }],
      { launch: { resume: kvID } },
    )
    model.writeAhead('Would a 16-bit cache fit at 128K?')
    model.apply({ temperature: 0.4 })
    model.compact()
    expect(model.held.map((line) => line.case)).toEqual(['set', 'compact'])
    await tick(clock, 40)
    expect(lastRow(model)).toMatchObject({ kind: 'ended', text: 'apogee exited with status 2: fatal: the model went away', details: 'loading\nfatal: the model went away', atLaunch: false, canStartAgain: true })
    expect(model.face().rows.filter((row) => row.kind === 'answer').at(-1)).toMatchObject({ text: 'A 16-bit cache is twice the 8-bit one, 4.8 GB at 128K, so with the weights it' })
    expect(model.face().held.words).toBe('Sent when the session starts again: temperature 0.4 · compact')
    expect(model.face().composer).toMatchObject({ placeholder: 'The session has ended', status: 'session ended', canAttach: false })
    await model.start()
    expect(model.face().held.words).toBe('Sent once the session is back: temperature 0.4 · compact')
    await tick(clock, 2)
    expect(children[1]?.written).toEqual(['{"type":"set","temperature":0.4}', '{"type":"compact"}'])
    expect(model.face().held.shown).toBe(false)
    // The next child answers each, in the CLI's words.
    await tick(clock, 6)
    const notices = model.face().rows.flatMap((row) => (row.kind === 'notice' ? [row.text] : []))
    expect(notices.slice(-2)).toEqual(['temperature: 0.4 -- from the next turn', 'history compacted'])
    expect(child.unanswered).toEqual([])
  })

  test('Start again resumes a conversation it was not launched to resume: a new chat answered once, its child dead, comes back with --resume and its turns read back', async () => {
    const { model, clock, children, child } = await opened(
      { stdout: chatKv, exit: 2, stderr: 'fatal: the model went away\n' },
      { chats: [{ arguments: [...chat, '--resume', kvID], answer: { stdout: resumedKv } }] },
    )
    model.writeAhead('Where does llama.cpp decide how big the KV cache is, and what does it cost for gpt-oss-20b?')
    await tick(clock, 120)
    expect(model.transcript.status).toEqual({ case: 'ended' })
    await model.start()
    await tick(clock, 2)
    expect(children).toHaveLength(2)
    expect(child.calls.filter((call) => call.kind === 'chats').at(-1)?.arguments).toEqual([...chat, '--resume', kvID])
    expect(child.calls.some((call) => call.kind === 'reads' && call.arguments.slice(0, 3).join(' ') === `chats show ${kvID}`)).toBe(true)
    expect(model.face().rows.map((row) => row.kind).slice(0, 4)).toEqual(['warning', 'user', 'answer', 'divider'])
    expect(model.transcript.status).toEqual({ case: 'ready' })
    expect(child.unanswered).toEqual([])
  })

  test('the reader is the one handed in: a resumed chat is read back through it, as `chats show <id>`', async () => {
    const { model, child } = await opened({ stdout: resumedKv }, { launch: { resume: kvID } })
    expect(child.calls.find((call) => call.kind === 'reads')?.arguments).toEqual(['chats', 'show', kvID])
    expect(model.face().rows[0]).toMatchObject({ kind: 'warning', text: 'a message could not be read and was skipped' })
  })

  test('the held lines reach the next child in the order made, a suite move last', async () => {
    const { model, clock, children } = await opened([{ stdout: resumedKvEnded, exit: 2 }, { stdout: resumedKv, exitAfter: 5 }, { stdout: resumedKvPair }], { launch: { resume: kvID } })
    model.writeAhead('Would a 16-bit cache fit at 128K?')
    model.presentSuites([{ name: 'pair', footprint: undefined, isDefault: false }], undefined, 'pair')
    await model.switchSuite()
    model.attach('notes.md')
    model.apply({ temperature: 0.4 })
    expect(model.held.map((line) => line.case)).toEqual(['suite', 'attach', 'set'])
    await tick(clock, 40)
    await model.start()
    await tick(clock, 2)
    expect(children[1]?.written).toEqual(['{"type":"attach","path":"notes.md"}', '{"type":"set","temperature":0.4}'])
    expect(model.transcript.status).toEqual({ case: 'restarting' })
  })
})
